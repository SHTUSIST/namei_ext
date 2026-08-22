// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FIXED_PATHS 4U
#define BUFFER_SIZE 128U
#define MAX_WIDTH 256U

struct observed_file {
	char path[64];
	char bytes[BUFFER_SIZE];
	unsigned int error;
	mode_t mode;
	uid_t uid;
	gid_t gid;
	uint64_t dev;
	uint64_t ino;
	off_t size;
};

struct consumer {
	const char *root;
	bool atomicwriter;
	unsigned int width;
	uid_t uid;
	gid_t gid;
	int root_fd;
	int old_fd;
	struct stat root_initial;
	struct stat old_initial;
	struct stat update_app;
	bool update_app_valid;
	struct operation_counts *observations;
	unsigned int state_count;
};

struct operation_counts {
	unsigned int readdir;
	unsigned int open;
	unsigned int read;
	unsigned int stat;
	unsigned int missing;
	unsigned int old_fd;
	unsigned int visible_root_entries;
	uint64_t root_dev;
	uint64_t root_ino;
	uint64_t app_dev;
	uint64_t app_ino;
	uint64_t old_dev;
	uint64_t old_ino;
	char old_bytes[BUFFER_SIZE];
	unsigned int old_error;
	mode_t old_mode;
	uid_t old_uid;
	gid_t old_gid;
	off_t old_size;
	struct observed_file files[MAX_WIDTH];
	unsigned int file_count;
	char root_names[MAX_WIDTH][32];
	unsigned int root_name_count;
	char config_names[4][32];
	unsigned int config_name_count;
	char tls_names[4][32];
	unsigned int tls_name_count;
};

static int record_name(struct operation_counts *counts, const char *relative,
		       const char *name)
{
	char (*names)[32];
	unsigned int *count;
	unsigned int capacity;

	if (!*relative) {
		names = counts->root_names;
		count = &counts->root_name_count;
		capacity = MAX_WIDTH;
	} else if (!strcmp(relative, "config")) {
		names = counts->config_names;
		count = &counts->config_name_count;
		capacity = 4;
	} else {
		names = counts->tls_names;
		count = &counts->tls_name_count;
		capacity = 4;
	}
	if (*count >= capacity || strlen(name) >= sizeof(names[0]))
		return -EOVERFLOW;
	strcpy(names[*count], name);
	(*count)++;
	return 0;
}

static int record_file(struct operation_counts *counts, const char *path,
		       const char *bytes, const struct stat *metadata,
		       unsigned int error)
{
	struct observed_file *file;

	if (counts->file_count >= MAX_WIDTH ||
	    strlen(path) >= sizeof(counts->files[0].path) ||
	    strlen(bytes) >= sizeof(counts->files[0].bytes))
		return -EOVERFLOW;
	file = &counts->files[counts->file_count++];
	strcpy(file->path, path);
	strcpy(file->bytes, bytes);
	file->error = error;
	if (metadata) {
		file->mode = metadata->st_mode & 0777;
		file->uid = metadata->st_uid;
		file->gid = metadata->st_gid;
		file->dev = metadata->st_dev;
		file->ino = metadata->st_ino;
		file->size = metadata->st_size;
	}
	return 0;
}

static bool second_generation(const char *state)
{
	return !strcmp(state, "update") || !strcmp(state, "no-op");
}

static int expected_entry(const char *name, unsigned int width, bool second)
{
	unsigned int index;
	char generated[32];

	if (!strcmp(name, "config") || !strcmp(name, "tls"))
		return 1;
	if (!strcmp(name, second ? "added.conf" : "retired.conf"))
		return 1;
	for (index = 0; index < width - FIXED_PATHS; index++) {
		if (snprintf(generated, sizeof(generated), "entry-%03u.conf",
			     index) < 0)
			return -EIO;
		if (!strcmp(name, generated))
			return 1;
	}
	return 0;
}

static int check_directory(struct consumer *consumer, const char *relative,
			   bool second, struct operation_counts *counts)
{
	char path[4096];
	struct dirent *entry;
	DIR *directory;
	unsigned int seen = 0;
	unsigned int expected;
	int length;
	int ret = 0;

	if (*relative) {
		length = snprintf(path, sizeof(path), "%s/%s", consumer->root,
				  relative);
		expected = 1;
	} else {
		length = snprintf(path, sizeof(path), "%s", consumer->root);
		expected = consumer->width - 1;
	}
	if (length < 0 || (size_t)length >= sizeof(path))
		return -ENAMETOOLONG;
	directory = opendir(path);
	if (!directory)
		return -errno;
	counts->readdir++;
	errno = 0;
	while ((entry = readdir(directory))) {
		int present;

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		if (!*relative && consumer->atomicwriter &&
		    !strncmp(entry->d_name, "..", 2))
			continue;
		if (!strcmp(relative, "config"))
			present = !strcmp(entry->d_name, "app.conf");
		else if (!strcmp(relative, "tls"))
			present = !strcmp(entry->d_name, "cert.pem");
		else
			present = expected_entry(entry->d_name, consumer->width,
						 second);
		if (present < 0) {
			ret = present;
			break;
		}
		if (!present) {
			ret = -EINVAL;
			break;
		}
		ret = record_name(counts, relative, entry->d_name);
		if (ret)
			break;
		seen++;
	}
	if (!ret && errno)
		ret = -errno;
	if (closedir(directory) && !ret)
		ret = -errno;
	if (!ret && seen != expected)
		ret = -EINVAL;
	if (!ret && !*relative)
		counts->visible_root_entries = seen;
	return ret;
}

static int read_expected(struct consumer *consumer, const char *relative,
			 const char *expected, mode_t mode,
			 struct operation_counts *counts, bool keep_old,
			 struct stat *metadata_out)
{
	char bytes[BUFFER_SIZE];
	struct stat metadata;
	ssize_t length;
	int fd;

	fd = openat(consumer->root_fd, relative, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	counts->open++;
	length = read(fd, bytes, sizeof(bytes) - 1);
	if (length < 0) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	counts->read++;
	bytes[length] = '\0';
	if (strcmp(bytes, expected)) {
		close(fd);
		return -EINVAL;
	}
	if (fstat(fd, &metadata)) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	counts->stat++;
	if (record_file(counts, relative, bytes, &metadata, 0)) {
		close(fd);
		return -EOVERFLOW;
	}
	if ((metadata.st_mode & 0777) != mode || metadata.st_uid != consumer->uid ||
	    metadata.st_gid != consumer->gid) {
		close(fd);
		return -EINVAL;
	}
	if (metadata_out)
		*metadata_out = metadata;
	if (keep_old) {
		consumer->old_fd = fd;
		consumer->old_initial = metadata;
		return 0;
	}
	if (close(fd))
		return -errno;
	return 0;
}

static int read_generated(struct consumer *consumer, unsigned int index,
			  struct operation_counts *counts)
{
	char path[32];
	char expected[32];
	int path_length;
	int data_length;

	path_length = snprintf(path, sizeof(path), "entry-%03u.conf", index);
	data_length = snprintf(expected, sizeof(expected), "stable-%03u\n", index);
	if (path_length < 0 || data_length < 0 ||
	    (size_t)path_length >= sizeof(path) ||
	    (size_t)data_length >= sizeof(expected))
		return -EOVERFLOW;
	return read_expected(consumer, path, expected, 0644, counts, false, NULL);
}

static int check_missing(struct consumer *consumer, const char *relative,
			 struct operation_counts *counts)
{
	int fd = openat(consumer->root_fd, relative, O_RDONLY | O_CLOEXEC);

	if (fd >= 0) {
		close(fd);
		return -EINVAL;
	}
	counts->missing++;
	if (errno != ENOENT)
		return -errno;
	return record_file(counts, relative, "", NULL, ENOENT);
}

static int check_old_fd(struct consumer *consumer,
			struct operation_counts *counts)
{
	char bytes[BUFFER_SIZE];
	struct stat metadata;
	ssize_t length;

	length = pread(consumer->old_fd, bytes, sizeof(bytes) - 1, 0);
	if (length < 0)
		return -errno;
	counts->old_fd++;
	bytes[length] = '\0';
	if (strcmp(bytes, "version=0\n"))
		return -EINVAL;
	strcpy(counts->old_bytes, bytes);
	if (fstat(consumer->old_fd, &metadata))
		return -errno;
	counts->stat++;
	counts->old_dev = metadata.st_dev;
	counts->old_ino = metadata.st_ino;
	counts->old_mode = metadata.st_mode & 0777;
	counts->old_uid = metadata.st_uid;
	counts->old_gid = metadata.st_gid;
	counts->old_size = metadata.st_size;
	return metadata.st_dev == consumer->old_initial.st_dev &&
		metadata.st_ino == consumer->old_initial.st_ino ? 0 : -EINVAL;
}

static int check_state(struct consumer *consumer, const char *state,
		       struct operation_counts *counts)
{
	struct stat root_current;
	struct stat app_metadata;
	bool second = second_generation(state);
	unsigned int index;
	int ret;

	if (consumer->root_fd < 0) {
		consumer->root_fd = open(consumer->root,
					      O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (consumer->root_fd < 0)
			return -errno;
		counts->open++;
		if (fstat(consumer->root_fd, &consumer->root_initial))
			return -errno;
		counts->stat++;
	}
	if (fstat(consumer->root_fd, &root_current))
		return -errno;
	counts->stat++;
	if (root_current.st_dev != consumer->root_initial.st_dev ||
	    root_current.st_ino != consumer->root_initial.st_ino)
		return -EINVAL;
	counts->root_dev = root_current.st_dev;
	counts->root_ino = root_current.st_ino;
	ret = check_directory(consumer, "", second, counts);
	if (!ret)
		ret = check_directory(consumer, "config", second, counts);
	if (!ret)
		ret = check_directory(consumer, "tls", second, counts);
	if (!ret)
		ret = read_expected(consumer, "config/app.conf",
				    second ? "version=1\n" : "version=0\n",
				    second ? 0600 : 0644, counts,
				    !strcmp(state, "initial"), &app_metadata);
	if (!ret && !strcmp(state, "update")) {
		consumer->update_app = app_metadata;
		consumer->update_app_valid = true;
	}
	if (!ret) {
		counts->app_dev = app_metadata.st_dev;
		counts->app_ino = app_metadata.st_ino;
		if (!strcmp(state, "initial")) {
			counts->old_dev = consumer->old_initial.st_dev;
			counts->old_ino = consumer->old_initial.st_ino;
		}
	}
	if (!ret && !strcmp(state, "no-op") &&
	    (!consumer->update_app_valid ||
	     app_metadata.st_dev != consumer->update_app.st_dev ||
	     app_metadata.st_ino != consumer->update_app.st_ino))
		ret = -EINVAL;
	if (!ret)
		ret = read_expected(consumer, "tls/cert.pem",
				    second ? "certificate-v1\n" :
					     "certificate-v0\n",
				    0400, counts, false, NULL);
	if (!ret && second)
		ret = read_expected(consumer, "added.conf", "added\n", 0644,
				    counts, false, NULL);
	if (!ret && !second)
		ret = read_expected(consumer, "retired.conf", "retired\n", 0644,
				    counts, false, NULL);
	if (!ret)
		ret = check_missing(consumer,
				    second ? "retired.conf" : "added.conf", counts);
	for (index = 0; !ret && index < consumer->width - FIXED_PATHS;
	     index++)
		ret = read_generated(consumer, index, counts);
	if (!ret && strcmp(state, "initial"))
		ret = check_old_fd(consumer, counts);
	return ret;
}

static void print_json_string(const char *value)
{
	const unsigned char *cursor = (const unsigned char *)value;

	putchar('"');
	while (*cursor) {
		switch (*cursor) {
		case '\\':
			fputs("\\\\", stdout);
			break;
		case '"':
			fputs("\\\"", stdout);
			break;
		case '\n':
			fputs("\\n", stdout);
			break;
		case '\r':
			fputs("\\r", stdout);
			break;
		case '\t':
			fputs("\\t", stdout);
			break;
		default:
			if (*cursor < 0x20)
				printf("\\u%04x", *cursor);
			else
				putchar(*cursor);
		}
		cursor++;
	}
	putchar('"');
}

static void print_name_array(const char names[][32], unsigned int count)
{
	unsigned int index;

	putchar('[');
	for (index = 0; index < count; index++) {
		if (index)
			putchar(',');
		print_json_string(names[index]);
	}
	putchar(']');
}

static void print_file_array(const struct operation_counts *counts)
{
	unsigned int index;

	putchar('[');
	for (index = 0; index < counts->file_count; index++) {
		const struct observed_file *file = &counts->files[index];

		if (index)
			putchar(',');
		fputs("{\"path\":", stdout);
		print_json_string(file->path);
		fputs(",\"bytes\":", stdout);
		print_json_string(file->bytes);
		printf(",\"error\":%u,\"mode\":%u,\"uid\":%u,"
		       "\"gid\":%u,\"dev\":%llu,\"ino\":%llu,"
		       "\"size\":%lld}",
		       file->error, file->mode, file->uid, file->gid,
		       (unsigned long long)file->dev,
		       (unsigned long long)file->ino, (long long)file->size);
	}
	putchar(']');
}

static void print_observation(const char *state,
			      const struct operation_counts *counts)
{
	printf("{\"state\":\"%s\",\"pass\":true,\"error\":0,"
	       "\"readdir_ops\":%u,\"open_ops\":%u,"
	       "\"read_ops\":%u,\"stat_ops\":%u,"
	       "\"missing_ops\":%u,\"old_fd_ops\":%u,"
	       "\"visible_root_entries\":%u,"
	       "\"root_dev\":%llu,\"root_ino\":%llu,"
	       "\"app_dev\":%llu,\"app_ino\":%llu,"
	       "\"old_dev\":%llu,\"old_ino\":%llu,\"old_bytes\":",
	       state, counts->readdir, counts->open, counts->read, counts->stat,
	       counts->missing, counts->old_fd, counts->visible_root_entries,
	       (unsigned long long)counts->root_dev,
	       (unsigned long long)counts->root_ino,
	       (unsigned long long)counts->app_dev,
	       (unsigned long long)counts->app_ino,
	       (unsigned long long)counts->old_dev,
	       (unsigned long long)counts->old_ino);
	print_json_string(counts->old_bytes);
	printf(",\"old_error\":%u,\"old_mode\":%u,\"old_uid\":%u,"
	       "\"old_gid\":%u,\"old_size\":%lld,\"files\":",
	       counts->old_error, counts->old_mode, counts->old_uid,
	       counts->old_gid, (long long)counts->old_size);
	print_file_array(counts);
	fputs(",\"root_entries\":", stdout);
	print_name_array(counts->root_names, counts->root_name_count);
	fputs(",\"config_entries\":", stdout);
	print_name_array(counts->config_names, counts->config_name_count);
	fputs(",\"tls_entries\":", stdout);
	print_name_array(counts->tls_names, counts->tls_name_count);
	fputs("}\n", stdout);
}

int main(int argc, char **argv)
{
	static const char *const states[] = {
		"initial", "update", "no-op", "rollback",
	};
	struct consumer consumer = {
		.root_fd = -1,
		.old_fd = -1,
	};
	struct operation_counts observations[4] = {};
	char *end = NULL;
	char state[32];
	unsigned long value;

	if (argc != 6) {
		fprintf(stderr,
			"usage: %s ROOT atomicwriter|namei_ext WIDTH UID GID\n",
			argv[0]);
		return 2;
	}
	consumer.root = argv[1];
	if (!strcmp(argv[2], "atomicwriter"))
		consumer.atomicwriter = true;
	else if (strcmp(argv[2], "namei_ext"))
		return 2;
	errno = 0;
	value = strtoul(argv[3], &end, 10);
	if (errno || !end || *end || value < FIXED_PATHS || value > 256)
		return 2;
	consumer.width = (unsigned int)value;
	errno = 0;
	value = strtoul(argv[4], &end, 10);
	if (errno || !end || *end || value > UINT32_MAX)
		return 2;
	consumer.uid = (uid_t)value;
	errno = 0;
	value = strtoul(argv[5], &end, 10);
	if (errno || !end || *end || value > UINT32_MAX)
		return 2;
	consumer.gid = (gid_t)value;
	consumer.observations = observations;
	if (geteuid() == 0) {
		if (setgroups(0, NULL) || setgid(consumer.gid) ||
		    setuid(consumer.uid)) {
			perror("drop consumer credentials");
			return 1;
		}
	} else if (getuid() != consumer.uid || getgid() != consumer.gid) {
		fprintf(stderr, "consumer runtime identity mismatch\n");
		return 1;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	while (fgets(state, sizeof(state), stdin)) {
		char *newline = strchr(state, '\n');
		int ret;

		if (newline)
			*newline = '\0';
		if (!strcmp(state, "quit"))
			break;
		if (!strcmp(state, "evidence")) {
			unsigned int index;

			if (consumer.state_count != 4)
				return 2;
			for (index = 0; index < 4; index++)
				print_observation(states[index], &observations[index]);
			if (ferror(stdout))
				return 1;
			continue;
		}
		if (consumer.state_count >= 4 ||
		    strcmp(state, states[consumer.state_count]))
			return 2;
		ret = check_state(&consumer, state,
				  &observations[consumer.state_count]);
		printf("{\"state\":\"%s\",\"pass\":%s,\"error\":%d}\n",
		       state, ret ? "false" : "true", ret ? -ret : 0);
		if (ferror(stdout))
			return 1;
		if (ret)
			return 1;
		consumer.state_count++;
	}
	if (consumer.old_fd >= 0 && close(consumer.old_fd))
		return 1;
	if (consumer.root_fd >= 0 && close(consumer.root_fd))
		return 1;
	return 0;
}

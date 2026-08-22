// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include "namei_ext_harness.h"

#include <bpf/bpf.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FIXED_PATHS 4U
#define MAX_WIDTH 256U
#define ACK_SIZE (256U * 1024U)

enum counter_key {
	COUNTER_TOTAL = 0,
	COUNTER_LOOKUP = 1,
	COUNTER_READDIR = 2,
	COUNTER_SELECT = 3,
	COUNTER_PASS = 4,
	COUNTER_HIDE = 5,
	COUNTER_MAX = 6,
};

struct entry {
	char relative[64];
	char parent_relative[32];
	char name[32];
	char v0_data[64];
	char v1_data[64];
	mode_t v0_mode;
	mode_t v1_mode;
	bool v0_present;
	bool v1_present;
	uint32_t v0_target;
	uint32_t v1_target;
	struct stat v0_initial;
	struct stat v1_initial;
};

struct phase_times {
	uint64_t setup_ns;
	uint64_t initial_publish_ns;
	uint64_t initial_consumer_ns;
	uint64_t update_publish_ns;
	uint64_t update_consumer_ns;
	uint64_t no_op_publish_ns;
	uint64_t no_op_consumer_ns;
	uint64_t rollback_publish_ns;
	uint64_t rollback_consumer_ns;
};

struct setup_phase_times {
	uint64_t object_preparation_ns;
	uint64_t target_registration_ns;
	uint64_t map_population_ns;
	uint64_t consumer_cgroup_move_ns;
};

struct consumer_process {
	pid_t pid;
	FILE *input;
	FILE *output;
};

struct sample {
	const char *policy_path;
	const char *consumer_path;
	const char *output_path;
	const char *parent;
	const char *cgroup_root;
	unsigned int width;
	unsigned int boot;
	unsigned int pair;
	unsigned int order;
	uid_t uid;
	gid_t gid;
	char logical[PATH_MAX];
	char lower[PATH_MAX];
	char v0[PATH_MAX];
	char v1[PATH_MAX];
	char cgroup[PATH_MAX];
	struct entry entries[MAX_WIDTH];
	struct phase_times phases;
	struct setup_phase_times setup_phases;
	char acknowledgements[4][ACK_SIZE];
	unsigned int state_count;
	unsigned int acknowledgement_count;
	uint64_t attach_ns;
	uint64_t wall_span_ns;
	uint64_t active_total_ns;
	uint64_t publication_only_ns;
	uint64_t consumer_only_ns;
	uint64_t counters[COUNTER_MAX];
	unsigned int lower_files;
	uint64_t lower_bytes;
	unsigned int observed_lower_files;
	uint64_t observed_lower_bytes;
	unsigned int logical_files;
	unsigned int registered_targets;
	unsigned int managed_identity_checks;
	unsigned int managed_hidden_checks;
	unsigned int lower_preservation_checks;
	unsigned int unmanaged_checks;
	bool rollback_original_v0;
	bool unmanaged_scope_pass;
	int consumer_exit_status;
	bool generation_removed;
	bool view_maps_empty;
	bool policy_destroyed;
	bool targets_cleared;
	bool cgroup_removed;
	bool logical_removed;
	bool lower_removed;
	int cleanup_consumer_error;
	int cleanup_generation_error;
	int cleanup_map_error;
	size_t cleanup_v0_map_count;
	size_t cleanup_v1_map_count;
	int cleanup_policy_error;
	int cleanup_targets_error;
	int cleanup_cgroup_error;
	int cleanup_logical_lookup_error;
	int cleanup_lower_lookup_error;
};

static int join(char *destination, size_t size, const char *left,
		const char *right)
{
	return namei_ext_path_join(destination, size, left, right);
}

static int monotonic_ns(uint64_t *value)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return -errno;
	*value = (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
	return 0;
}

static int mkdir_exact(const char *path, mode_t mode)
{
	if (mkdir(path, mode))
		return -errno;
	return 0;
}

static int create_file(const char *path, const char *data, mode_t mode,
		       uid_t uid, gid_t gid, struct stat *metadata_out)
{
	int fd;
	size_t length = strlen(data);
	ssize_t written;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
	if (fd < 0)
		return -errno;
	written = write(fd, data, length);
	if (written < 0) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	if ((size_t)written != length) {
		close(fd);
		return -EIO;
	}
	if (fchown(fd, uid, gid)) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	if (fchmod(fd, mode)) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	if (metadata_out && fstat(fd, metadata_out)) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	if (close(fd))
		return -errno;
	return 0;
}

static int initialize_entries(struct sample *sample)
{
	struct entry *entry;
	unsigned int index;
	uint32_t target = 1;

	memset(sample->entries, 0, sizeof(sample->entries));
	entry = &sample->entries[0];
	strcpy(entry->relative, "config/app.conf");
	strcpy(entry->parent_relative, "config");
	strcpy(entry->name, "app.conf");
	strcpy(entry->v0_data, "version=0\n");
	strcpy(entry->v1_data, "version=1\n");
	entry->v0_mode = 0644;
	entry->v1_mode = 0600;
	entry->v0_present = true;
	entry->v1_present = true;
	entry = &sample->entries[1];
	strcpy(entry->relative, "tls/cert.pem");
	strcpy(entry->parent_relative, "tls");
	strcpy(entry->name, "cert.pem");
	strcpy(entry->v0_data, "certificate-v0\n");
	strcpy(entry->v1_data, "certificate-v1\n");
	entry->v0_mode = 0400;
	entry->v1_mode = 0400;
	entry->v0_present = true;
	entry->v1_present = true;
	entry = &sample->entries[2];
	strcpy(entry->relative, "retired.conf");
	strcpy(entry->name, "retired.conf");
	strcpy(entry->v0_data, "retired\n");
	entry->v0_mode = 0644;
	entry->v0_present = true;
	entry = &sample->entries[3];
	strcpy(entry->relative, "added.conf");
	strcpy(entry->name, "added.conf");
	strcpy(entry->v1_data, "added\n");
	entry->v1_mode = 0644;
	entry->v1_present = true;
	for (index = FIXED_PATHS; index < sample->width; index++) {
		entry = &sample->entries[index];
		if (snprintf(entry->relative, sizeof(entry->relative),
			     "entry-%03u.conf", index - FIXED_PATHS) < 0 ||
		    snprintf(entry->name, sizeof(entry->name),
			     "entry-%03u.conf", index - FIXED_PATHS) < 0 ||
		    snprintf(entry->v0_data, sizeof(entry->v0_data),
			     "stable-%03u\n", index - FIXED_PATHS) < 0)
			return -EIO;
		strcpy(entry->v1_data, entry->v0_data);
		entry->v0_mode = 0644;
		entry->v1_mode = 0644;
		entry->v0_present = true;
		entry->v1_present = true;
	}
	for (index = 0; index < sample->width; index++) {
		entry = &sample->entries[index];
		if (entry->v0_present)
			entry->v0_target = target++;
		if (entry->v1_present)
			entry->v1_target = target++;
	}
	return 0;
}

static int initialize_paths(struct sample *sample)
{
	int length;

	if (join(sample->logical, sizeof(sample->logical), sample->parent,
		 "logical") ||
	    join(sample->lower, sizeof(sample->lower), sample->parent, "lower") ||
	    join(sample->v0, sizeof(sample->v0), sample->lower, "v0") ||
	    join(sample->v1, sizeof(sample->v1), sample->lower, "v1"))
		return -ENAMETOOLONG;
	length = snprintf(sample->cgroup, sizeof(sample->cgroup),
			  "%s/namei-ext-configmap-q-%ld", sample->cgroup_root,
			  (long)getpid());
	if (length < 0 || (size_t)length >= sizeof(sample->cgroup))
		return -ENAMETOOLONG;
	return 0;
}

static int setup_directories(struct sample *sample)
{
	char path[PATH_MAX];
	int ret;

	ret = mkdir_exact(sample->logical, 0755);
	if (!ret && join(path, sizeof(path), sample->logical, "config"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = mkdir_exact(path, 0755);
	if (!ret && join(path, sizeof(path), sample->logical, "tls"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = mkdir_exact(path, 0755);
	if (!ret)
		ret = mkdir_exact(sample->lower, 0755);
	if (!ret)
		ret = mkdir_exact(sample->v0, 0755);
	if (!ret && join(path, sizeof(path), sample->v0, "config"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = mkdir_exact(path, 0755);
	if (!ret && join(path, sizeof(path), sample->v0, "tls"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = mkdir_exact(path, 0755);
	if (!ret)
		ret = mkdir_exact(sample->v1, 0755);
	if (!ret && join(path, sizeof(path), sample->v1, "config"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = mkdir_exact(path, 0755);
	if (!ret && join(path, sizeof(path), sample->v1, "tls"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = mkdir_exact(path, 0755);
	if (!ret)
		ret = mkdir_exact(sample->cgroup, 0755);
	return ret;
}

static int setup_files(struct sample *sample)
{
	char path[PATH_MAX];
	unsigned int index;

	for (index = 0; index < sample->width; index++) {
		struct entry *entry = &sample->entries[index];
		int ret;

		if (join(path, sizeof(path), sample->logical, entry->relative))
			return -ENAMETOOLONG;
		ret = create_file(path, "", 0644, sample->uid, sample->gid, NULL);
		if (ret)
			return ret;
		sample->logical_files++;
		if (entry->v0_present) {
			if (join(path, sizeof(path), sample->v0, entry->relative))
				return -ENAMETOOLONG;
			ret = create_file(path, entry->v0_data, entry->v0_mode,
					  sample->uid, sample->gid,
					  &entry->v0_initial);
			if (ret)
				return ret;
			sample->lower_files++;
			sample->lower_bytes += strlen(entry->v0_data);
		}
		if (entry->v1_present) {
			if (join(path, sizeof(path), sample->v1, entry->relative))
				return -ENAMETOOLONG;
			ret = create_file(path, entry->v1_data, entry->v1_mode,
					  sample->uid, sample->gid,
					  &entry->v1_initial);
			if (ret)
				return ret;
			sample->lower_files++;
			sample->lower_bytes += strlen(entry->v1_data);
		}
	}
	return 0;
}

static int register_targets(struct sample *sample)
{
	struct namei_ext_target_registration *targets;
	char (*paths)[PATH_MAX];
	size_t target_count = 0;
	size_t target_capacity = 2 * sample->width;
	unsigned int index;
	int ret = 0;

	targets = calloc(target_capacity, sizeof(*targets));
	paths = calloc(target_capacity, sizeof(*paths));
	if (!targets || !paths) {
		ret = -ENOMEM;
		goto out;
	}

	for (index = 0; index < sample->width; index++) {
		struct entry *entry = &sample->entries[index];

		if (entry->v0_present) {
			ret = join(paths[target_count], sizeof(paths[target_count]),
				   sample->v0, entry->relative);
			if (ret)
				goto out;
			targets[target_count].path = paths[target_count];
			targets[target_count].target_id = entry->v0_target;
			target_count++;
		}
		if (entry->v1_present) {
			ret = join(paths[target_count], sizeof(paths[target_count]),
				   sample->v1, entry->relative);
			if (ret)
				goto out;
			targets[target_count].path = paths[target_count];
			targets[target_count].target_id = entry->v1_target;
			target_count++;
		}
	}
	if (target_count != 2U * (sample->width - 1U)) {
		ret = -EINVAL;
		goto out;
	}
	ret = namei_ext_register_target_batch(sample->cgroup, targets,
					      target_count);
	if (!ret)
		sample->registered_targets = target_count;
out:
	free(paths);
	free(targets);
	return ret;
}

static int configure_maps(struct sample *sample,
			  struct namei_ext_harness_policy *policy,
			  uint64_t cgroup_id)
{
	char parent[PATH_MAX];
	unsigned int index;

	for (index = 0; index < sample->width; index++) {
		struct entry *entry = &sample->entries[index];
		int ret;

		if (*entry->parent_relative) {
			if (join(parent, sizeof(parent), sample->logical,
				 entry->parent_relative))
				return -ENAMETOOLONG;
		} else if (snprintf(parent, sizeof(parent), "%s", sample->logical) < 0) {
			return -EIO;
		}
		ret = namei_ext_component_map_update(
			policy, "configmap_publication_v0_views", cgroup_id,
			parent, entry->name, entry->v0_target);
		if (!ret)
			ret = namei_ext_component_map_update(
				policy, "configmap_publication_v1_views", cgroup_id,
				parent, entry->name, entry->v1_target);
		if (ret)
			return ret;
	}
	return 0;
}

static int delete_maps(struct sample *sample,
		       struct namei_ext_harness_policy *policy,
		       uint64_t cgroup_id)
{
	char parent[PATH_MAX];
	unsigned int index;
	int first = 0;

	for (index = 0; index < sample->width; index++) {
		struct entry *entry = &sample->entries[index];
		int ret;

		if (*entry->parent_relative) {
			if (join(parent, sizeof(parent), sample->logical,
				 entry->parent_relative))
				return -ENAMETOOLONG;
		} else {
			snprintf(parent, sizeof(parent), "%s", sample->logical);
		}
		ret = namei_ext_component_map_delete(
			policy, "configmap_publication_v0_views", cgroup_id,
			parent, entry->name);
		if (ret && !first)
			first = ret;
		ret = namei_ext_component_map_delete(
			policy, "configmap_publication_v1_views", cgroup_id,
			parent, entry->name);
		if (ret && !first)
			first = ret;
	}
	return first;
}

static int map_fd(struct namei_ext_harness_policy *policy,
		  const char *map_name)
{
	struct bpf_map *map = bpf_object__find_map_by_name(policy->obj, map_name);
	int fd;

	if (!map)
		return -ENOENT;
	fd = bpf_map__fd(map);
	return fd < 0 ? -EINVAL : fd;
}

static int set_generation(struct namei_ext_harness_policy *policy,
			  uint64_t cgroup_id, uint32_t generation,
			  bool present)
{
	int fd = map_fd(policy, "configmap_publication_generations");

	if (fd < 0)
		return fd;
	if (present) {
		if (bpf_map_update_elem(fd, &cgroup_id, &generation, BPF_ANY))
			return -errno;
	} else if (bpf_map_delete_elem(fd, &cgroup_id) && errno != ENOENT) {
		return -errno;
	}
	return 0;
}

static int move_pid_to_cgroup(const char *cgroup, pid_t pid)
{
	char path[PATH_MAX];
	char value[32];

	if (join(path, sizeof(path), cgroup, "cgroup.procs"))
		return -ENAMETOOLONG;
	if (snprintf(value, sizeof(value), "%ld\n", (long)pid) < 0)
		return -EIO;
	return namei_ext_write_text(path, value);
}

static int start_consumer(struct sample *sample,
			  struct consumer_process *consumer)
{
	int input_pipe[2];
	int output_pipe[2];
	char width[16];
	char uid[16];
	char gid[16];
	pid_t pid;

	memset(consumer, 0, sizeof(*consumer));
	if (pipe2(input_pipe, O_CLOEXEC) || pipe2(output_pipe, O_CLOEXEC))
		return -errno;
	pid = fork();
	if (pid < 0)
		return -errno;
	if (!pid) {
		snprintf(width, sizeof(width), "%u", sample->width);
		snprintf(uid, sizeof(uid), "%u", sample->uid);
		snprintf(gid, sizeof(gid), "%u", sample->gid);
		if (dup2(input_pipe[0], STDIN_FILENO) < 0 ||
		    dup2(output_pipe[1], STDOUT_FILENO) < 0)
			_exit(126);
		close(input_pipe[0]);
		close(input_pipe[1]);
		close(output_pipe[0]);
		close(output_pipe[1]);
		execl(sample->consumer_path, sample->consumer_path, sample->logical,
		      "namei_ext", width, uid, gid, NULL);
		_exit(127);
	}
	close(input_pipe[0]);
	close(output_pipe[1]);
	consumer->pid = pid;
	consumer->input = fdopen(input_pipe[1], "w");
	consumer->output = fdopen(output_pipe[0], "r");
	if (!consumer->input || !consumer->output)
		return -errno;
	setvbuf(consumer->input, NULL, _IOLBF, 0);
	return 0;
}

static int stop_consumer(struct consumer_process *consumer, bool request_quit,
			 int *exit_status)
{
	int status;
	int first = 0;

	if (request_quit && consumer->input &&
	    (fprintf(consumer->input, "quit\n") < 0 || fflush(consumer->input)))
		first = -EIO;
	if (consumer->input && fclose(consumer->input) && !first)
		first = -errno;
	consumer->input = NULL;
	if (consumer->output && fclose(consumer->output) && !first)
		first = -errno;
	consumer->output = NULL;
	if (consumer->pid <= 0)
		return first;
	if (waitpid(consumer->pid, &status, 0) < 0)
		return first ? first : -errno;
	consumer->pid = 0;
	if (WIFEXITED(status))
		*exit_status = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		*exit_status = 128 + WTERMSIG(status);
	else
		*exit_status = -1;
	if ((!WIFEXITED(status) || WEXITSTATUS(status)) && !first)
		first = -ECHILD;
	return first;
}

static int send_state(struct sample *sample, struct consumer_process *consumer,
		      const char *state, uint64_t *elapsed)
{
	char expected[128];
	char acknowledgement[256];
	uint64_t start;
	uint64_t end;
	char *newline;
	int ret;

	if (sample->state_count >= 4)
		return -EOVERFLOW;
	ret = monotonic_ns(&start);
	if (ret)
		return ret;
	if (fprintf(consumer->input, "%s\n", state) < 0 ||
	    fflush(consumer->input))
		return -EIO;
	if (!fgets(acknowledgement, sizeof(acknowledgement), consumer->output))
		return ferror(consumer->output) ? -EIO : -EPIPE;
	ret = monotonic_ns(&end);
	if (ret)
		return ret;
	newline = strchr(acknowledgement, '\n');
	if (newline)
		*newline = '\0';
	if (snprintf(expected, sizeof(expected),
		     "{\"state\":\"%s\",\"pass\":true,\"error\":0}",
		     state) < 0 || strcmp(acknowledgement, expected))
		return -EINVAL;
	*elapsed = end - start;
	sample->state_count++;
	return 0;
}

static int collect_evidence(struct sample *sample,
			    struct consumer_process *consumer)
{
	static const char *const states[] = {
		"initial", "update", "no-op", "rollback",
	};
	unsigned int index;

	if (sample->state_count != 4 ||
	    fprintf(consumer->input, "evidence\n") < 0 || fflush(consumer->input))
		return -EIO;
	for (index = 0; index < 4; index++) {
		char *ack = sample->acknowledgements[index];
		char state_field[64];
		char *newline;

		if (!fgets(ack, ACK_SIZE, consumer->output))
			return ferror(consumer->output) ? -EIO : -EPIPE;
		newline = strchr(ack, '\n');
		if (newline)
			*newline = '\0';
		if (snprintf(state_field, sizeof(state_field), "\"state\":\"%s\"",
			     states[index]) < 0 || !strstr(ack, state_field) ||
		    !strstr(ack, "\"pass\":true") ||
		    !strstr(ack, "\"error\":0"))
			return -EINVAL;
		sample->acknowledgement_count++;
	}
	return 0;
}

static int timed_generation(struct namei_ext_harness_policy *policy,
			    uint64_t cgroup_id, uint32_t generation,
			    uint64_t *elapsed)
{
	uint64_t start;
	uint64_t end;
	int ret = monotonic_ns(&start);

	if (!ret)
		ret = set_generation(policy, cgroup_id, generation, true);
	if (!ret)
		ret = monotonic_ns(&end);
	if (!ret)
		*elapsed = end - start;
	return ret;
}

static bool metadata_preserved(const struct stat *initial,
			       const struct stat *current)
{
	return initial->st_dev == current->st_dev &&
		initial->st_ino == current->st_ino &&
		initial->st_mode == current->st_mode &&
		initial->st_uid == current->st_uid &&
		initial->st_gid == current->st_gid &&
		initial->st_size == current->st_size &&
		initial->st_mtim.tv_sec == current->st_mtim.tv_sec &&
		initial->st_mtim.tv_nsec == current->st_mtim.tv_nsec &&
		initial->st_ctim.tv_sec == current->st_ctim.tv_sec &&
		initial->st_ctim.tv_nsec == current->st_ctim.tv_nsec;
}

static int read_and_stat(const char *path, const char *expected, mode_t mode,
			 uid_t uid, gid_t gid, struct stat *metadata,
			 char *observed, size_t observed_size)
{
	char bytes[128];
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t length;

	if (fd < 0)
		return -errno;
	length = read(fd, bytes, sizeof(bytes) - 1);
	if (length < 0) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	bytes[length] = '\0';
	if (observed) {
		if ((size_t)length >= observed_size) {
			close(fd);
			return -EOVERFLOW;
		}
		memcpy(observed, bytes, (size_t)length + 1);
	}
	if (fstat(fd, metadata)) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	if (close(fd))
		return -errno;
	if (strcmp(bytes, expected) || (metadata->st_mode & 0777) != mode ||
	    metadata->st_uid != uid || metadata->st_gid != gid)
		return -EINVAL;
	return 0;
}

static int emit_json_string(FILE *output, const char *value)
{
	const unsigned char *cursor = (const unsigned char *)value;

	if (fputc('"', output) == EOF)
		return -EIO;
	while (*cursor) {
		int ret;

		switch (*cursor) {
		case '\\':
			ret = fputs("\\\\", output);
			break;
		case '"':
			ret = fputs("\\\"", output);
			break;
		case '\n':
			ret = fputs("\\n", output);
			break;
		case '\r':
			ret = fputs("\\r", output);
			break;
		case '\t':
			ret = fputs("\\t", output);
			break;
		default:
			if (*cursor < 0x20)
				ret = fprintf(output, "\\u%04x", *cursor);
			else
				ret = fputc(*cursor, output);
		}
		if (ret < 0)
			return -EIO;
		cursor++;
	}
	return fputc('"', output) == EOF ? -EIO : 0;
}

static int emit_identity_evidence(FILE *output, const struct sample *sample,
				  const char *state,
				  const struct entry *entry, bool present,
				  const struct stat *expected,
				  const struct stat *visible, int error,
				  bool pass)
{
	if (fprintf(output,
		    "{\"event\":\"kubernetes-configmap-quantitative-identity\","
		    "\"mechanism\":\"namei_ext\",\"boot\":%u,"
		    "\"pair\":%u,\"width\":%u,\"state\":\"%s\","
		    "\"path\":\"%s\",\"expected_present\":%s,"
		    "\"expected_dev\":%llu,\"expected_ino\":%llu,"
		    "\"expected_mode\":%u,\"expected_uid\":%u,"
		    "\"expected_gid\":%u,\"expected_size\":%lld,"
		    "\"expected_regular\":%s,"
		    "\"visible_dev\":%llu,\"visible_ino\":%llu,"
		    "\"visible_mode\":%u,\"visible_uid\":%u,"
		    "\"visible_gid\":%u,\"visible_size\":%lld,"
		    "\"visible_regular\":%s,"
		    "\"v0_dev\":%llu,\"v0_ino\":%llu,"
		    "\"v1_dev\":%llu,\"v1_ino\":%llu,"
		    "\"error\":%d,\"pass\":%s}\n",
		    sample->boot, sample->pair, sample->width, state,
		    entry->relative, present ? "true" : "false",
		    (unsigned long long)(expected ? expected->st_dev : 0),
		    (unsigned long long)(expected ? expected->st_ino : 0),
		    expected ? expected->st_mode & 0777 : 0,
		    expected ? expected->st_uid : 0,
		    expected ? expected->st_gid : 0,
		    (long long)(expected ? expected->st_size : 0),
		    expected && S_ISREG(expected->st_mode) ? "true" : "false",
		    (unsigned long long)(visible ? visible->st_dev : 0),
		    (unsigned long long)(visible ? visible->st_ino : 0),
		    visible ? visible->st_mode & 0777 : 0,
		    visible ? visible->st_uid : 0,
		    visible ? visible->st_gid : 0,
		    (long long)(visible ? visible->st_size : 0),
		    visible && S_ISREG(visible->st_mode) ? "true" : "false",
		    (unsigned long long)(entry->v0_present ?
			entry->v0_initial.st_dev : 0),
		    (unsigned long long)(entry->v0_present ?
			entry->v0_initial.st_ino : 0),
		    (unsigned long long)(entry->v1_present ?
			entry->v1_initial.st_dev : 0),
		    (unsigned long long)(entry->v1_present ?
			entry->v1_initial.st_ino : 0),
		    error < 0 ? -error : error, pass ? "true" : "false") < 0)
		return -EIO;
	return 0;
}

static int validate_selected_generation(struct sample *sample, bool second,
					const char *state, FILE *evidence)
{
	char path[PATH_MAX];
	unsigned int index;

	for (index = 0; index < sample->width; index++) {
		struct entry *entry = &sample->entries[index];
		const struct stat *expected_metadata = second ?
			&entry->v1_initial : &entry->v0_initial;
		const char *expected_data = second ? entry->v1_data : entry->v0_data;
		mode_t expected_mode = second ? entry->v1_mode : entry->v0_mode;
		bool present = second ? entry->v1_present : entry->v0_present;
		struct stat visible;
		int ret;

		if (join(path, sizeof(path), sample->logical, entry->relative))
			return -ENAMETOOLONG;
		if (!present) {
			int actual_error;
			bool pass;

			if (!stat(path, &visible))
				actual_error = 0;
			else
				actual_error = -errno;
			pass = actual_error == -ENOENT;
			ret = emit_identity_evidence(evidence, sample, state, entry,
						     false, NULL, NULL,
						     actual_error, pass);
			if (ret)
				return ret;
			if (!pass)
				return -EINVAL;
			sample->managed_hidden_checks++;
			continue;
		}
		ret = read_and_stat(path, expected_data, expected_mode, sample->uid,
				    sample->gid, &visible, NULL, 0);
		if (ret) {
			emit_identity_evidence(evidence, sample, state, entry, true,
					       expected_metadata, NULL, ret, false);
			return ret;
		}
		bool pass = visible.st_dev == expected_metadata->st_dev &&
			visible.st_ino == expected_metadata->st_ino;

		ret = emit_identity_evidence(evidence, sample, state, entry, true,
					     expected_metadata, &visible, 0, pass);
		if (ret)
			return ret;
		if (!pass)
			return -EXDEV;
		sample->managed_identity_checks++;
	}
	return 0;
}

static int capture_directory_entries(const char *path, char names[][32],
				     unsigned int capacity,
				     unsigned int *count)
{
	DIR *directory = opendir(path);
	struct dirent *entry;

	if (!directory)
		return -errno;
	*count = 0;
	errno = 0;
	while ((entry = readdir(directory))) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		if (*count >= capacity || strlen(entry->d_name) >= sizeof(names[0])) {
			closedir(directory);
			return -EOVERFLOW;
		}
		strcpy(names[*count], entry->d_name);
		(*count)++;
	}
	if (errno) {
		int ret = -errno;

		closedir(directory);
		return ret;
	}
	if (closedir(directory))
		return -errno;
	return 0;
}

static int emit_name_array(FILE *output, char names[][32], unsigned int count)
{
	unsigned int index;

	if (fputc('[', output) == EOF)
		return -EIO;
	for (index = 0; index < count; index++) {
		if (index && fputc(',', output) == EOF)
			return -EIO;
		if (emit_json_string(output, names[index]))
			return -EIO;
	}
	return fputc(']', output) == EOF ? -EIO : 0;
}

static int emit_unmanaged_directory_evidence(
	FILE *output, const struct sample *sample, char root_names[][32],
	unsigned int root_count, char config_names[][32],
	unsigned int config_count, char tls_names[][32], unsigned int tls_count,
	bool pass)
{
	if (fprintf(output,
		    "{\"event\":\"kubernetes-configmap-quantitative-unmanaged-directory\","
		    "\"mechanism\":\"namei_ext\",\"boot\":%u,\"pair\":%u,"
		    "\"width\":%u,\"root_entries\":",
		    sample->boot, sample->pair, sample->width) < 0 ||
	    emit_name_array(output, root_names, root_count) ||
	    fputs(",\"config_entries\":", output) < 0 ||
	    emit_name_array(output, config_names, config_count) ||
	    fputs(",\"tls_entries\":", output) < 0 ||
	    emit_name_array(output, tls_names, tls_count) ||
	    fprintf(output, ",\"pass\":%s}\n", pass ? "true" : "false") < 0)
		return -EIO;
	return 0;
}

static int validate_unmanaged(struct sample *sample, FILE *evidence)
{
	char path[PATH_MAX];
	char root_names[MAX_WIDTH][32];
	char config_names[MAX_WIDTH][32];
	char tls_names[MAX_WIDTH][32];
	unsigned int index;
	unsigned int root_entries;
	unsigned int config_entries;
	unsigned int tls_entries;
	int ret;

	for (index = 0; index < sample->width; index++) {
		struct entry *entry = &sample->entries[index];
		struct stat metadata;

		if (join(path, sizeof(path), sample->logical, entry->relative))
			return -ENAMETOOLONG;
		if (stat(path, &metadata)) {
			emit_identity_evidence(evidence, sample, "unmanaged", entry,
					       true, NULL, NULL, -errno, false);
			return -errno;
		}
		bool pass = S_ISREG(metadata.st_mode) && metadata.st_size == 0 &&
			metadata.st_uid == sample->uid &&
			metadata.st_gid == sample->gid &&
			!(entry->v0_present &&
			  metadata.st_dev == entry->v0_initial.st_dev &&
			  metadata.st_ino == entry->v0_initial.st_ino) &&
			!(entry->v1_present &&
			  metadata.st_dev == entry->v1_initial.st_dev &&
			  metadata.st_ino == entry->v1_initial.st_ino);

		ret = emit_identity_evidence(evidence, sample, "unmanaged", entry,
					     true, NULL, &metadata, 0, pass);
		if (ret)
			return ret;
		if (!pass)
			return -EINVAL;
		if (!S_ISREG(metadata.st_mode) || metadata.st_size != 0 ||
		    metadata.st_uid != sample->uid || metadata.st_gid != sample->gid)
			return -EINVAL;
		sample->unmanaged_checks++;
	}
	ret = capture_directory_entries(sample->logical, root_names, MAX_WIDTH,
					&root_entries);
	if (!ret && join(path, sizeof(path), sample->logical, "config"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = capture_directory_entries(path, config_names, MAX_WIDTH,
						&config_entries);
	if (!ret && join(path, sizeof(path), sample->logical, "tls"))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = capture_directory_entries(path, tls_names, MAX_WIDTH,
						&tls_entries);
	if (!ret && (root_entries != sample->width || config_entries != 1 ||
		     tls_entries != 1))
		ret = -EINVAL;
	if (!ret)
		ret = emit_unmanaged_directory_evidence(
			evidence, sample, root_names, root_entries, config_names,
			config_entries, tls_names, tls_entries, true);
	if (!ret)
		sample->unmanaged_scope_pass = true;
	return ret;
}

static int validate_managed_replay(struct sample *sample,
				   struct namei_ext_harness_policy *policy,
				   uint64_t cgroup_id, FILE *evidence)
{
	static const uint32_t generations[] = { 0, 1, 1, 0 };
	static const char *const states[] = {
		"initial-replay", "update-replay", "no-op-replay",
		"rollback-replay",
	};
	unsigned int index;

	for (index = 0; index < sizeof(generations) / sizeof(generations[0]);
	     index++) {
		int ret = set_generation(policy, cgroup_id, generations[index], true);

		if (!ret)
			ret = validate_selected_generation(sample, generations[index],
						   states[index], evidence);
		if (ret)
			return ret;
	}
	sample->rollback_original_v0 = true;
	return 0;
}

static int emit_lower_evidence(FILE *output, const struct sample *sample,
			       const struct entry *entry,
			       unsigned int generation,
			       const struct stat *initial,
			       const struct stat *current, const char *expected_bytes,
			       const char *current_bytes, int error,
			       bool pass)
{
	if (fprintf(output,
		    "{\"event\":\"kubernetes-configmap-quantitative-lower\","
		    "\"mechanism\":\"namei_ext\",\"boot\":%u,"
		    "\"pair\":%u,\"width\":%u,\"generation\":%u,"
		    "\"path\":\"%s\",\"expected_bytes\":",
		    sample->boot, sample->pair, sample->width, generation,
		    entry->relative) < 0 ||
	    emit_json_string(output, expected_bytes) ||
	    fputs(",\"current_bytes\":", output) < 0 ||
	    emit_json_string(output, current_bytes ? current_bytes : "") ||
	    fprintf(output,
		    ",\"initial_dev\":%llu,"
		    "\"initial_ino\":%llu,\"current_dev\":%llu,"
		    "\"current_ino\":%llu,\"initial_mode\":%u,"
		    "\"current_mode\":%u,\"initial_uid\":%u,"
		    "\"current_uid\":%u,\"initial_gid\":%u,"
		    "\"current_gid\":%u,\"initial_size\":%lld,"
		    "\"current_size\":%lld,\"initial_regular\":%s,"
		    "\"current_regular\":%s,\"initial_mtime_sec\":%lld,"
		    "\"initial_mtime_nsec\":%ld,\"current_mtime_sec\":%lld,"
		    "\"current_mtime_nsec\":%ld,\"initial_ctime_sec\":%lld,"
		    "\"initial_ctime_nsec\":%ld,\"current_ctime_sec\":%lld,"
		    "\"current_ctime_nsec\":%ld,\"error\":%d,\"pass\":%s}\n",
		    (unsigned long long)initial->st_dev,
		    (unsigned long long)initial->st_ino,
		    (unsigned long long)(current ? current->st_dev : 0),
		    (unsigned long long)(current ? current->st_ino : 0),
		    initial->st_mode, current ? current->st_mode : 0,
		    initial->st_uid, current ? current->st_uid : 0,
		    initial->st_gid, current ? current->st_gid : 0,
		    (long long)initial->st_size,
		    (long long)(current ? current->st_size : 0),
		    S_ISREG(initial->st_mode) ? "true" : "false",
		    current && S_ISREG(current->st_mode) ? "true" : "false",
		    (long long)initial->st_mtim.tv_sec, initial->st_mtim.tv_nsec,
		    (long long)(current ? current->st_mtim.tv_sec : 0),
		    current ? current->st_mtim.tv_nsec : 0,
		    (long long)initial->st_ctim.tv_sec, initial->st_ctim.tv_nsec,
		    (long long)(current ? current->st_ctim.tv_sec : 0),
		    current ? current->st_ctim.tv_nsec : 0,
		    error < 0 ? -error : error, pass ? "true" : "false") < 0)
		return -EIO;
	return 0;
}

static int validate_lower(struct sample *sample, FILE *evidence)
{
	char path[PATH_MAX];
	unsigned int index;

	for (index = 0; index < sample->width; index++) {
		struct entry *entry = &sample->entries[index];
		const char *roots[] = { sample->v0, sample->v1 };
		const char *expected[] = { entry->v0_data, entry->v1_data };
		mode_t modes[] = { entry->v0_mode, entry->v1_mode };
		bool present[] = { entry->v0_present, entry->v1_present };
		const struct stat *initial[] = {
			&entry->v0_initial, &entry->v1_initial,
		};
		unsigned int generation;

		for (generation = 0; generation < 2; generation++) {
			char observed[128] = {};
			struct stat metadata;
			int ret;

			if (!present[generation])
				continue;
			if (join(path, sizeof(path), roots[generation], entry->relative))
				return -ENAMETOOLONG;
			ret = read_and_stat(path, expected[generation],
					    modes[generation], sample->uid,
					    sample->gid, &metadata, observed,
					    sizeof(observed));
			if (ret) {
				emit_lower_evidence(evidence, sample, entry, generation,
						    initial[generation], NULL,
						    expected[generation], observed, ret,
						    false);
				return ret;
			}
			bool pass = metadata_preserved(initial[generation], &metadata);

			ret = emit_lower_evidence(evidence, sample, entry, generation,
						  initial[generation], &metadata,
						  expected[generation], observed, 0,
						  pass);
			if (ret)
				return ret;
			if (!pass)
				return -ESTALE;
			sample->observed_lower_files++;
			sample->observed_lower_bytes +=
				(uint64_t)metadata.st_size;
			sample->lower_preservation_checks++;
		}
	}
	return sample->observed_lower_files == sample->lower_files &&
		sample->observed_lower_bytes == sample->lower_bytes ? 0 : -EINVAL;
}

static int emit_observation(const struct sample *sample, int error,
			    bool cleanup_pass)
{
	FILE *output = fopen(sample->output_path, "a");
	unsigned int index;

	if (!output)
		return -errno;
	fprintf(output,
		"{\"event\":\"kubernetes-configmap-quantitative-lifecycle\","
		"\"mechanism\":\"namei_ext\",\"boot\":%u,\"pair\":%u,"
		"\"order\":%u,\"width\":%u,\"present_per_state\":%u,"
		"\"changed_union_paths\":4,\"active_total_ns\":%llu,"
		"\"wall_span_ns\":%llu,\"publication_only_ns\":%llu,"
		"\"consumer_only_ns\":%llu,\"attach_ns\":%llu,"
		"\"phases\":{\"setup_ns\":%llu,"
		"\"initial_publish_ns\":%llu,\"initial_consumer_ns\":%llu,"
		"\"update_publish_ns\":%llu,\"update_consumer_ns\":%llu,"
		"\"no_op_publish_ns\":%llu,\"no_op_consumer_ns\":%llu,"
		"\"rollback_publish_ns\":%llu,"
		"\"rollback_consumer_ns\":%llu},"
		"\"setup_phases\":{\"object_preparation_ns\":%llu,"
		"\"target_registration_ns\":%llu,\"map_population_ns\":%llu,"
		"\"consumer_cgroup_move_ns\":%llu},\"consumer\":[",
		sample->boot, sample->pair, sample->order, sample->width,
		sample->width - 1, (unsigned long long)sample->active_total_ns,
		(unsigned long long)sample->wall_span_ns,
		(unsigned long long)sample->publication_only_ns,
		(unsigned long long)sample->consumer_only_ns,
		(unsigned long long)sample->attach_ns,
		(unsigned long long)sample->phases.setup_ns,
		(unsigned long long)sample->phases.initial_publish_ns,
		(unsigned long long)sample->phases.initial_consumer_ns,
		(unsigned long long)sample->phases.update_publish_ns,
		(unsigned long long)sample->phases.update_consumer_ns,
		(unsigned long long)sample->phases.no_op_publish_ns,
		(unsigned long long)sample->phases.no_op_consumer_ns,
		(unsigned long long)sample->phases.rollback_publish_ns,
		(unsigned long long)sample->phases.rollback_consumer_ns,
		(unsigned long long)sample->setup_phases.object_preparation_ns,
		(unsigned long long)sample->setup_phases.target_registration_ns,
		(unsigned long long)sample->setup_phases.map_population_ns,
		(unsigned long long)sample->setup_phases.consumer_cgroup_move_ns);
	for (index = 0; index < sample->acknowledgement_count; index++)
		fprintf(output, "%s%s", index ? "," : "",
			sample->acknowledgements[index]);
	fprintf(output,
		"],\"runtime_uid\":%u,\"runtime_gid\":%u,"
		"\"lower_files\":%u,\"lower_bytes\":%llu,"
		"\"observed_lower_files\":%u,"
		"\"observed_lower_bytes\":%llu,"
		"\"logical_files\":%u,\"registered_targets\":%u,"
		"\"managed_identity_checks\":%u,"
		"\"managed_hidden_checks\":%u,"
		"\"lower_preservation_checks\":%u,"
		"\"unmanaged_checks\":%u,"
		"\"rollback_original_v0\":%s,"
		"\"unmanaged_scope_pass\":%s,"
		"\"consumer_exit_status\":%d,"
		"\"cleanup_generation_removed\":%s,"
		"\"cleanup_view_maps_empty\":%s,"
		"\"cleanup_policy_destroyed\":%s,"
		"\"cleanup_targets_cleared\":%s,"
		"\"cleanup_cgroup_removed\":%s,"
		"\"cleanup_logical_removed\":%s,"
		"\"cleanup_lower_removed\":%s,"
		"\"cleanup_consumer_error\":%d,"
		"\"cleanup_generation_error\":%d,"
		"\"cleanup_map_error\":%d,"
		"\"cleanup_v0_map_count\":%zu,"
		"\"cleanup_v1_map_count\":%zu,"
		"\"cleanup_policy_error\":%d,"
		"\"cleanup_targets_error\":%d,"
		"\"cleanup_cgroup_error\":%d,"
		"\"cleanup_logical_lookup_error\":%d,"
		"\"cleanup_lower_lookup_error\":%d,\"counters\":{"
		"\"total\":%llu,\"lookup\":%llu,\"readdir\":%llu,"
		"\"select\":%llu,\"pass\":%llu,\"hide\":%llu},"
		"\"error\":%d,\"cleanup_pass\":%s,\"pass\":%s}\n",
		sample->uid, sample->gid, sample->lower_files,
		(unsigned long long)sample->lower_bytes,
		sample->observed_lower_files,
		(unsigned long long)sample->observed_lower_bytes,
		sample->logical_files, sample->registered_targets,
		sample->managed_identity_checks,
		sample->managed_hidden_checks,
		sample->lower_preservation_checks, sample->unmanaged_checks,
		sample->rollback_original_v0 ? "true" : "false",
		sample->unmanaged_scope_pass ? "true" : "false",
		sample->consumer_exit_status,
		sample->generation_removed ? "true" : "false",
		sample->view_maps_empty ? "true" : "false",
		sample->policy_destroyed ? "true" : "false",
		sample->targets_cleared ? "true" : "false",
		sample->cgroup_removed ? "true" : "false",
		sample->logical_removed ? "true" : "false",
		sample->lower_removed ? "true" : "false",
		sample->cleanup_consumer_error,
		sample->cleanup_generation_error,
		sample->cleanup_map_error,
		sample->cleanup_v0_map_count,
		sample->cleanup_v1_map_count,
		sample->cleanup_policy_error,
		sample->cleanup_targets_error,
		sample->cleanup_cgroup_error,
		sample->cleanup_logical_lookup_error,
		sample->cleanup_lower_lookup_error,
		(unsigned long long)sample->counters[COUNTER_TOTAL],
		(unsigned long long)sample->counters[COUNTER_LOOKUP],
		(unsigned long long)sample->counters[COUNTER_READDIR],
		(unsigned long long)sample->counters[COUNTER_SELECT],
		(unsigned long long)sample->counters[COUNTER_PASS],
		(unsigned long long)sample->counters[COUNTER_HIDE],
		error < 0 ? -error : error, cleanup_pass ? "true" : "false",
		!error && cleanup_pass && sample->acknowledgement_count == 4 ?
			"true" : "false");
	if (fclose(output))
		return -errno;
	return 0;
}

static int run_sample(struct sample *sample)
{
	struct namei_ext_harness_policy policy = {};
	struct consumer_process consumer = {};
	FILE *evidence = NULL;
	uint64_t attach_start;
	uint64_t attach_end;
	uint64_t setup_start;
	uint64_t setup_boundary;
	uint64_t setup_now;
	uint64_t wall_start;
	uint64_t wall_end;
	uint64_t cgroup_id = 0;
	size_t map_count = 0;
	bool consumer_started = false;
	bool policy_attached = false;
	bool cgroup_created = false;
	bool targets_registered = false;
	bool maps_configured = false;
	bool generation_set = false;
	bool parent_managed = false;
	bool cleanup_pass = true;
	int ret;
	int cleanup_ret;
	unsigned int index;

	sample->consumer_exit_status = -1;
	ret = initialize_entries(sample);
	if (!ret)
		ret = initialize_paths(sample);
	if (!ret)
		ret = start_consumer(sample, &consumer);
	if (ret)
		goto out;
	consumer_started = true;
	ret = monotonic_ns(&attach_start);
	if (!ret)
		ret = namei_ext_policy_load_attach(sample->policy_path,
						 sample->cgroup_root, &policy);
	if (!ret)
		ret = monotonic_ns(&attach_end);
	if (ret)
		goto out;
	policy_attached = true;
	sample->attach_ns = attach_end - attach_start;
	ret = monotonic_ns(&wall_start);
	if (!ret)
		ret = monotonic_ns(&setup_start);
	if (!ret)
		ret = setup_directories(sample);
	if (ret)
		goto out;
	cgroup_created = true;
	ret = setup_files(sample);
	if (!ret)
		ret = namei_ext_cgroup_id(sample->cgroup, &cgroup_id);
	if (!ret)
		ret = monotonic_ns(&setup_boundary);
	if (!ret)
		sample->setup_phases.object_preparation_ns =
			setup_boundary - setup_start;
	if (!ret) {
		targets_registered = true;
		ret = register_targets(sample);
	}
	if (!ret)
		ret = monotonic_ns(&setup_now);
	if (!ret) {
		sample->setup_phases.target_registration_ns =
			setup_now - setup_boundary;
		setup_boundary = setup_now;
	}
	if (!ret) {
		maps_configured = true;
		ret = configure_maps(sample, &policy, cgroup_id);
	}
	if (!ret)
		ret = monotonic_ns(&setup_now);
	if (!ret) {
		sample->setup_phases.map_population_ns =
			setup_now - setup_boundary;
		setup_boundary = setup_now;
	}
	if (!ret)
		ret = move_pid_to_cgroup(sample->cgroup, consumer.pid);
	if (!ret)
		ret = monotonic_ns(&setup_now);
	if (!ret) {
		sample->setup_phases.consumer_cgroup_move_ns =
			setup_now - setup_boundary;
		sample->phases.setup_ns = setup_now - setup_start;
	}
	if (!ret)
		ret = timed_generation(&policy, cgroup_id, 0,
				       &sample->phases.initial_publish_ns);
	if (!ret)
		generation_set = true;
	if (!ret)
		ret = send_state(sample, &consumer, "initial",
				 &sample->phases.initial_consumer_ns);
	if (!ret)
		ret = timed_generation(&policy, cgroup_id, 1,
				       &sample->phases.update_publish_ns);
	if (!ret)
		ret = send_state(sample, &consumer, "update",
				 &sample->phases.update_consumer_ns);
	if (!ret)
		ret = timed_generation(&policy, cgroup_id, 1,
				       &sample->phases.no_op_publish_ns);
	if (!ret)
		ret = send_state(sample, &consumer, "no-op",
				 &sample->phases.no_op_consumer_ns);
	if (!ret)
		ret = timed_generation(&policy, cgroup_id, 0,
				       &sample->phases.rollback_publish_ns);
	if (!ret)
		ret = send_state(sample, &consumer, "rollback",
				 &sample->phases.rollback_consumer_ns);
	if (!ret)
		ret = monotonic_ns(&wall_end);
	if (!ret)
		sample->wall_span_ns = wall_end - wall_start;
	if (!ret) {
		sample->publication_only_ns =
			sample->phases.initial_publish_ns +
			sample->phases.update_publish_ns +
			sample->phases.no_op_publish_ns +
			sample->phases.rollback_publish_ns;
		sample->consumer_only_ns =
			sample->phases.initial_consumer_ns +
			sample->phases.update_consumer_ns +
			sample->phases.no_op_consumer_ns +
			sample->phases.rollback_consumer_ns;
		sample->active_total_ns = sample->phases.setup_ns +
			sample->publication_only_ns + sample->consumer_only_ns;
	}
	if (!ret)
		ret = collect_evidence(sample, &consumer);
	if (!ret) {
		evidence = fopen(sample->output_path, "a");
		if (!evidence)
			ret = -errno;
	}
	if (!ret)
		ret = validate_lower(sample, evidence);
	if (!ret) {
		ret = namei_ext_move_self_to_cgroup(sample->cgroup);
		if (!ret)
			parent_managed = true;
	}
	if (!ret)
		ret = validate_managed_replay(sample, &policy, cgroup_id, evidence);
	if (parent_managed) {
		cleanup_ret = namei_ext_move_self_to_cgroup(sample->cgroup_root);
		if (cleanup_ret && !ret)
			ret = cleanup_ret;
		if (!cleanup_ret)
			parent_managed = false;
	}
	if (!ret)
		ret = validate_unmanaged(sample, evidence);
	if (evidence) {
		cleanup_ret = fclose(evidence);
		evidence = NULL;
		if (cleanup_ret && !ret)
			ret = -errno;
	}
	if (!ret) {
		for (index = 0; index < COUNTER_MAX; index++) {
			ret = namei_ext_policy_counter(
				&policy, "configmap_publication_counters", index,
				&sample->counters[index]);
			if (ret || !sample->counters[index]) {
				if (!ret)
					ret = -ERANGE;
				break;
			}
		}
	}

out:
	if (evidence) {
		cleanup_ret = fclose(evidence);
		evidence = NULL;
		if (cleanup_ret) {
			cleanup_pass = false;
			if (!ret)
				ret = -errno;
		}
	}
	if (consumer_started) {
		cleanup_ret = stop_consumer(&consumer,
					    sample->acknowledgement_count == 4,
					    &sample->consumer_exit_status);
		sample->cleanup_consumer_error = cleanup_ret < 0 ?
			-cleanup_ret : cleanup_ret;
		if (cleanup_ret) {
			cleanup_pass = false;
			if (!ret)
				ret = cleanup_ret;
		}
	}
	if (parent_managed) {
		cleanup_ret = namei_ext_move_self_to_cgroup(sample->cgroup_root);
		if (cleanup_ret) {
			cleanup_pass = false;
			if (!ret)
				ret = cleanup_ret;
		} else {
			parent_managed = false;
		}
	}
	if (policy_attached && generation_set) {
		cleanup_ret = set_generation(&policy, cgroup_id, 0, false);
		sample->cleanup_generation_error = cleanup_ret < 0 ?
			-cleanup_ret : cleanup_ret;
		if (!cleanup_ret)
			sample->generation_removed = true;
		if (cleanup_ret) {
			cleanup_pass = false;
			if (!ret)
				ret = cleanup_ret;
		}
	}
	if (policy_attached && maps_configured) {
		cleanup_ret = delete_maps(sample, &policy, cgroup_id);
		if (!cleanup_ret)
			cleanup_ret = namei_ext_component_map_count(
				&policy, "configmap_publication_v0_views", &map_count);
		if (!cleanup_ret)
			sample->cleanup_v0_map_count = map_count;
		if (!cleanup_ret && map_count)
			cleanup_ret = -EBUSY;
		if (!cleanup_ret)
			cleanup_ret = namei_ext_component_map_count(
				&policy, "configmap_publication_v1_views", &map_count);
		if (!cleanup_ret)
			sample->cleanup_v1_map_count = map_count;
		if (!cleanup_ret && map_count)
			cleanup_ret = -EBUSY;
		if (!cleanup_ret)
			sample->view_maps_empty = true;
		sample->cleanup_map_error = cleanup_ret < 0 ?
			-cleanup_ret : cleanup_ret;
		if (cleanup_ret) {
			cleanup_pass = false;
			if (!ret)
				ret = cleanup_ret;
		}
	}
	if (policy_attached) {
		cleanup_ret = namei_ext_policy_destroy(&policy);
		sample->cleanup_policy_error = cleanup_ret < 0 ?
			-cleanup_ret : cleanup_ret;
		if (!cleanup_ret)
			sample->policy_destroyed = true;
		if (cleanup_ret) {
			cleanup_pass = false;
			if (!ret)
				ret = cleanup_ret;
		}
	}
	if (targets_registered) {
		cleanup_ret = namei_ext_clear_targets(sample->cgroup);
		sample->cleanup_targets_error = cleanup_ret < 0 ?
			-cleanup_ret : cleanup_ret;
		if (!cleanup_ret)
			sample->targets_cleared = true;
		if (cleanup_ret) {
			cleanup_pass = false;
			if (!ret)
				ret = cleanup_ret;
		}
	}
	if (cgroup_created) {
		if (rmdir(sample->cgroup)) {
			sample->cleanup_cgroup_error = errno;
			cleanup_pass = false;
			if (!ret)
				ret = -errno;
		} else {
			sample->cleanup_cgroup_error = 0;
			sample->cgroup_removed = true;
		}
	}
	namei_ext_remove_tree(sample->logical);
	namei_ext_remove_tree(sample->lower);
	if (access(sample->logical, F_OK))
		sample->cleanup_logical_lookup_error = errno;
	if (sample->cleanup_logical_lookup_error == ENOENT)
		sample->logical_removed = true;
	if (access(sample->lower, F_OK))
		sample->cleanup_lower_lookup_error = errno;
	if (sample->cleanup_lower_lookup_error == ENOENT)
		sample->lower_removed = true;
	if (!sample->logical_removed || !sample->lower_removed) {
		cleanup_pass = false;
		if (!ret)
			ret = -EBUSY;
	}
	cleanup_ret = emit_observation(sample, ret, cleanup_pass);
	if (cleanup_ret && !ret)
		ret = cleanup_ret;
	return ret;
}

static int parse_unsigned(const char *text, unsigned int minimum,
			  unsigned int maximum, unsigned int *value)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno || !end || *end || parsed < minimum || parsed > maximum)
		return -EINVAL;
	*value = (unsigned int)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	struct sample sample = {};
	struct stat parent_metadata;
	int ret;

	if (argc != 10) {
		fprintf(stderr,
			"usage: %s POLICY CONSUMER OUTPUT PARENT CGROUP_ROOT WIDTH BOOT PAIR ORDER\n",
			argv[0]);
		return 2;
	}
	sample.policy_path = argv[1];
	sample.consumer_path = argv[2];
	sample.output_path = argv[3];
	sample.parent = argv[4];
	sample.cgroup_root = argv[5];
	ret = parse_unsigned(argv[6], FIXED_PATHS, MAX_WIDTH, &sample.width);
	if (!ret)
		ret = parse_unsigned(argv[7], 1, UINT_MAX, &sample.boot);
	if (!ret)
		ret = parse_unsigned(argv[8], 1, UINT_MAX, &sample.pair);
	if (!ret)
		ret = parse_unsigned(argv[9], 1, 2, &sample.order);
	if (ret)
		return 2;
	if (stat(sample.parent, &parent_metadata)) {
		perror("stat sample parent");
		return 1;
	}
	sample.uid = parent_metadata.st_uid;
	sample.gid = parent_metadata.st_gid;
	if (!sample.uid || !sample.gid) {
		fprintf(stderr, "sample parent must have a non-root owner\n");
		return 1;
	}
	ret = run_sample(&sample);
	if (ret) {
		fprintf(stderr, "namei_ext quantitative sample failed: %s\n",
			strerror(-ret));
		return 1;
	}
	return 0;
}

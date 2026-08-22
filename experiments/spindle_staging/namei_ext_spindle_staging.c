// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <namei_ext_harness.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FOCAL_OBJECTS 47
#define EXPECTED_LOADER_PROGRESS 44
#define SPINDLE_TARGET_MAX 64
#define SPINDLE_STAGING_HIDE_VALUE 0U
#define CACHE_ROOT "/tmp/namei-ext-spindle-cache"
#define COMM_ROOT "/tmp/namei-ext-spindle-comm"
#define TMP_ROOT "/tmp/namei-ext-spindle-tmp"
#define SOURCE_TIMEOUT_SECONDS 180
#define NAMEI_TIMEOUT_SECONDS 120
#define WITHDRAWN_TIMEOUT_SECONDS 120
#define PROCESS_QUIESCENCE_SECONDS 10

enum spindle_staging_counter {
	SPINDLE_COUNTER_TOTAL = 0,
	SPINDLE_COUNTER_LOOKUP = 1,
	SPINDLE_COUNTER_SELECT = 2,
	SPINDLE_COUNTER_PASS = 3,
	SPINDLE_COUNTER_READDIR = 4,
	SPINDLE_COUNTER_HIDE_LOOKUP = 5,
	SPINDLE_COUNTER_HIDE_READDIR = 6,
};

struct focal_spec {
	const char *name;
	const char *subdir;
};

struct file_snapshot {
	struct stat st;
};

struct focal_mapping {
	const struct focal_spec *spec;
	uint32_t target_id;
	unsigned int seen;
	char source[PATH_MAX];
	char source_parent[PATH_MAX];
	char cache[PATH_MAX];
	struct file_snapshot source_before;
	struct file_snapshot cache_before;
};

struct run_environment {
	uid_t uid;
	gid_t gid;
	char home[PATH_MAX];
	char user[64];
	char home_env[PATH_MAX + 6];
	char user_env[70];
	char logname_env[73];
	char path_env[PATH_MAX + 32];
	char ld_library_path_env[PATH_MAX + 32];
	char tmpdir_env[PATH_MAX + 16];
	char *source_env[13];
	char *namei_env[14];
};

struct process_result {
	int exit_status;
	int runner_errno;
	uint64_t duration_ns;
};

struct identity_wire {
	int error;
	struct stat st;
};

static const struct focal_spec focal_specs[FOCAL_OBJECTS] = {
	{ "libtest10.so", "" },
	{ "libtest11.so", "" },
	{ "libtest12.so", "" },
	{ "libtest13.so", "" },
	{ "libtest14.so", "" },
	{ "libtest15.so", "" },
	{ "libtest16.so", "" },
	{ "libtest17.so", "" },
	{ "libtest18.so", "" },
	{ "libtest19.so", "" },
	{ "libtest20.so", "" },
	{ "libtest50.so", "" },
	{ "libtest100.so", "" },
	{ "libtest500.so", "" },
	{ "libtest1000.so", "" },
	{ "libtest2000.so", "" },
	{ "libtest4000.so", "" },
	{ "libtest6000.so", "" },
	{ "libtest8000.so", "" },
	{ "libtest10000.so", "" },
	{ "libdepA.so", "" },
	{ "libdepB.so", "" },
	{ "libdepC.so", "" },
	{ "libcxxexceptA.so", "" },
	{ "libcxxexceptB.so", "" },
	{ "liboriginlib.so", "origin_dir" },
	{ "liborigintarget.so", "origin_dir/origin_subdir" },
	{ "libtls1.so", "" },
	{ "libtls2.so", "" },
	{ "libtls3.so", "" },
	{ "libtls4.so", "" },
	{ "libtls5.so", "" },
	{ "libtls6.so", "" },
	{ "libtls7.so", "" },
	{ "libtls8.so", "" },
	{ "libtls9.so", "" },
	{ "libtls10.so", "" },
	{ "libtls11.so", "" },
	{ "libtls12.so", "" },
	{ "libtls13.so", "" },
	{ "libtls14.so", "" },
	{ "libtls15.so", "" },
	{ "libtls16.so", "" },
	{ "libtls17.so", "" },
	{ "libtls18.so", "" },
	{ "libtls19.so", "" },
	{ "libtls20.so", "" },
};

static const char *const expected_loader_progress[EXPECTED_LOADER_PROGRESS] = {
	"dlstart libtest10.so\n",
	"dlstart libtest11.so\n",
	"dlstart libtest12.so\n",
	"dlstart libtest13.so\n",
	"dlstart libtest14.so\n",
	"dlstart libtest15.so\n",
	"dlstart libtest16.so\n",
	"dlstart libtest17.so\n",
	"dlstart libtest18.so\n",
	"dlstart libtest19.so\n",
	"dlstart libtest20.so\n",
	"dlstart libtest50.so\n",
	"dlstart libtest100.so\n",
	"dlstart libtest500.so\n",
	"dlstart libtest1000.so\n",
	"dlstart libtest2000.so\n",
	"dlstart libtest4000.so\n",
	"dlstart libtest6000.so\n",
	"dlstart libtest8000.so\n",
	"dlstart libtest10000.so\n",
	"dlstart libdepA.so\n",
	"dlstart libcxxexceptA.so\n",
	"dlstart libnoexist.so\n",
	"dlstart liboriginlib.so\n",
	"dlstart libtls1.so\n",
	"dlstart libtls2.so\n",
	"dlstart libtls3.so\n",
	"dlstart libtls4.so\n",
	"dlstart libtls5.so\n",
	"dlstart libtls6.so\n",
	"dlstart libtls7.so\n",
	"dlstart libtls8.so\n",
	"dlstart libtls9.so\n",
	"dlstart libtls10.so\n",
	"dlstart libtls11.so\n",
	"dlstart libtls12.so\n",
	"dlstart libtls13.so\n",
	"dlstart libtls14.so\n",
	"dlstart libtls15.so\n",
	"dlstart libtls16.so\n",
	"dlstart libtls17.so\n",
	"dlstart libtls18.so\n",
	"dlstart libtls19.so\n",
	"dlstart libtls20.so\n",
};

static uint64_t monotonic_ns(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return 0;
	return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}

static void json_string(FILE *out, const char *value)
{
	const unsigned char *cursor = (const unsigned char *)value;

	fputc('"', out);
	while (*cursor) {
		switch (*cursor) {
		case '"':
			fputs("\\\"", out);
			break;
		case '\\':
			fputs("\\\\", out);
			break;
		case '\b':
			fputs("\\b", out);
			break;
		case '\f':
			fputs("\\f", out);
			break;
		case '\n':
			fputs("\\n", out);
			break;
		case '\r':
			fputs("\\r", out);
			break;
		case '\t':
			fputs("\\t", out);
			break;
		default:
			if (*cursor < 0x20)
				fprintf(out, "\\u%04x", *cursor);
			else
				fputc(*cursor, out);
		}
		cursor++;
	}
	fputc('"', out);
}

static void json_string_array(FILE *out, char *const values[])
{
	fputc('[', out);
	for (size_t index = 0; values[index]; index++) {
		if (index)
			fputc(',', out);
		json_string(out, values[index]);
	}
	fputc(']', out);
}

static void emit_case(FILE *out, const char *name, bool pass, int error,
		      const char *detail)
{
	fputs("{\"event\":\"spindle-staging-case\",\"case\":", out);
	json_string(out, name);
	fprintf(out, ",\"pass\":%s,\"errno\":%d,\"detail\":",
		pass ? "true" : "false", error);
	json_string(out, detail);
	fputs("}\n", out);
	fflush(out);
}

static void emit_condition(FILE *out, const char *condition,
			   const struct process_result *result,
			   bool diagnostic_ok, bool pass, const char *detail)
{
	fputs("{\"event\":\"spindle-staging-condition\",\"condition\":", out);
	json_string(out, condition);
	fprintf(out,
		",\"exit_status\":%d,\"runner_errno\":%d,"
		"\"diagnostic_ok\":%s,\"duration_ns\":%llu,\"pass\":%s,"
		"\"detail\":", result->exit_status, result->runner_errno,
		diagnostic_ok ? "true" : "false",
		(unsigned long long)result->duration_ns,
		pass ? "true" : "false");
	json_string(out, detail);
	fputs("}\n", out);
	fflush(out);
}

static void emit_mapping(FILE *out, const struct focal_mapping *mapping,
			 bool bytes_equal, bool pass, const char *detail)
{
	fputs("{\"event\":\"spindle-staging-mapping\",\"target_id\":", out);
	fprintf(out, "%u,\"name\":", mapping->target_id);
	json_string(out, mapping->spec->name);
	fputs(",\"source\":", out);
	json_string(out, mapping->source);
	fputs(",\"cache\":", out);
	json_string(out, mapping->cache);
	fprintf(out,
		",\"source_dev\":%llu,\"source_ino\":%llu,"
		"\"cache_dev\":%llu,\"cache_ino\":%llu,"
		"\"source_size\":%lld,\"cache_size\":%lld,"
		"\"source_mode\":%u,\"cache_mode\":%u,"
		"\"bytes_equal\":%s",
		(unsigned long long)mapping->source_before.st.st_dev,
		(unsigned long long)mapping->source_before.st.st_ino,
		(unsigned long long)mapping->cache_before.st.st_dev,
		(unsigned long long)mapping->cache_before.st.st_ino,
		(long long)mapping->source_before.st.st_size,
		(long long)mapping->cache_before.st.st_size,
		(unsigned int)mapping->source_before.st.st_mode,
		(unsigned int)mapping->cache_before.st.st_mode,
		bytes_equal ? "true" : "false");
	fprintf(out, ",\"pass\":%s,\"detail\":", pass ? "true" : "false");
	json_string(out, detail);
	fputs("}\n", out);
	fflush(out);
}

static void emit_selection(FILE *out, const struct focal_mapping *mapping,
			   uint64_t before, uint64_t after, bool pass)
{
	uint64_t delta = after >= before ? after - before : 0;

	fputs("{\"event\":\"spindle-staging-selection\",\"target_id\":", out);
	fprintf(out, "%u,\"name\":", mapping->target_id);
	json_string(out, mapping->spec->name);
	fprintf(out,
		",\"hits_before\":%llu,\"hits_after\":%llu,"
		"\"hits_delta\":%llu,\"pass\":%s}\n",
		(unsigned long long)before, (unsigned long long)after,
		(unsigned long long)delta,
		pass ? "true" : "false");
	fflush(out);
}

static int emit_runtime_contract(FILE *out, const char *spindle,
				 const char *test_driver, const char *test_dir,
				 const struct run_environment *environment,
				 char *const source_argv[],
				 char *const namei_argv[])
{
	char spindle_real[PATH_MAX];
	char test_driver_real[PATH_MAX];
	char test_dir_real[PATH_MAX];

	if (!realpath(spindle, spindle_real) ||
	    !realpath(test_driver, test_driver_real) ||
	    !realpath(test_dir, test_dir_real))
		return -errno;
	fputs("{\"event\":\"spindle-staging-runtime\","
	      "\"source_system\":\"LLNL-Spindle\",\"uid\":", out);
	fprintf(out, "%u,\"gid\":%u,\"env_i\":true,\"spindle\":",
		(unsigned int)environment->uid, (unsigned int)environment->gid);
	json_string(out, spindle_real);
	fputs(",\"test_driver\":", out);
	json_string(out, test_driver_real);
	fputs(",\"working_directory\":", out);
	json_string(out, test_dir_real);
	fputs(",\"source_argv\":", out);
	json_string_array(out, source_argv);
	fputs(",\"source_env\":", out);
	json_string_array(out, environment->source_env);
	fputs(",\"namei_argv\":", out);
	json_string_array(out, namei_argv);
	fputs(",\"namei_env\":", out);
	json_string_array(out, environment->namei_env);
	fputs(",\"withdrawn_argv\":", out);
	json_string_array(out, namei_argv);
	fputs(",\"withdrawn_env\":", out);
	json_string_array(out, environment->namei_env);
	fputs(",\"pass\":true}\n", out);
	fflush(out);
	return 0;
}

static void emit_counter_window(FILE *out, uint64_t select_before,
				uint64_t select_after, uint64_t hit_sum,
				bool pass)
{
	uint64_t select_delta = select_after >= select_before ?
		select_after - select_before : 0;

	fprintf(out,
		"{\"event\":\"spindle-staging-counter-window\","
		"\"select_before\":%llu,\"select_after\":%llu,"
		"\"select_delta\":%llu,\"per_target_delta_sum\":%llu,"
		"\"pass\":%s}\n",
		(unsigned long long)select_before,
		(unsigned long long)select_after,
		(unsigned long long)select_delta,
		(unsigned long long)hit_sum, pass ? "true" : "false");
	fflush(out);
}

static int write_all(int fd, const void *buffer, size_t length)
{
	const char *cursor = buffer;

	while (length) {
		ssize_t written = write(fd, cursor, length);

		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!written)
			return -EIO;
		cursor += written;
		length -= (size_t)written;
	}
	return 0;
}

static int read_all(int fd, void *buffer, size_t length)
{
	char *cursor = buffer;

	while (length) {
		ssize_t count = read(fd, cursor, length);

		if (count < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!count)
			return -EPIPE;
		cursor += count;
		length -= (size_t)count;
	}
	return 0;
}

static int drop_privileges(uid_t uid, gid_t gid)
{
	if (setgroups(0, NULL) || setresgid(gid, gid, gid) ||
	    setresuid(uid, uid, uid))
		return -errno;
	return 0;
}

static int wait_child(pid_t pid, unsigned int timeout_seconds, int *status_out)
{
	uint64_t deadline = monotonic_ns() +
		(uint64_t)timeout_seconds * 1000000000ULL;
	int status = 0;

	for (;;) {
		pid_t waited = waitpid(pid, &status, WNOHANG);

		if (waited == pid)
			break;
		if (waited < 0 && errno != EINTR)
			return -errno;
		if (monotonic_ns() >= deadline) {
			kill(-pid, SIGKILL);
			kill(pid, SIGKILL);
			while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
				;
			*status_out = status;
			return -ETIMEDOUT;
		}
		usleep(10000);
	}
	*status_out = status;
	return 0;
}

static int run_process(const char *working_directory,
		       const char *cgroup_path,
		       const struct run_environment *environment,
		       char *const argv[], char *const envp[],
		       const char *stdout_path, const char *stderr_path,
		       unsigned int timeout_seconds,
		       struct process_result *result)
{
	uint64_t started = monotonic_ns();
	pid_t pid = fork();
	int status = 0;
	int ret;

	if (pid < 0)
		return -errno;
	if (!pid) {
		int stdout_fd;
		int stderr_fd;

		if (setpgid(0, 0))
			_exit(120);
		if (cgroup_path &&
		    namei_ext_move_self_to_cgroup(cgroup_path))
			_exit(121);
		stdout_fd = open(stdout_path,
				 O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
		stderr_fd = open(stderr_path,
				 O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
		if (stdout_fd < 0 || stderr_fd < 0 ||
		    dup2(stdout_fd, STDOUT_FILENO) < 0 ||
		    dup2(stderr_fd, STDERR_FILENO) < 0)
			_exit(122);
		close(stdout_fd);
		close(stderr_fd);
		if (chdir(working_directory))
			_exit(123);
		if (drop_privileges(environment->uid, environment->gid))
			_exit(124);
		execve(argv[0], argv, envp);
		_exit(125);
	}
	if (setpgid(pid, pid) && errno != EACCES && errno != ESRCH) {
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
		return -errno;
	}
	ret = wait_child(pid, timeout_seconds, &status);
	result->duration_ns = monotonic_ns() - started;
	if (WIFEXITED(status))
		result->exit_status = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		result->exit_status = 128 + WTERMSIG(status);
	else if (!ret)
		return -ECHILD;
	result->runner_errno = ret ? -ret : 0;
	return ret;
}

static int files_equal(const char *left_path, const char *right_path,
		       bool *equal)
{
	char left[16384];
	char right[16384];
	int left_fd;
	int right_fd;
	off_t offset = 0;
	int ret = 0;

	*equal = false;
	left_fd = open(left_path, O_RDONLY | O_CLOEXEC);
	if (left_fd < 0)
		return -errno;
	right_fd = open(right_path, O_RDONLY | O_CLOEXEC);
	if (right_fd < 0) {
		ret = -errno;
		close(left_fd);
		return ret;
	}
	for (;;) {
		ssize_t left_count;
		ssize_t right_count;

		do {
			left_count = pread(left_fd, left, sizeof(left), offset);
		} while (left_count < 0 && errno == EINTR);
		if (left_count < 0) {
			ret = -errno;
			break;
		}
		do {
			right_count = pread(right_fd, right, sizeof(right), offset);
		} while (right_count < 0 && errno == EINTR);
		if (right_count < 0) {
			ret = -errno;
			break;
		}
		if (left_count != right_count)
			break;
		if (!left_count) {
			*equal = true;
			break;
		}
		if (memcmp(left, right, (size_t)left_count))
			break;
		offset += left_count;
	}
	if (close(left_fd) && !ret)
		ret = -errno;
	if (close(right_fd) && !ret)
		ret = -errno;
	return ret;
}

static int capture_snapshot(const char *path, struct file_snapshot *snapshot)
{
	if (stat(path, &snapshot->st))
		return -errno;
	if (!S_ISREG(snapshot->st.st_mode))
		return -EINVAL;
	return 0;
}

static bool snapshot_equal(const struct file_snapshot *left,
			   const struct file_snapshot *right)
{
	return left->st.st_dev == right->st.st_dev &&
	       left->st.st_ino == right->st.st_ino &&
	       left->st.st_mode == right->st.st_mode &&
	       left->st.st_uid == right->st.st_uid &&
	       left->st.st_gid == right->st.st_gid &&
	       left->st.st_size == right->st.st_size &&
	       left->st.st_mtim.tv_sec == right->st.st_mtim.tv_sec &&
	       left->st.st_mtim.tv_nsec == right->st.st_mtim.tv_nsec;
}

static int split_parent(const char *path, char *parent, size_t parent_size)
{
	const char *slash = strrchr(path, '/');
	size_t length;

	if (!slash || slash == path)
		return -EINVAL;
	length = (size_t)(slash - path);
	if (length >= parent_size)
		return -ENAMETOOLONG;
	memcpy(parent, path, length);
	parent[length] = '\0';
	return 0;
}

static int build_expected_paths(const char *test_dir,
				struct focal_mapping mappings[FOCAL_OBJECTS])
{
	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		char parent[PATH_MAX];
		int ret;

		mappings[index].spec = &focal_specs[index];
		mappings[index].target_id = (uint32_t)index + 1;
		if (focal_specs[index].subdir[0])
			ret = namei_ext_path_join(
				parent, sizeof(parent), test_dir,
				focal_specs[index].subdir);
		else if (snprintf(parent, sizeof(parent), "%s", test_dir) >=
			 (int)sizeof(parent))
			ret = -ENAMETOOLONG;
		else
			ret = 0;
		if (!ret)
			ret = namei_ext_path_join(
				mappings[index].source,
				sizeof(mappings[index].source), parent,
				focal_specs[index].name);
		if (ret)
			return ret;
		if (snprintf(mappings[index].source_parent,
			     sizeof(mappings[index].source_parent), "%s",
			     parent) >=
		    (int)sizeof(mappings[index].source_parent))
			return -ENAMETOOLONG;
	}
	return 0;
}

static int parse_mapping_log(const char *path,
			     struct focal_mapping mappings[FOCAL_OBJECTS])
{
	static const char marker[] = "add_global_name - Adding ";
	char *line = NULL;
	size_t capacity = 0;
	FILE *input = fopen(path, "r");
	int ret = 0;

	if (!input)
		return -errno;
	while (getline(&line, &capacity, input) >= 0) {
		char *local = strstr(line, marker);
		char *global;
		char *separator;
		char *index_marker;

		if (!local)
			continue;
		local += sizeof(marker) - 1;
		separator = strstr(local, ", ");
		if (!separator)
			continue;
		*separator = '\0';
		global = separator + 2;
		index_marker = strstr(global, ", index=");
		if (!index_marker)
			continue;
		*index_marker = '\0';
		if (!strstr(local, "-spindlens-file-"))
			continue;
		for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
			if (strcmp(global, mappings[index].source))
				continue;
			mappings[index].seen++;
			if (mappings[index].seen == 1 &&
			    snprintf(mappings[index].cache,
				     sizeof(mappings[index].cache), "%s",
				     local) >=
				    (int)sizeof(mappings[index].cache))
				ret = -ENAMETOOLONG;
			break;
		}
		if (ret)
			break;
	}
	if (ferror(input) && !ret)
		ret = -EIO;
	free(line);
	if (fclose(input) && !ret)
		ret = -errno;
	return ret;
}

static int collect_mapping_logs(const char *test_dir, const char *result_dir,
				struct focal_mapping mappings[FOCAL_OBJECTS],
				unsigned int *log_count_out)
{
	struct dirent *entry;
	DIR *directory = opendir(test_dir);
	unsigned int log_count = 0;
	int ret = 0;

	if (!directory)
		return -errno;
	for (;;) {
		char source[PATH_MAX];
		char destination[PATH_MAX];

		errno = 0;
		entry = readdir(directory);
		if (!entry) {
			if (errno)
				ret = -errno;
			break;
		}
		if (strncmp(entry->d_name, "spindle_output.",
			    strlen("spindle_output.")))
			continue;
		ret = namei_ext_path_join(source, sizeof(source), test_dir,
					  entry->d_name);
		if (!ret) {
			char output_name[NAME_MAX + 16];

			if (snprintf(output_name, sizeof(output_name),
				     "source-%s", entry->d_name) >=
			    (int)sizeof(output_name))
				ret = -ENAMETOOLONG;
			else
				ret = namei_ext_path_join(
					destination, sizeof(destination),
					result_dir, output_name);
		}
		if (!ret)
			ret = namei_ext_copy_file(source, destination);
		if (!ret)
			ret = parse_mapping_log(source, mappings);
		if (ret)
			break;
		log_count++;
	}
	if (closedir(directory) && !ret)
		ret = -errno;
	*log_count_out = log_count;
	return ret;
}

static int validate_mappings(FILE *out,
			     struct focal_mapping mappings[FOCAL_OBJECTS])
{
	struct stat cache_root;
	size_t cache_prefix_length = strlen(CACHE_ROOT);
	int failures = 0;

	if (stat(CACHE_ROOT, &cache_root))
		return -errno;
	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		struct focal_mapping *mapping = &mappings[index];
		bool bytes_equal = false;
		bool pass = true;
		int ret = 0;

		if (mapping->seen != 1 || !mapping->cache[0])
			pass = false;
		if (pass &&
		    (strncmp(mapping->cache, CACHE_ROOT,
			     cache_prefix_length) ||
		     mapping->cache[cache_prefix_length] != '/'))
			pass = false;
		if (pass) {
			ret = capture_snapshot(mapping->source,
					       &mapping->source_before);
			if (!ret)
				ret = capture_snapshot(mapping->cache,
						       &mapping->cache_before);
			if (!ret)
				ret = files_equal(mapping->source, mapping->cache,
						  &bytes_equal);
			if (ret)
				pass = false;
		}
		if (pass &&
		    (mapping->cache_before.st.st_dev != cache_root.st_dev ||
		     mapping->source_before.st.st_dev ==
			     mapping->cache_before.st.st_dev ||
		     (mapping->source_before.st.st_dev ==
			      mapping->cache_before.st.st_dev &&
		      mapping->source_before.st.st_ino ==
			      mapping->cache_before.st.st_ino) ||
		     mapping->source_before.st.st_size !=
			     mapping->cache_before.st.st_size ||
		     !bytes_equal))
			pass = false;
		emit_mapping(out, mapping, bytes_equal, pass,
			     pass ? "unique Spindle file payload matches source"
				  : "missing, ambiguous, or invalid Spindle payload");
		failures += !pass;
	}
	return failures ? -EINVAL : 0;
}

static int prepare_environment(const char *test_dir, const char *result_dir,
			       struct run_environment *environment)
{
	struct passwd *password;
	struct stat result_stat;

	if (stat(result_dir, &result_stat))
		return -errno;
	if (!result_stat.st_uid)
		return -EPERM;
	environment->uid = result_stat.st_uid;
	environment->gid = result_stat.st_gid;
	password = getpwuid(environment->uid);
	if (password && password->pw_dir && password->pw_dir[0]) {
		if (snprintf(environment->home, sizeof(environment->home), "%s",
			     password->pw_dir) >= (int)sizeof(environment->home))
			return -ENAMETOOLONG;
	} else if (snprintf(environment->home, sizeof(environment->home),
			    "/tmp") >= (int)sizeof(environment->home)) {
		return -ENAMETOOLONG;
	}
	if (password && password->pw_name && password->pw_name[0]) {
		if (snprintf(environment->user, sizeof(environment->user), "%s",
			     password->pw_name) >= (int)sizeof(environment->user))
			return -ENAMETOOLONG;
	} else if (snprintf(environment->user, sizeof(environment->user),
			    "uid-%u", environment->uid) >=
		   (int)sizeof(environment->user)) {
		return -ENAMETOOLONG;
	}
	if (snprintf(environment->home_env, sizeof(environment->home_env),
		     "HOME=%s", environment->home) >=
		    (int)sizeof(environment->home_env) ||
	    snprintf(environment->user_env, sizeof(environment->user_env),
		     "USER=%s", environment->user) >=
		    (int)sizeof(environment->user_env) ||
	    snprintf(environment->logname_env,
		     sizeof(environment->logname_env), "LOGNAME=%s",
		     environment->user) >=
		    (int)sizeof(environment->logname_env) ||
	    snprintf(environment->path_env, sizeof(environment->path_env),
		     "PATH=/usr/bin:/bin:%s", test_dir) >=
		    (int)sizeof(environment->path_env) ||
	    snprintf(environment->ld_library_path_env,
		     sizeof(environment->ld_library_path_env),
		     "LD_LIBRARY_PATH=%s", test_dir) >=
		    (int)sizeof(environment->ld_library_path_env) ||
	    snprintf(environment->tmpdir_env,
		     sizeof(environment->tmpdir_env), "TMPDIR=%s",
		     TMP_ROOT) >= (int)sizeof(environment->tmpdir_env))
		return -ENAMETOOLONG;

	char *base[] = {
		environment->home_env,
		environment->user_env,
		environment->logname_env,
		"SHELL=/bin/sh",
		environment->path_env,
		environment->ld_library_path_env,
		"SPINDLE_TEST=1",
		"SPINDLE_DEBUG=3",
		environment->tmpdir_env,
		"LC_ALL=C",
		"LANG=C",
		"TZ=UTC",
		NULL,
	};

	memcpy(environment->source_env, base, sizeof(base));
	memcpy(environment->namei_env, base, sizeof(base));
	environment->namei_env[12] =
		"LDCS_CHOSEN_PARSED_CACHEPATH=/__namei_ext_no_spindle_cache__";
	environment->namei_env[13] = NULL;
	return 0;
}

static int setup_tmpfs(const char *path, uid_t uid, gid_t gid)
{
	namei_ext_remove_tree(path);
	if (mkdir(path, 0755))
		return -errno;
	if (mount("tmpfs", path, "tmpfs", MS_NOSUID | MS_NODEV,
		  "mode=0755,size=512m"))
		return -errno;
	if (chown(path, uid, gid)) {
		int saved_errno = errno;

		umount2(path, MNT_DETACH);
		return -saved_errno;
	}
	return 0;
}

static int clear_source_logs(const char *test_dir)
{
	struct dirent *entry;
	DIR *directory = opendir(test_dir);
	int ret = 0;

	if (!directory)
		return -errno;
	for (;;) {
		char path[PATH_MAX];

		errno = 0;
		entry = readdir(directory);
		if (!entry) {
			if (errno)
				ret = -errno;
			break;
		}
		if (strncmp(entry->d_name, "spindle_output.",
			    strlen("spindle_output.")) &&
		    strncmp(entry->d_name, "spindle_test",
			    strlen("spindle_test")))
			continue;
		ret = namei_ext_path_join(path, sizeof(path), test_dir,
					  entry->d_name);
		if (!ret && unlink(path) && errno != ENOENT)
			ret = -errno;
		if (ret)
			break;
	}
	if (closedir(directory) && !ret)
		ret = -errno;
	return ret;
}

static int process_name_starts_spindle(pid_t self, bool *found_out)
{
	struct dirent *entry;
	DIR *proc = opendir("/proc");
	bool found = false;
	int ret = 0;

	if (!proc)
		return -errno;
	for (;;) {
		char *end = NULL;
		char link_path[PATH_MAX];
		char executable[PATH_MAX];
		char *base;
		long pid;
		ssize_t length;

		errno = 0;
		entry = readdir(proc);
		if (!entry) {
			if (errno)
				ret = -errno;
			break;
		}
		pid = strtol(entry->d_name, &end, 10);
		if (!entry->d_name[0] || !end || *end || pid <= 0 ||
		    pid == self)
			continue;
		if (snprintf(link_path, sizeof(link_path), "/proc/%ld/exe",
			     pid) >= (int)sizeof(link_path)) {
			ret = -ENAMETOOLONG;
			break;
		}
		length = readlink(link_path, executable,
				  sizeof(executable) - 1);
		if (length < 0) {
			if (errno == ENOENT || errno == EACCES)
				continue;
			ret = -errno;
			break;
		}
		executable[length] = '\0';
		base = strrchr(executable, '/');
		base = base ? base + 1 : executable;
		if (!strncmp(base, "spindle", strlen("spindle"))) {
			found = true;
			break;
		}
	}
	if (closedir(proc) && !ret)
		ret = -errno;
	*found_out = found;
	return ret;
}

static int wait_for_spindle_quiescence(pid_t self,
				       unsigned int timeout_seconds)
{
	uint64_t deadline = monotonic_ns() +
		(uint64_t)timeout_seconds * 1000000000ULL;

	for (;;) {
		bool found = false;
		int ret = process_name_starts_spindle(self, &found);

		if (ret)
			return ret;
		if (!found)
			return 0;
		if (monotonic_ns() >= deadline)
			return -ETIMEDOUT;
		usleep(10000);
	}
}

static int unique_parent_add(struct namei_ext_harness_policy *policy,
			     const char *cgroup_path,
			     char parents[8][PATH_MAX], size_t *parent_count,
			     const char *parent)
{
	for (size_t index = 0; index < *parent_count; index++) {
		if (!strcmp(parents[index], parent))
			return 0;
	}
	if (*parent_count >= 8)
		return -E2BIG;
	int ret;

	if (!*parent_count)
		ret = namei_ext_policy_parent_exact(cgroup_path, parent);
	else
		ret = namei_ext_policy_parent_add(cgroup_path, parent);
	if (ret)
		return ret;
	if (snprintf(parents[*parent_count], PATH_MAX, "%s", parent) >=
	    PATH_MAX)
		return -ENAMETOOLONG;
	(*parent_count)++;
	(void)policy;
	return 0;
}

static int configure_policy(
	struct namei_ext_harness_policy *policy, const char *policy_path,
	const char *cgroup_path,
	struct focal_mapping mappings[FOCAL_OBJECTS],
	uint64_t *cgroup_id_out)
{
	char parents[8][PATH_MAX] = {};
	char cache_origin_parent[PATH_MAX];
	size_t parent_count = 0;
	size_t map_count = 0;
	int ret;

	ret = namei_ext_cgroup_id(cgroup_path, cgroup_id_out);
	if (ret)
		return ret;
	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		ret = namei_ext_register_target(
			cgroup_path, mappings[index].cache,
			mappings[index].target_id);
		if (ret)
			return ret;
	}
	if (namei_ext_policy_load_attach(policy_path, cgroup_path, policy))
		return -errno;
	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		ret = unique_parent_add(policy, cgroup_path, parents,
					&parent_count,
					mappings[index].source_parent);
		if (ret)
			return ret;
		ret = namei_ext_component_map_update(
			policy, "spindle_staging_rules", *cgroup_id_out,
			mappings[index].source_parent,
			mappings[index].spec->name,
			mappings[index].target_id);
		if (ret)
			return ret;
	}
	ret = split_parent(mappings[26].cache, cache_origin_parent,
			   sizeof(cache_origin_parent));
	if (!ret)
		ret = unique_parent_add(policy, cgroup_path, parents,
					&parent_count, cache_origin_parent);
	if (!ret)
		ret = namei_ext_component_map_update(
			policy, "spindle_staging_rules", *cgroup_id_out,
			cache_origin_parent, "liborigintarget.so",
			mappings[26].target_id);
	if (!ret)
		ret = namei_ext_component_map_count(
			policy, "spindle_staging_rules", &map_count);
	if (ret)
		return ret;
	return map_count == FOCAL_OBJECTS + 1 ? 0 : -EINVAL;
}

static int collect_counter(struct namei_ext_harness_policy *policy,
			   const char *map, uint32_t key, uint64_t *value)
{
	return namei_ext_policy_counter(policy, map, key, value);
}

static int identity_probe(const char *cgroup_path,
			  const struct run_environment *environment,
			  const char *logical_path, struct stat *stat_out)
{
	struct identity_wire wire = {};
	int pipe_fd[2];
	int status = 0;
	pid_t pid;
	int ret;

	if (pipe2(pipe_fd, O_CLOEXEC))
		return -errno;
	pid = fork();
	if (pid < 0) {
		ret = -errno;
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		return ret;
	}
	if (!pid) {
		int fd;

		close(pipe_fd[0]);
		if (namei_ext_move_self_to_cgroup(cgroup_path)) {
			wire.error = errno ? errno : EIO;
			goto child_done;
		}
		if (drop_privileges(environment->uid, environment->gid)) {
			wire.error = errno ? errno : EIO;
			goto child_done;
		}
		fd = open(logical_path, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			wire.error = errno;
			goto child_done;
		}
		if (fstat(fd, &wire.st))
			wire.error = errno;
		close(fd);
child_done:
		if (write_all(pipe_fd[1], &wire, sizeof(wire)))
			_exit(126);
		close(pipe_fd[1]);
		_exit(0);
	}
	close(pipe_fd[1]);
	ret = read_all(pipe_fd[0], &wire, sizeof(wire));
	close(pipe_fd[0]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		return -ECHILD;
	if (ret)
		return ret;
	if (wire.error)
		return -wire.error;
	*stat_out = wire.st;
	return 0;
}

static int permission_probe(const char *cgroup_path,
			    const struct run_environment *environment,
			    const char *logical_path, int *observed_errno)
{
	int pipe_fd[2];
	int status = 0;
	pid_t pid;
	int ret;

	if (pipe2(pipe_fd, O_CLOEXEC))
		return -errno;
	pid = fork();
	if (pid < 0) {
		ret = -errno;
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		return ret;
	}
	if (!pid) {
		int error = 0;
		int fd;

		close(pipe_fd[0]);
		if (namei_ext_move_self_to_cgroup(cgroup_path))
			error = errno ? errno : EIO;
		else if (drop_privileges(environment->uid, environment->gid))
			error = errno ? errno : EIO;
		else {
			fd = open(logical_path, O_RDONLY | O_CLOEXEC);
			if (fd >= 0) {
				close(fd);
				error = 0;
			} else {
				error = errno;
			}
		}
		if (write_all(pipe_fd[1], &error, sizeof(error)))
			_exit(126);
		close(pipe_fd[1]);
		_exit(0);
	}
	close(pipe_fd[1]);
	ret = read_all(pipe_fd[0], observed_errno,
		       sizeof(*observed_errno));
	close(pipe_fd[0]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		return -ECHILD;
	return ret;
}

static bool file_contains(const char *path, const char *needle)
{
	char buffer[4096];
	FILE *input = fopen(path, "r");
	bool found = false;

	if (!input)
		return false;
	while (fgets(buffer, sizeof(buffer), input)) {
		if (strstr(buffer, needle)) {
			found = true;
			break;
		}
	}
	fclose(input);
	return found;
}

static int file_is_empty(const char *path, bool *empty)
{
	struct stat st;

	if (stat(path, &st))
		return -errno;
	if (!S_ISREG(st.st_mode))
		return -EINVAL;
	*empty = st.st_size == 0;
	return 0;
}

static int validate_loader_progress(const char *path, bool *valid)
{
	char line[256];
	size_t index = 0;
	FILE *input = fopen(path, "r");
	int ret = 0;

	*valid = false;
	if (!input)
		return -errno;
	while (fgets(line, sizeof(line), input)) {
		if (index >= EXPECTED_LOADER_PROGRESS ||
		    strcmp(line, expected_loader_progress[index]))
			goto out;
		index++;
	}
	if (ferror(input)) {
		ret = -EIO;
		goto out;
	}
	*valid = index == EXPECTED_LOADER_PROGRESS;
out:
	if (fclose(input) && !ret)
		ret = -errno;
	return ret;
}

static int capture_cache_tree(const char *result_dir)
{
	char output_path[PATH_MAX];
	pid_t pid;
	int status = 0;
	int ret;

	ret = namei_ext_path_join(output_path, sizeof(output_path),
				  result_dir, "cache-tree.txt");
	if (ret)
		return ret;
	pid = fork();
	if (pid < 0)
		return -errno;
	if (!pid) {
		int output = open(output_path,
				  O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
				  0644);

		if (output < 0 || dup2(output, STDOUT_FILENO) < 0)
			_exit(120);
		close(output);
		execl("/usr/bin/find", "find", CACHE_ROOT, "-xdev",
		      "-printf",
		      "%y\t%m\t%U\t%G\t%s\t%D\t%i\t%T@\t%p\n",
		      (char *)NULL);
		_exit(121);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status))
		return -ECHILD;
	return 0;
}

static int write_manifest(const char *result_dir, const char *phase,
			  struct focal_mapping mappings[FOCAL_OBJECTS])
{
	char name[64];
	char path[PATH_MAX];
	FILE *output;

	if (snprintf(name, sizeof(name), "focal-manifest-%s.jsonl",
		     phase) >= (int)sizeof(name))
		return -ENAMETOOLONG;
	if (namei_ext_path_join(path, sizeof(path), result_dir, name))
		return -ENAMETOOLONG;
	output = fopen(path, "w");
	if (!output)
		return -errno;
	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		struct file_snapshot source;
		struct file_snapshot cache;
		int ret = capture_snapshot(mappings[index].source, &source);

		if (!ret)
			ret = capture_snapshot(mappings[index].cache, &cache);
		if (ret) {
			fclose(output);
			return ret;
		}
		fputs("{\"phase\":", output);
		json_string(output, phase);
		fprintf(output, ",\"target_id\":%u,\"name\":",
			mappings[index].target_id);
		json_string(output, mappings[index].spec->name);
		fputs(",\"source\":", output);
		json_string(output, mappings[index].source);
		fputs(",\"cache\":", output);
		json_string(output, mappings[index].cache);
		fprintf(output,
			",\"source_dev\":%llu,\"source_ino\":%llu,"
			"\"source_mode\":%u,\"source_uid\":%u,"
			"\"source_gid\":%u,\"source_size\":%lld,"
			"\"source_mtime_sec\":%lld,"
			"\"source_mtime_nsec\":%ld",
			(unsigned long long)source.st.st_dev,
			(unsigned long long)source.st.st_ino,
			(unsigned int)source.st.st_mode,
			(unsigned int)source.st.st_uid,
			(unsigned int)source.st.st_gid,
			(long long)source.st.st_size,
			(long long)source.st.st_mtim.tv_sec,
			source.st.st_mtim.tv_nsec);
		fprintf(output,
			",\"cache_dev\":%llu,\"cache_ino\":%llu,"
			"\"cache_mode\":%u,\"cache_uid\":%u,"
			"\"cache_gid\":%u,\"cache_size\":%lld,"
			"\"cache_mtime_sec\":%lld,"
			"\"cache_mtime_nsec\":%ld",
			(unsigned long long)cache.st.st_dev,
			(unsigned long long)cache.st.st_ino,
			(unsigned int)cache.st.st_mode,
			(unsigned int)cache.st.st_uid,
			(unsigned int)cache.st.st_gid,
			(long long)cache.st.st_size,
			(long long)cache.st.st_mtim.tv_sec,
			cache.st.st_mtim.tv_nsec);
		fputs("}\n", output);
	}
	return fclose(output) ? -errno : 0;
}

static int setup_canary(const char *source, char *canary,
			size_t canary_size, bool *mounted)
{
	struct stat source_stat;
	int fd;

	if (snprintf(canary, canary_size,
		     "/tmp/namei-ext-spindle-canary-%ld",
		     (long)getpid()) >= (int)canary_size)
		return -ENAMETOOLONG;
	fd = open(canary, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0444);
	if (fd < 0)
		return -errno;
	if (close(fd))
		return -errno;
	if (mount(canary, source, NULL, MS_BIND, NULL))
		return -errno;
	*mounted = true;
	if (mount(NULL, source, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY, NULL))
		return -errno;
	if (stat(source, &source_stat))
		return -errno;
	return source_stat.st_size == 0 ? 0 : -EINVAL;
}

static int validate_preservation(
	FILE *out, struct focal_mapping mappings[FOCAL_OBJECTS])
{
	int failures = 0;

	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		struct file_snapshot source_after;
		struct file_snapshot cache_after;
		bool bytes_equal = false;
		int ret = capture_snapshot(mappings[index].source,
					   &source_after);
		if (!ret)
			ret = capture_snapshot(mappings[index].cache,
					       &cache_after);
		if (!ret)
			ret = files_equal(mappings[index].source,
					  mappings[index].cache, &bytes_equal);
		bool pass = !ret &&
			snapshot_equal(&mappings[index].source_before,
				       &source_after) &&
			snapshot_equal(&mappings[index].cache_before,
				       &cache_after) &&
			bytes_equal;
		bool source_metadata_equal =
			snapshot_equal(&mappings[index].source_before,
				       &source_after);
		bool cache_metadata_equal =
			snapshot_equal(&mappings[index].cache_before,
				       &cache_after);

		fputs("{\"event\":\"spindle-staging-preservation\","
		      "\"target_id\":", out);
		fprintf(out, "%u,\"name\":", mappings[index].target_id);
		json_string(out, mappings[index].spec->name);
		fprintf(out,
			",\"source_metadata_equal\":%s,"
			"\"cache_metadata_equal\":%s,\"bytes_equal\":%s,"
			"\"pass\":%s}\n",
			source_metadata_equal ? "true" : "false",
			cache_metadata_equal ? "true" : "false",
			bytes_equal ? "true" : "false",
			pass ? "true" : "false");
		fflush(out);
		failures += !pass;
	}
	return failures ? -EINVAL : 0;
}

#ifndef SPINDLE_STAGING_NO_MAIN
int main(int argc, char **argv)
{
	struct namei_ext_harness_policy policy = {
		.cgroup_fd = -1,
		.prog_fd = -1,
	};
	struct focal_mapping mappings[FOCAL_OBJECTS] = {};
	struct run_environment environment = {};
	struct process_result source_result = {};
	struct process_result namei_result = {};
	struct process_result withdrawn_result = {};
	const char *cgroup_root = "/sys/fs/cgroup";
	char test_driver[PATH_MAX];
	char source_stdout[PATH_MAX];
	char source_stderr[PATH_MAX];
	char namei_stdout[PATH_MAX];
	char namei_stderr[PATH_MAX];
	char withdrawn_stdout[PATH_MAX];
	char withdrawn_stderr[PATH_MAX];
	char cgroup_path[PATH_MAX] = {};
	char canary_path[PATH_MAX] = {};
	uint64_t cgroup_id = 0;
	uint64_t select_before = 0;
	uint64_t select_after = 0;
	uint64_t hits_before[FOCAL_OBJECTS] = {};
	uint64_t hits_after[FOCAL_OBJECTS] = {};
	uint64_t withdrawn_hits_before = 0;
	uint64_t withdrawn_hits_after = 0;
	unsigned int log_count = 0;
	bool cache_mounted = false;
	bool comm_mounted = false;
	bool tmp_mounted = false;
	bool canary_mounted = false;
	bool cgroup_created = false;
	bool targets_registered = false;
	bool source_passed = false;
	bool no_spindle_process = false;
	FILE *out = NULL;
	int failures = 0;
	int ret;

	if (argc < 6 || argc > 7) {
		fprintf(stderr,
			"usage: %s POLICY_BPF_O RESULT_JSONL SPINDLE_BIN "
			"TEST_DIR RESULT_DIR [CGROUP_ROOT]\n",
			argv[0]);
		return 2;
	}
	if (argc == 7)
		cgroup_root = argv[6];
	out = fopen(argv[2], "a");
	if (!out) {
		perror("fopen result");
		return 2;
	}
	ret = build_expected_paths(argv[4], mappings);
	if (!ret)
		ret = namei_ext_path_join(test_driver, sizeof(test_driver),
					  argv[4], "test_driver");
	if (!ret)
		ret = namei_ext_path_join(source_stdout,
					  sizeof(source_stdout), argv[5],
					  "source.stdout.log");
	if (!ret)
		ret = namei_ext_path_join(source_stderr,
					  sizeof(source_stderr), argv[5],
					  "source.stderr.log");
	if (!ret)
		ret = namei_ext_path_join(namei_stdout,
					  sizeof(namei_stdout), argv[5],
					  "namei_ext.stdout.log");
	if (!ret)
		ret = namei_ext_path_join(namei_stderr,
					  sizeof(namei_stderr), argv[5],
					  "namei_ext.stderr.log");
	if (!ret)
		ret = namei_ext_path_join(withdrawn_stdout,
					  sizeof(withdrawn_stdout), argv[5],
					  "withdrawn.stdout.log");
	if (!ret)
		ret = namei_ext_path_join(withdrawn_stderr,
					  sizeof(withdrawn_stderr), argv[5],
					  "withdrawn.stderr.log");
	if (!ret &&
	    snprintf(cgroup_path, sizeof(cgroup_path),
		     "%s/namei-ext-spindle-%ld", cgroup_root,
		     (long)getpid()) >= (int)sizeof(cgroup_path))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = prepare_environment(argv[4], argv[5], &environment);
	emit_case(out, "fixture_paths_and_identity", !ret,
		  ret ? -ret : 0,
		  "frozen upstream paths and unprivileged run identity");
	if (ret) {
		failures++;
		goto cleanup;
	}
	if (access(argv[3], X_OK) || access(test_driver, X_OK)) {
		emit_case(out, "upstream_artifacts", false, errno,
			  "installed Spindle or upstream test ELF missing");
		failures++;
		goto cleanup;
	}

	ret = setup_tmpfs(CACHE_ROOT, environment.uid, environment.gid);
	if (!ret)
		cache_mounted = true;
	if (!ret)
		ret = setup_tmpfs(COMM_ROOT, environment.uid,
				  environment.gid);
	if (!ret)
		comm_mounted = true;
	if (!ret)
		ret = setup_tmpfs(TMP_ROOT, environment.uid,
				  environment.gid);
	if (!ret)
		tmp_mounted = true;
	if (!ret)
		ret = clear_source_logs(argv[4]);
	emit_case(out, "dedicated_tmpfs_and_clean_logs", !ret,
		  ret ? -ret : 0,
		  "cache, communication, and temporary roots are fresh tmpfs");
	if (ret) {
		failures++;
		goto cleanup;
	}

	char *source_argv[] = {
		argv[3],
		"--level=high",
		"--launcher=serial",
		"--pull",
		"--noclean=yes",
		"--strip=no",
		test_driver,
		"--dlopen",
		"--pull",
		"--nompi",
		NULL,
	};
	char *namei_argv[] = {
		test_driver,
		"--dlopen",
		"--pull",
		"--nompi",
		NULL,
	};
	ret = emit_runtime_contract(out, argv[3], test_driver, argv[4],
				    &environment, source_argv, namei_argv);
	if (ret) {
		emit_case(out, "runtime_contract", false, -ret,
			  "failed to resolve and record executed argv and environment");
		failures++;
		goto cleanup;
	}
	ret = run_process(argv[4], NULL, &environment, source_argv,
			  environment.source_env, source_stdout,
			  source_stderr, SOURCE_TIMEOUT_SECONDS,
			  &source_result);
	bool source_diagnostic_ok = false;
	int diagnostic_ret = file_is_empty(source_stderr,
					    &source_diagnostic_ok);

	source_passed = !ret && !diagnostic_ret &&
		source_result.exit_status == 0 && source_diagnostic_ok;
	emit_condition(out, "source_spindle", &source_result,
		       source_diagnostic_ok, source_passed,
		       "official serial pull mode exits cleanly without upstream diagnostics");
	if (!source_passed) {
		failures++;
		goto cleanup;
	}
	ret = wait_for_spindle_quiescence(
		getpid(), PROCESS_QUIESCENCE_SECONDS);
	no_spindle_process = !ret;
	emit_case(out, "source_process_quiescence",
		  !ret && no_spindle_process, ret ? -ret : 0,
		  "no live Spindle executable remains after source condition");
	if (ret || !no_spindle_process) {
		failures++;
		goto cleanup;
	}

	ret = collect_mapping_logs(argv[4], argv[5], mappings, &log_count);
	if (!ret && !log_count)
		ret = -ENOENT;
	if (!ret)
		ret = validate_mappings(out, mappings);
	emit_case(out, "source_mapping_contract", !ret,
		  ret ? -ret : 0,
		  "47 unique file payload mappings recovered from first-party logs");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = capture_cache_tree(argv[5]);
	if (!ret)
		ret = write_manifest(argv[5], "before", mappings);
	emit_case(out, "source_cache_manifest", !ret,
		  ret ? -ret : 0,
		  "complete cache tree and focal manifests preserved");
	if (ret) {
		failures++;
		goto cleanup;
	}

	ret = setup_canary(mappings[0].source, canary_path,
			   sizeof(canary_path), &canary_mounted);
	emit_case(out, "cover_libtest10_source", !ret,
		  ret ? -ret : 0,
		  "read-only empty bind canary covers source implementation");
	if (ret) {
		failures++;
		goto cleanup;
	}

	ret = mkdir(cgroup_path, 0755) ? -errno : 0;
	if (!ret) {
		cgroup_created = true;
		targets_registered = true;
		ret = configure_policy(&policy, argv[1], cgroup_path, mappings,
				       &cgroup_id);
	}
	emit_case(out, "configure_namei_ext", !ret, ret ? -ret : 0,
		  "47 targets and 48 exact source/cache-origin rules installed");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = collect_counter(&policy, "spindle_staging_counters",
			      SPINDLE_COUNTER_SELECT, &select_before);
	for (size_t index = 0; !ret && index < FOCAL_OBJECTS; index++)
		ret = collect_counter(&policy, "spindle_staging_rule_hits",
				      mappings[index].target_id,
				      &hits_before[index]);
	if (ret) {
		emit_case(out, "counter_snapshot_before", false, -ret,
			  "failed to snapshot pre-application counters");
		failures++;
		goto cleanup;
	}

	ret = run_process(argv[4], cgroup_path, &environment, namei_argv,
			  environment.namei_env, namei_stdout, namei_stderr,
			  NAMEI_TIMEOUT_SECONDS, &namei_result);
	bool namei_diagnostic_ok = false;

	diagnostic_ret = validate_loader_progress(namei_stderr,
						  &namei_diagnostic_ok);
	bool namei_passed = !ret && !diagnostic_ret &&
		namei_result.exit_status == 0 && namei_diagnostic_ok;
	emit_condition(out, "namei_ext", &namei_result,
		       namei_diagnostic_ok, namei_passed,
		       "upstream loader exits zero with its exact 44-line progress transcript");
	if (!namei_passed) {
		failures++;
		goto cleanup;
	}
	ret = collect_counter(&policy, "spindle_staging_counters",
			      SPINDLE_COUNTER_SELECT, &select_after);
	uint64_t hit_sum = 0;

	for (size_t index = 0; !ret && index < FOCAL_OBJECTS; index++) {
		ret = collect_counter(&policy, "spindle_staging_rule_hits",
				      mappings[index].target_id,
				      &hits_after[index]);
		if (!ret) {
			uint64_t delta = hits_after[index] >= hits_before[index] ?
				hits_after[index] - hits_before[index] : 0;

			hit_sum += delta;
			bool pass = hits_after[index] >= hits_before[index] &&
				delta > 0;

			emit_selection(out, &mappings[index],
				       hits_before[index], hits_after[index], pass);
			if (!pass)
				failures++;
		}
	}
	bool counters_pass = !ret && select_after >= select_before &&
		select_after - select_before == hit_sum &&
		hit_sum >= FOCAL_OBJECTS;
	emit_counter_window(out, select_before, select_after, hit_sum,
			    counters_pass);
	emit_case(out, "application_selection_attribution", counters_pass,
		  ret ? -ret : 0,
		  "all focal objects selected and aggregate equals per-target hits");
	if (!counters_pass) {
		failures++;
		goto cleanup;
	}

	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		struct stat logical_stat = {};

		ret = identity_probe(cgroup_path, &environment,
				     mappings[index].source, &logical_stat);
		bool pass = !ret &&
			logical_stat.st_dev ==
				mappings[index].cache_before.st.st_dev &&
			logical_stat.st_ino ==
				mappings[index].cache_before.st.st_ino &&
			logical_stat.st_mode ==
				mappings[index].cache_before.st.st_mode &&
			logical_stat.st_size ==
				mappings[index].cache_before.st.st_size;

		fputs("{\"event\":\"spindle-staging-identity\","
		      "\"target_id\":", out);
		fprintf(out, "%u,\"name\":", mappings[index].target_id);
		json_string(out, mappings[index].spec->name);
		fprintf(out,
			",\"actual_dev\":%llu,\"actual_ino\":%llu,"
			"\"actual_mode\":%u,\"actual_size\":%lld,"
			"\"expected_dev\":%llu,\"expected_ino\":%llu,"
			"\"expected_mode\":%u,\"expected_size\":%lld",
			(unsigned long long)logical_stat.st_dev,
			(unsigned long long)logical_stat.st_ino,
			(unsigned int)logical_stat.st_mode,
			(long long)logical_stat.st_size,
			(unsigned long long)
				mappings[index].cache_before.st.st_dev,
			(unsigned long long)
				mappings[index].cache_before.st.st_ino,
			(unsigned int)mappings[index].cache_before.st.st_mode,
			(long long)mappings[index].cache_before.st.st_size);
		fprintf(out, ",\"probe_errno\":%d,\"pass\":%s}\n",
			ret ? -ret : 0, pass ? "true" : "false");
		fflush(out);
		if (!pass) {
			failures++;
			goto cleanup;
		}
	}

	mode_t original_mode = mappings[0].cache_before.st.st_mode & 07777;
	int observed_errno = 0;

	if (chmod(mappings[0].cache, 0000))
		ret = -errno;
	else
		ret = permission_probe(cgroup_path, &environment,
				       mappings[0].source, &observed_errno);
	int restore_ret = chmod(mappings[0].cache, original_mode) ?
		-errno : 0;
	bool permission_pass = !ret && !restore_ret &&
		observed_errno == EACCES;
	fprintf(out,
		"{\"event\":\"spindle-staging-permission\","
		"\"target_id\":%u,\"temporary_mode\":0,"
		"\"original_mode\":%u,\"observed_errno\":%d,"
		"\"probe_errno\":%d,\"restore_errno\":%d,\"pass\":%s}\n",
		mappings[0].target_id, (unsigned int)original_mode,
		observed_errno, ret ? -ret : 0,
		restore_ret ? -restore_ret : 0,
		permission_pass ? "true" : "false");
	fflush(out);
	emit_case(out, "lower_file_permission", permission_pass,
		  ret ? -ret : (restore_ret ? -restore_ret : observed_errno),
		  "unprivileged logical open observes target EACCES");
	if (!permission_pass) {
		failures++;
		goto cleanup;
	}

	ret = collect_counter(&policy, "spindle_staging_rule_hits",
			      mappings[0].target_id,
			      &withdrawn_hits_before);
	if (!ret)
		ret = namei_ext_component_map_delete(
			&policy, "spindle_staging_rules", cgroup_id,
			mappings[0].source_parent, mappings[0].spec->name);
	emit_case(out, "withdraw_libtest10_rule", !ret,
		  ret ? -ret : 0,
		  "only the focal source mapping is removed");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = run_process(argv[4], cgroup_path, &environment, namei_argv,
			  environment.namei_env, withdrawn_stdout,
			  withdrawn_stderr, WITHDRAWN_TIMEOUT_SECONDS,
			  &withdrawn_result);
	bool withdrawn_exit = !ret && withdrawn_result.exit_status != 0;
	bool withdrawn_reason =
		file_contains(withdrawn_stderr, "Failed to dlopen library") &&
		file_contains(withdrawn_stderr, "libtest10.so");
	if (!ret)
		ret = collect_counter(&policy, "spindle_staging_rule_hits",
				      mappings[0].target_id,
				      &withdrawn_hits_after);
	bool withdrawn_pass = withdrawn_exit && withdrawn_reason && !ret &&
		withdrawn_hits_after == withdrawn_hits_before;
	fprintf(out,
		"{\"event\":\"spindle-staging-withdrawal\","
		"\"target_id\":%u,\"hits_before\":%llu,"
		"\"hits_after\":%llu,\"hits_delta\":%llu,"
		"\"nonzero_exit\":%s,\"expected_diagnostic\":%s,"
		"\"pass\":%s}\n",
		mappings[0].target_id,
		(unsigned long long)withdrawn_hits_before,
		(unsigned long long)withdrawn_hits_after,
		(unsigned long long)
			(withdrawn_hits_after >= withdrawn_hits_before ?
			 withdrawn_hits_after - withdrawn_hits_before : 0),
		withdrawn_exit ? "true" : "false",
		withdrawn_reason ? "true" : "false",
		withdrawn_pass ? "true" : "false");
	fflush(out);
	emit_condition(out, "withdrawn", &withdrawn_result,
		       withdrawn_reason, withdrawn_pass,
		       "covered source fails after libtest10 target withdrawal");
	if (!withdrawn_pass) {
		failures++;
		goto cleanup;
	}

	if (umount2(mappings[0].source, MNT_DETACH))
		ret = -errno;
	else {
		canary_mounted = false;
		ret = 0;
	}
	if (!ret) {
		struct file_snapshot restored;

		ret = capture_snapshot(mappings[0].source, &restored);
		if (!ret &&
		    !snapshot_equal(&mappings[0].source_before, &restored))
			ret = -EIO;
	}
	emit_case(out, "restore_libtest10_source", !ret,
		  ret ? -ret : 0,
		  "unmount reveals the original source identity and bytes");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = validate_preservation(out, mappings);
	if (!ret)
		ret = write_manifest(argv[5], "after", mappings);
	emit_case(out, "source_and_cache_preservation", !ret,
		  ret ? -ret : 0,
		  "all 47 source and cache payload manifests are unchanged");
	if (ret)
		failures++;

cleanup:
	if (canary_mounted && umount2(mappings[0].source, MNT_DETACH))
		failures++;
	if (canary_path[0] && unlink(canary_path) && errno != ENOENT)
		failures++;
	if (policy.attached) {
		if (namei_ext_policy_parent_clear(cgroup_path))
			failures++;
		if (namei_ext_policy_destroy(&policy))
			failures++;
	}
	if (targets_registered &&
	    namei_ext_clear_targets(cgroup_path))
		failures++;
	if (cgroup_created && rmdir(cgroup_path) && errno != ENOENT)
		failures++;
	bool process_found = false;

	ret = process_name_starts_spindle(getpid(), &process_found);
	emit_case(out, "final_process_quiescence",
		  !ret && !process_found, ret ? -ret : 0,
		  "no Spindle executable remains at cleanup");
	failures += !!ret || process_found;
	if (tmp_mounted && umount2(TMP_ROOT, MNT_DETACH))
		failures++;
	if (comm_mounted && umount2(COMM_ROOT, MNT_DETACH))
		failures++;
	if (cache_mounted && umount2(CACHE_ROOT, MNT_DETACH))
		failures++;
	namei_ext_remove_tree(TMP_ROOT);
	namei_ext_remove_tree(COMM_ROOT);
	namei_ext_remove_tree(CACHE_ROOT);
	fprintf(out,
		"{\"event\":\"spindle-staging-summary\","
		"\"source_system\":\"LLNL-Spindle\","
		"\"focal_objects\":%d,\"source_logs\":%u,"
		"\"source_exit\":%d,\"namei_ext_exit\":%d,"
		"\"withdrawn_exit\":%d,\"failures\":%d,"
		"\"pass\":%s}\n",
		FOCAL_OBJECTS, log_count, source_result.exit_status,
		namei_result.exit_status, withdrawn_result.exit_status,
		failures, failures ? "false" : "true");
	fclose(out);
	return failures ? 1 : 0;
}
#endif

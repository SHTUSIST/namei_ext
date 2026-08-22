// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/magic.h>
#include <namei_ext_harness.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ACTION_TARGET_ID 1
#define MAX_SCALES 8
#define RESPONSE_MAX 4096
#define ACTION_TIMEOUT_MS 120000
#define DAEMON_TIMEOUT_MS 30000
#define EXTERNAL_EVIDENCE_TIMEOUT_MS 120000

enum build_action_counter {
	BAS_COUNTER_TOTAL = 0,
	BAS_COUNTER_LOOKUP = 1,
	BAS_COUNTER_READDIR = 2,
	BAS_COUNTER_SELECT = 3,
	BAS_COUNTER_ALLOW_LOOKUP = 4,
	BAS_COUNTER_ALLOW_READDIR = 5,
	BAS_COUNTER_HIDE_LOOKUP = 6,
	BAS_COUNTER_HIDE_READDIR = 7,
	BAS_COUNTER_PASS = 8,
	BAS_COUNTER_MAX = 9,
};

struct sandboxfs_process {
	pid_t pid;
	int input_fd;
	int output_fd;
	char mountpoint[PATH_MAX];
};

struct process_stats {
	unsigned long long user_ticks;
	unsigned long long system_ticks;
	unsigned long long voluntary_context_switches;
	unsigned long long involuntary_context_switches;
	unsigned long long vm_hwm_kb;
};

struct action_process {
	const char *name;
	const char *bazel;
	const char *workspace;
	const char *output_base;
	const char *install_base;
	const char *cgroup;
	const char *stdout_path;
	const char *stderr_path;
	const char *logical_action;
	const char *sandbox_view;
	pid_t pid;
	int setup_fd;
	int start_fd;
	int wait_status;
	bool wait_status_valid;
};

struct sample_paths {
	char sample_root[PATH_MAX];
	char lower_a[PATH_MAX];
	char lower_b[PATH_MAX];
	char workspace_a[PATH_MAX];
	char workspace_b[PATH_MAX];
	char output_base_a[PATH_MAX];
	char output_base_b[PATH_MAX];
	char ready_a[PATH_MAX];
	char ready_b[PATH_MAX];
	char started_a[PATH_MAX];
	char started_b[PATH_MAX];
	char finished_a[PATH_MAX];
	char finished_b[PATH_MAX];
	char release[PATH_MAX];
	char bazel_output_a[PATH_MAX];
	char bazel_output_b[PATH_MAX];
	char expected_concat_a[PATH_MAX];
	char expected_concat_b[PATH_MAX];
	char manifest_a_before[PATH_MAX];
	char manifest_b_before[PATH_MAX];
	char manifest_a_after[PATH_MAX];
	char manifest_b_after[PATH_MAX];
	char stdout_a[PATH_MAX];
	char stdout_b[PATH_MAX];
	char stderr_a[PATH_MAX];
	char stderr_b[PATH_MAX];
	char saved_output_a[PATH_MAX];
	char saved_output_b[PATH_MAX];
};

static unsigned long long nsec_now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts))
		return 0;
	return (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static int format_string(char *dst, size_t size, const char *format, ...)
{
	va_list args;
	int len;

	va_start(args, format);
	len = vsnprintf(dst, size, format, args);
	va_end(args);
	if (len < 0)
		return -errno;
	if ((size_t)len >= size)
		return -ENAMETOOLONG;
	return 0;
}

static int mkdir_one(const char *path)
{
	if (!mkdir(path, 0755))
		return 0;
	return errno == EEXIST ? 0 : -errno;
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

static int read_text(const char *path, char *buffer, size_t size)
{
	ssize_t nread;
	int fd;

	if (!size)
		return -EINVAL;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	nread = read(fd, buffer, size - 1);
	if (nread < 0) {
		int ret = -errno;

		close(fd);
		return ret;
	}
	if (close(fd))
		return -errno;
	buffer[nread] = '\0';
	return 0;
}

static bool path_text_equals(const char *path, const char *expected)
{
	char buffer[256];

	return !read_text(path, buffer, sizeof(buffer)) &&
	       !strcmp(buffer, expected);
}

static int copy_file(const char *source, const char *destination)
{
	char buffer[8192];
	int input;
	int output;
	int ret = 0;

	input = open(source, O_RDONLY | O_CLOEXEC);
	if (input < 0)
		return -errno;
	output = open(destination, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
		      0644);
	if (output < 0) {
		ret = -errno;
		close(input);
		return ret;
	}
	for (;;) {
		ssize_t nread = read(input, buffer, sizeof(buffer));

		if (nread < 0) {
			if (errno == EINTR)
				continue;
			ret = -errno;
			break;
		}
		if (!nread)
			break;
		ret = write_all(output, buffer, (size_t)nread);
		if (ret)
			break;
	}
	if (close(input) && !ret)
		ret = -errno;
	if (close(output) && !ret)
		ret = -errno;
	return ret;
}

static int files_equal(const char *left, const char *right)
{
	char left_buffer[8192];
	char right_buffer[8192];
	int left_fd;
	int right_fd;
	int ret = 0;

	left_fd = open(left, O_RDONLY | O_CLOEXEC);
	if (left_fd < 0)
		return -errno;
	right_fd = open(right, O_RDONLY | O_CLOEXEC);
	if (right_fd < 0) {
		ret = -errno;
		close(left_fd);
		return ret;
	}
	for (;;) {
		ssize_t left_read = read(left_fd, left_buffer, sizeof(left_buffer));
		ssize_t right_read;

		if (left_read < 0) {
			if (errno == EINTR)
				continue;
			ret = -errno;
			break;
		}
		right_read = read(right_fd, right_buffer, sizeof(right_buffer));
		if (right_read < 0) {
			if (errno == EINTR)
				continue;
			ret = -errno;
			break;
		}
		if (left_read != right_read ||
		    (left_read &&
		     memcmp(left_buffer, right_buffer, (size_t)left_read))) {
			ret = -EIO;
			break;
		}
		if (!left_read)
			break;
	}
	close(left_fd);
	close(right_fd);
	return ret;
}

static int wait_child_timeout(pid_t pid, unsigned int timeout_ms,
			      int *status_out)
{
	unsigned int elapsed = 0;

	if (status_out)
		*status_out = -1;

	while (elapsed <= timeout_ms) {
		int status;
		pid_t waited = waitpid(pid, &status, WNOHANG);

		if (waited == pid) {
			if (status_out)
				*status_out = status;
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
				return 0;
			if (WIFEXITED(status))
				return -EIO;
			if (WIFSIGNALED(status))
				return -EINTR;
			return -EIO;
		}
		if (waited < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		usleep(10000);
		elapsed += 10;
	}
	return -ETIMEDOUT;
}

static void record_action_status(struct action_process *action, int status)
{
	action->pid = -1;
	action->wait_status = status;
	action->wait_status_valid = true;
}

static int detect_action_exit(struct action_process *action,
			      struct action_process **failed_action_out)
{
	int status;
	pid_t waited;

	if (!action || action->pid <= 0)
		return 0;
	waited = waitpid(action->pid, &status, WNOHANG);
	if (waited == action->pid) {
		record_action_status(action, status);
		*failed_action_out = action;
		return -EIO;
	}
	if (waited < 0 && errno != EINTR) {
		*failed_action_out = action;
		return -errno;
	}
	return 0;
}

static int wait_action_timeout(struct action_process *action,
			       unsigned int timeout_ms)
{
	int status;
	int ret = wait_child_timeout(action->pid, timeout_ms, &status);

	if (status >= 0)
		record_action_status(action, status);
	return ret;
}

static int terminate_child(pid_t pid)
{
	unsigned int elapsed = 0;

	if (kill(pid, SIGTERM) && errno != ESRCH)
		return -errno;
	while (elapsed <= DAEMON_TIMEOUT_MS) {
		int status;
		pid_t waited = waitpid(pid, &status, WNOHANG);

		if (waited == pid || (waited < 0 && errno == ECHILD))
			return 0;
		if (waited < 0 && errno != EINTR)
			return -errno;
		usleep(10000);
		elapsed += 10;
	}
	if (kill(pid, SIGKILL) && errno != ESRCH)
		return -errno;
	while (waitpid(pid, NULL, 0) < 0) {
		if (errno == ECHILD)
			return 0;
		if (errno != EINTR)
			return -errno;
	}
	return 0;
}

static int terminate_action(struct action_process *action)
{
	unsigned int elapsed = 0;

	if (action->pid <= 0)
		return 0;
	if (kill(action->pid, SIGTERM) && errno != ESRCH)
		return -errno;
	while (elapsed <= DAEMON_TIMEOUT_MS) {
		int status;
		pid_t waited = waitpid(action->pid, &status, WNOHANG);

		if (waited == action->pid) {
			record_action_status(action, status);
			return 0;
		}
		if (waited < 0) {
			if (errno == EINTR)
				continue;
			if (errno == ECHILD) {
				action->pid = -1;
				return action->wait_status_valid ? 0 : -ECHILD;
			}
			return -errno;
		}
		usleep(10000);
		elapsed += 10;
	}
	if (kill(action->pid, SIGKILL) && errno != ESRCH)
		return -errno;
	for (;;) {
		int status;
		pid_t waited = waitpid(action->pid, &status, 0);

		if (waited == action->pid) {
			record_action_status(action, status);
			return 0;
		}
		if (waited < 0 && errno == EINTR)
			continue;
		if (waited < 0 && errno == ECHILD) {
			action->pid = -1;
			return action->wait_status_valid ? 0 : -ECHILD;
		}
		return -errno;
	}
}

static int verify_action_output(const char *path, const char *expected_id,
				const char *expected_contents,
				unsigned long long *bytes_out)
{
	char output_buffer[8192];
	char expected_buffer[8192];
	char *line = NULL;
	size_t line_capacity = 0;
	ssize_t line_length;
	FILE *output = NULL;
	FILE *expected = NULL;
	unsigned long long total = 0;
	int ret = 0;

	output = fopen(path, "r");
	if (!output)
		return -errno;
	expected = fopen(expected_contents, "r");
	if (!expected) {
		ret = -errno;
		goto out;
	}
	line_length = getline(&line, &line_capacity, output);
	if (line_length < 0 || (size_t)line_length != strlen(expected_id) + 1 ||
	    memcmp(line, expected_id, strlen(expected_id)) ||
	    line[line_length - 1] != '\n') {
		ret = -EIO;
		goto out;
	}
	total = (unsigned long long)line_length;
	for (;;) {
		size_t output_read = fread(output_buffer, 1, sizeof(output_buffer),
					   output);
		size_t expected_read = fread(expected_buffer, 1,
					      sizeof(expected_buffer), expected);

		if (ferror(output) || ferror(expected)) {
			ret = -EIO;
			break;
		}
		if (output_read != expected_read ||
		    (output_read && memcmp(output_buffer, expected_buffer,
					   output_read))) {
			ret = -EIO;
			break;
		}
		total += output_read;
		if (!output_read)
			break;
	}
	if (!ret)
		*bytes_out = total;
out:
	free(line);
	if (expected && fclose(expected) && !ret)
		ret = -errno;
	if (fclose(output) && !ret)
		ret = -errno;
	return ret;
}

static void emit_failure(FILE *out, const char *condition,
			 unsigned int repetition, const char *stage, int ret)
{
	fprintf(out,
		"{\"event\":\"build-action-rq2-failure\","
		"\"condition\":\"%s\",\"repetition\":%u,"
		"\"stage\":\"%s\",\"errno\":%d,\"pass\":false}\n",
		condition, repetition, stage, ret < 0 ? -ret : ret);
	fflush(out);
}

static void emit_capacity(FILE *out, const char *condition,
			  unsigned int repetition, unsigned int requested,
			  size_t inserted, size_t removed, size_t remaining,
			  bool pass)
{
	fprintf(out,
		"{\"event\":\"build-action-rq2-capacity\","
		"\"condition\":\"%s\",\"repetition\":%u,"
		"\"requested\":%u,\"inserted\":%zu,\"removed\":%zu,"
		"\"remaining\":%zu,"
		"\"pass\":%s}\n",
		condition, repetition, requested, inserted, removed, remaining,
		pass ? "true" : "false");
	fflush(out);
}

static void emit_sample(FILE *out, const char *condition,
			unsigned int repetition, unsigned int scale,
			unsigned int sample, unsigned int order_index,
			unsigned long long setup_ns,
			unsigned long long action_ns,
			unsigned long long lifecycle_ns,
			const struct process_stats *before,
			const struct process_stats *after,
			unsigned long long output_bytes_a,
			unsigned long long output_bytes_b)
{
	fprintf(out,
		"{\"event\":\"build-action-rq2-sample\","
		"\"condition\":\"%s\",\"repetition\":%u,"
		"\"scale\":%u,\"sample\":%u,\"order_index\":%u,"
		"\"setup_ns\":%llu,\"action_ns\":%llu,"
		"\"lifecycle_ns\":%llu,"
		"\"sandboxfs_user_ticks\":%llu,"
		"\"sandboxfs_system_ticks\":%llu,"
		"\"sandboxfs_voluntary_context_switches\":%llu,"
		"\"sandboxfs_involuntary_context_switches\":%llu,"
		"\"sandboxfs_vm_hwm_kb\":%llu,"
		"\"output_bytes_a\":%llu,\"output_bytes_b\":%llu,"
		"\"output_exact\":true,"
		"\"actions\":2,\"concurrent\":true,"
		"\"unknown_hidden\":true,\"undeclared_hidden\":true,"
		"\"lower_objects_unchanged\":true,\"pass\":true}\n",
		condition, repetition, scale, sample, order_index, setup_ns,
		action_ns, lifecycle_ns,
		after->user_ticks - before->user_ticks,
		after->system_ticks - before->system_ticks,
		after->voluntary_context_switches -
			before->voluntary_context_switches,
		after->involuntary_context_switches -
			before->involuntary_context_switches,
		after->vm_hwm_kb, output_bytes_a, output_bytes_b);
	fflush(out);
}

static void emit_sample_cleanup_failure(FILE *out, const char *condition,
					unsigned int repetition,
					unsigned int scale,
					unsigned int sample,
					const char *stage, int ret)
{
	fprintf(out,
		"{\"event\":\"build-action-rq2-failure\","
		"\"condition\":\"%s\",\"repetition\":%u,"
		"\"scale\":%u,\"sample\":%u,\"stage\":\"%s\","
		"\"errno\":%d,\"pass\":false}\n",
		condition, repetition, scale, sample, stage,
		ret < 0 ? -ret : ret);
	fflush(out);
}

static void emit_sample_failure(FILE *out, const char *condition,
				unsigned int repetition, unsigned int scale,
				unsigned int sample, const char *stage, int ret,
				const struct action_process *action)
{
	int exit_code = -1;
	int signal_number = 0;

	if (action && action->wait_status_valid) {
		if (WIFEXITED(action->wait_status))
			exit_code = WEXITSTATUS(action->wait_status);
		else if (WIFSIGNALED(action->wait_status))
			signal_number = WTERMSIG(action->wait_status);
	}
	fprintf(out,
		"{\"event\":\"build-action-rq2-failure\","
		"\"condition\":\"%s\",\"repetition\":%u,"
		"\"scale\":%u,\"sample\":%u,\"stage\":\"%s\","
		"\"errno\":%d,\"action\":\"%s\",\"exit_code\":%d,"
		"\"signal\":%d,\"pass\":false}\n",
		condition, repetition, scale, sample, stage,
		ret < 0 ? -ret : ret, action ? action->name : "",
		exit_code, signal_number);
	fflush(out);
}

static void emit_counter(FILE *out, const char *condition,
			 unsigned int repetition, unsigned int key,
			 unsigned long long value, bool pass)
{
	fprintf(out,
		"{\"event\":\"build-action-rq2-policy-counter\","
		"\"condition\":\"%s\",\"repetition\":%u,"
		"\"counter\":%u,\"value\":%llu,\"pass\":%s}\n",
		condition, repetition, key, value, pass ? "true" : "false");
	fflush(out);
}

static int parse_scales(const char *input, unsigned int scales[MAX_SCALES],
			size_t *count_out)
{
	char *copy;
	char *cursor;
	size_t count = 0;
	int ret = 0;

	copy = strdup(input);
	if (!copy)
		return -errno;
	cursor = copy;
	while (cursor && *cursor) {
		char *next = strchr(cursor, ',');
		char *end;
		unsigned long value;

		if (next)
			*next++ = '\0';
		if (count >= MAX_SCALES) {
			ret = -E2BIG;
			break;
		}
		errno = 0;
		value = strtoul(cursor, &end, 10);
		if (errno || !*cursor || *end || !value || value > UINT_MAX) {
			ret = -EINVAL;
			break;
		}
		scales[count++] = (unsigned int)value;
		cursor = next;
	}
	free(copy);
	if (ret)
		return ret;
	if (!count)
		return -EINVAL;
	*count_out = count;
	return 0;
}

static int create_sample_paths(struct sample_paths *paths, const char *root,
			       const char *result_dir, unsigned int scale,
			       unsigned int sample)
{
#define SET_PATH(field, format, ...) \
	do { \
		int path_ret = format_string(paths->field, sizeof(paths->field), \
					     format, __VA_ARGS__); \
		if (path_ret) \
			return path_ret; \
	} while (0)
	SET_PATH(sample_root, "%s/scale-%06u-sample-%03u", root, scale, sample);
	SET_PATH(lower_a, "%s/lower-a", paths->sample_root);
	SET_PATH(lower_b, "%s/lower-b", paths->sample_root);
	SET_PATH(workspace_a, "%s/workspace-a", paths->sample_root);
	SET_PATH(workspace_b, "%s/workspace-b", paths->sample_root);
	SET_PATH(output_base_a, "%s/output-base-a", paths->sample_root);
	SET_PATH(output_base_b, "%s/output-base-b", paths->sample_root);
	SET_PATH(ready_a, "%s/action-a.ready", paths->sample_root);
	SET_PATH(ready_b, "%s/action-b.ready", paths->sample_root);
	SET_PATH(started_a, "%s/action-a.started", paths->sample_root);
	SET_PATH(started_b, "%s/action-b.started", paths->sample_root);
	SET_PATH(finished_a, "%s/action-a.finished", paths->sample_root);
	SET_PATH(finished_b, "%s/action-b.finished", paths->sample_root);
	SET_PATH(release, "%s/actions.release", paths->sample_root);
	SET_PATH(bazel_output_a, "%s/bazel-bin/result.txt", paths->workspace_a);
	SET_PATH(bazel_output_b, "%s/bazel-bin/result.txt", paths->workspace_b);
	SET_PATH(expected_concat_a, "%s/expected-a.bin", paths->sample_root);
	SET_PATH(expected_concat_b, "%s/expected-b.bin", paths->sample_root);
	SET_PATH(manifest_a_before,
		 "%s/scale-%06u-sample-%03u-lower-a-before.txt",
		 result_dir, scale, sample);
	SET_PATH(manifest_b_before,
		 "%s/scale-%06u-sample-%03u-lower-b-before.txt",
		 result_dir, scale, sample);
	SET_PATH(manifest_a_after,
		 "%s/scale-%06u-sample-%03u-lower-a-after.txt",
		 result_dir, scale, sample);
	SET_PATH(manifest_b_after,
		 "%s/scale-%06u-sample-%03u-lower-b-after.txt",
		 result_dir, scale, sample);
	SET_PATH(stdout_a, "%s/scale-%06u-sample-%03u-bazel-a.stdout.log",
		 result_dir, scale, sample);
	SET_PATH(stdout_b, "%s/scale-%06u-sample-%03u-bazel-b.stdout.log",
		 result_dir, scale, sample);
	SET_PATH(stderr_a, "%s/scale-%06u-sample-%03u-bazel-a.stderr.log",
		 result_dir, scale, sample);
	SET_PATH(stderr_b, "%s/scale-%06u-sample-%03u-bazel-b.stderr.log",
		 result_dir, scale, sample);
	SET_PATH(saved_output_a, "%s/scale-%06u-sample-%03u-output-a.txt",
		 result_dir, scale, sample);
	SET_PATH(saved_output_b, "%s/scale-%06u-sample-%03u-output-b.txt",
		 result_dir, scale, sample);
#undef SET_PATH
	return 0;
}

static int create_inputs(const char *lower, const char *expected_concat,
			 char action, unsigned int scale)
{
	char path[PATH_MAX];
	char content[128];
	int concat_fd;
	unsigned int index;
	int ret;

	concat_fd = open(expected_concat,
			 O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
	if (concat_fd < 0)
		return -errno;
	for (index = 1; index <= scale; index++) {
		ret = format_string(path, sizeof(path),
				    "%s/declared-%06u.txt", lower, index);
		if (ret)
			goto out;
		ret = format_string(content, sizeof(content),
				    "declared action=%c index=%06u\n",
				    action, index);
		if (ret)
			goto out;
		ret = namei_ext_write_text(path, content);
		if (ret)
			goto out;
		ret = write_all(concat_fd, content, strlen(content));
		if (ret)
			goto out;

		ret = format_string(path, sizeof(path),
				    "%s/undeclared-%06u.txt", lower, index);
		if (ret)
			goto out;
		ret = format_string(content, sizeof(content),
				    "undeclared action=%c index=%06u\n",
				    action, index);
		if (ret)
			goto out;
		ret = namei_ext_write_text(path, content);
		if (ret)
			goto out;
	}
	ret = 0;
out:
	if (close(concat_fd) && !ret)
		ret = -errno;
	return ret;
}

static int create_fixture(struct sample_paths *paths, const char *root,
			  const char *result_dir, unsigned int scale,
			  unsigned int sample)
{
	int ret;

	ret = create_sample_paths(paths, root, result_dir, scale, sample);
	if (ret)
		return ret;
	ret = mkdir_one(paths->sample_root);
	if (!ret)
		ret = mkdir_one(paths->lower_a);
	if (!ret)
		ret = mkdir_one(paths->lower_b);
	if (!ret)
		ret = mkdir_one(paths->workspace_a);
	if (!ret)
		ret = mkdir_one(paths->workspace_b);
	if (ret)
		return ret;
	ret = create_inputs(paths->lower_a, paths->expected_concat_a, 'A',
			    scale);
	if (!ret)
		ret = create_inputs(paths->lower_b, paths->expected_concat_b, 'B',
				    scale);
	return ret;
}

static int write_lower_manifest(const char *lower, const char *output,
				unsigned int scale)
{
	struct stat st;
	char path[PATH_MAX];
	char name[64];
	FILE *file;
	unsigned int index;
	int ret = 0;

	file = fopen(output, "w");
	if (!file)
		return -errno;
	for (index = 1; index <= scale; index++) {
		const char *kinds[] = {"declared", "undeclared"};
		size_t kind;

		for (kind = 0; kind < 2; kind++) {
			ret = format_string(name, sizeof(name), "%s-%06u.txt",
					    kinds[kind], index);
			if (!ret)
				ret = namei_ext_path_join(path, sizeof(path),
							  lower, name);
			if (!ret && lstat(path, &st))
				ret = -errno;
			if (ret)
				goto out;
			fprintf(file, "%s|%llu|%llu|%o|%lld\n", name,
				(unsigned long long)st.st_dev,
				(unsigned long long)st.st_ino,
				(unsigned int)st.st_mode,
				(long long)st.st_size);
		}
	}
	ret = namei_ext_path_join(path, sizeof(path), lower, "unknown.txt");
	if (!ret && lstat(path, &st))
		ret = -errno;
	if (!ret)
		fprintf(file, "unknown.txt|%llu|%llu|%o|%lld\n",
			(unsigned long long)st.st_dev,
			(unsigned long long)st.st_ino,
			(unsigned int)st.st_mode, (long long)st.st_size);
out:
	if (fclose(file) && !ret)
		ret = -errno;
	return ret;
}

static int verify_lower_contents(const char *lower, char action,
				 unsigned int scale)
{
	char path[PATH_MAX];
	char expected[128];
	unsigned int index;
	int ret;

	for (index = 1; index <= scale; index++) {
		ret = format_string(path, sizeof(path),
				    "%s/declared-%06u.txt", lower, index);
		if (!ret)
			ret = format_string(expected, sizeof(expected),
					    "declared action=%c index=%06u\n",
					    action, index);
		if (ret || !path_text_equals(path, expected))
			return ret ? ret : -EIO;
		ret = format_string(path, sizeof(path),
				    "%s/undeclared-%06u.txt", lower, index);
		if (!ret)
			ret = format_string(expected, sizeof(expected),
					    "undeclared action=%c index=%06u\n",
					    action, index);
		if (ret || !path_text_equals(path, expected))
			return ret ? ret : -EIO;
	}
	ret = namei_ext_path_join(path, sizeof(path), lower, "unknown.txt");
	if (ret)
		return ret;
	return path_text_equals(path, "unknown-after-setup\n") ? 0 : -EIO;
}

static int write_bazel_workspace(const char *workspace,
				 const char *logical_action,
				 const char *sample_id,
				 const char *started,
				 const char *ready,
				 const char *release,
				 const char *finished,
				 unsigned int scale)
{
	char build_path[PATH_MAX];
	char workspace_path[PATH_MAX];
	char *build;
	size_t build_size = 32768;
	int len;
	int ret;

	build = malloc(build_size);
	if (!build)
		return -errno;
	ret = namei_ext_path_join(build_path, sizeof(build_path), workspace,
				  "BUILD.bazel");
	if (!ret)
		ret = namei_ext_path_join(workspace_path,
					  sizeof(workspace_path), workspace,
					  "WORKSPACE.bazel");
	if (ret)
		goto out;
	len = snprintf(
		build, build_size,
		"genrule(\n"
		"    name = \"result\",\n"
		"    outs = [\"result.txt\"],\n"
		"    cmd = \"set -eu; out=\\\"$$PWD/$@\\\"; "
		"printf '%%s' '%s' > '%s'; "
		"printf '%%s' '%s' > '%s'; "
		"while test ! -e '%s'; do sleep 0.01; done; "
		"cd '%s'; "
		"test \\\"$$(find . -mindepth 1 -maxdepth 1 -type f "
		"-name 'declared-*.txt' | wc -l)\\\" -eq %u; "
		"test ! -e undeclared-000001.txt; "
		"test ! -e unknown.txt; "
		"test -z \\\"$$(find . -mindepth 1 -maxdepth 1 -type f "
		"-name 'undeclared-*' -print -quit)\\\"; "
		"printf '%%s\\\\n' '%s' > \\\"$$out\\\"; "
		"find . -mindepth 1 -maxdepth 1 -type f "
		"-name 'declared-*.txt' -printf '%%f\\\\n' | "
		"LC_ALL=C sort | while IFS= read -r f; do cat \\\"$$f\\\"; "
		"done >> \\\"$$out\\\"; "
		"printf '%%s' '%s' > '%s'\",\n"
		")\n",
		sample_id, started, sample_id, ready, release, logical_action,
		scale, sample_id, sample_id, finished);
	if (len < 0 || (size_t)len >= build_size) {
		ret = -ENAMETOOLONG;
		goto out;
	}
	ret = namei_ext_write_text(build_path, build);
	if (!ret)
		ret = namei_ext_write_text(
			workspace_path,
			"workspace(name = \"namei_ext_build_action_rq2\")\n");
out:
	free(build);
	return ret;
}

static int spawn_action(struct action_process *action)
{
	int setup_pipe[2];
	int start_pipe[2];
	pid_t pid;

	if (pipe2(setup_pipe, O_CLOEXEC))
		return -errno;
	if (pipe2(start_pipe, O_CLOEXEC)) {
		int ret = -errno;

		close(setup_pipe[0]);
		close(setup_pipe[1]);
		return ret;
	}
	pid = fork();
	if (pid < 0)
		goto fork_error;
	if (!pid) {
		char output_base[PATH_MAX + 32];
		char install_base[PATH_MAX + 32];
		char start_signal = 0;
		int stderr_fd;
		int stdout_fd;

		close(setup_pipe[0]);
		close(start_pipe[1]);
		if (namei_ext_move_self_to_cgroup(action->cgroup))
			_exit(120);
		if (action->sandbox_view) {
			if (unshare(CLONE_NEWNS))
				_exit(121);
			if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
				_exit(122);
			if (mount(action->sandbox_view, action->logical_action, NULL,
				  MS_BIND | MS_REC, NULL))
				_exit(123);
		}
		if (write_all(setup_pipe[1], "R", 1))
			_exit(124);
		close(setup_pipe[1]);
		do {
			errno = 0;
			if (read(start_pipe[0], &start_signal, 1) == 1)
				break;
		} while (errno == EINTR);
		close(start_pipe[0]);
		if (start_signal != 'G')
			_exit(125);
		stdout_fd = open(action->stdout_path,
				 O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
		stderr_fd = open(action->stderr_path,
				 O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
		if (stdout_fd < 0 || stderr_fd < 0)
			_exit(126);
		if (dup2(stdout_fd, STDOUT_FILENO) < 0 ||
		    dup2(stderr_fd, STDERR_FILENO) < 0)
			_exit(127);
		close(stdout_fd);
		close(stderr_fd);
		if (chdir(action->workspace))
			_exit(128);
		if (snprintf(output_base, sizeof(output_base),
			     "--output_base=%s", action->output_base) >=
		    (int)sizeof(output_base) ||
		    snprintf(install_base, sizeof(install_base),
			     "--install_base=%s", action->install_base) >=
		    (int)sizeof(install_base))
			_exit(129);
		if (signal(SIGPIPE, SIG_DFL) == SIG_ERR)
			_exit(131);
		execl(action->bazel, action->bazel, "--batch", output_base,
		      install_base, "build", "//:result", "--noenable_bzlmod",
		      "--spawn_strategy=standalone",
		      "--strategy=Genrule=standalone", "--jobs=1", "--color=no",
		      "--curses=no", "--noshow_progress",
		      "--noshow_loading_progress", "--verbose_failures",
		      (char *)NULL);
		_exit(130);
	}
	close(setup_pipe[1]);
	close(start_pipe[0]);
	action->pid = pid;
	action->setup_fd = setup_pipe[0];
	action->start_fd = start_pipe[1];
	return 0;

fork_error:
	{
		int ret = -errno;

		close(setup_pipe[0]);
		close(setup_pipe[1]);
		close(start_pipe[0]);
		close(start_pipe[1]);
		return ret;
	}
}

static int wait_for_action_setup(struct action_process *action_a,
				 struct action_process *action_b,
				 struct action_process **failed_action_out,
				 unsigned int timeout_ms)
{
	struct pollfd pollfds[2] = {
		{.fd = action_a->setup_fd, .events = POLLIN | POLLHUP},
		{.fd = action_b->setup_fd, .events = POLLIN | POLLHUP},
	};
	struct action_process *actions[2] = {action_a, action_b};
	bool ready[2] = {};
	unsigned int elapsed = 0;

	while (elapsed <= timeout_ms) {
		int ret = poll(pollfds, 2, 100);
		size_t index;

		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		for (index = 0; index < 2; index++) {
			char signal;
			ssize_t nread;

			if (ready[index] || !pollfds[index].revents)
				continue;
			nread = read(pollfds[index].fd, &signal, 1);
			if (nread != 1 || signal != 'R') {
				*failed_action_out = actions[index];
				return -EIO;
			}
			close(pollfds[index].fd);
			actions[index]->setup_fd = -1;
			pollfds[index].fd = -1;
			ready[index] = true;
		}
		if (ready[0] && ready[1])
			return 0;
		for (index = 0; index < 2; index++) {
			int action_ret;

			if (ready[index])
				continue;
			action_ret = detect_action_exit(actions[index],
						failed_action_out);
			if (action_ret)
				return action_ret;
		}
		elapsed += 100;
	}
	return -ETIMEDOUT;
}

static int release_action_setup(struct action_process *action)
{
	int ret;

	if (action->start_fd < 0)
		return -EBADF;
	ret = write_all(action->start_fd, "G", 1);
	if (close(action->start_fd) && !ret)
		ret = -errno;
	action->start_fd = -1;
	return ret;
}

static void close_action_control(struct action_process *action)
{
	if (action->setup_fd >= 0) {
		close(action->setup_fd);
		action->setup_fd = -1;
	}
	if (action->start_fd >= 0) {
		close(action->start_fd);
		action->start_fd = -1;
	}
}

static int wait_for_paths(const char *first, const char *second,
			  struct action_process *action_a,
			  struct action_process *action_b,
			  struct action_process **failed_action_out,
			  unsigned int timeout_ms)
{
	unsigned int elapsed = 0;

	while (elapsed <= timeout_ms) {
		int ret;

		if (!access(first, F_OK) && !access(second, F_OK))
			return 0;
		ret = detect_action_exit(action_a, failed_action_out);
		if (ret)
			return ret;
		ret = detect_action_exit(action_b, failed_action_out);
		if (ret)
			return ret;
		usleep(10000);
		elapsed += 10;
	}
	return -ETIMEDOUT;
}

static int wait_for_paths_inotify(int inotify_fd, const char *first,
				  const char *second,
				  struct action_process *action_a,
				  struct action_process *action_b,
				  struct action_process **failed_action_out,
				  unsigned int timeout_ms)
{
	const char *first_name = strrchr(first, '/');
	const char *second_name = strrchr(second, '/');
	bool first_seen = false;
	bool second_seen = false;
	unsigned int elapsed = 0;

	if (!first_name || !second_name)
		return -EINVAL;
	first_name++;
	second_name++;
	while (elapsed <= timeout_ms) {
		char buffer[4096]
			__attribute__((aligned(__alignof__(struct inotify_event))));
		struct pollfd pollfd = {
			.fd = inotify_fd,
			.events = POLLIN,
		};
		ssize_t length;
		ssize_t offset;
		int ret;

		if (!access(first, F_OK))
			first_seen = true;
		if (!access(second, F_OK))
			second_seen = true;
		if (first_seen && second_seen)
			return 0;
		ret = poll(&pollfd, 1, 100);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (ret > 0) {
			length = read(inotify_fd, buffer, sizeof(buffer));
			if (length < 0) {
				if (errno == EINTR)
					continue;
				return -errno;
			}
			for (offset = 0; offset < length;) {
				const struct inotify_event *event =
					(const struct inotify_event *)
						&buffer[offset];

				if (event->len && (event->mask & IN_CLOSE_WRITE)) {
					if (!strcmp(event->name, first_name))
						first_seen = true;
					if (!strcmp(event->name, second_name))
						second_seen = true;
				}
				offset += sizeof(*event) + event->len;
			}
			if (first_seen && second_seen)
				return 0;
		}
		if (!first_seen) {
			ret = detect_action_exit(action_a, failed_action_out);
			if (ret)
				return ret;
		}
		if (!second_seen) {
			ret = detect_action_exit(action_b, failed_action_out);
			if (ret)
				return ret;
		}
		elapsed += 100;
	}
	return -ETIMEDOUT;
}

static int read_process_stats(pid_t pid, struct process_stats *stats)
{
	char path[64];
	char buffer[8192];
	char *cursor;
	char *line;
	char *save;
	int field;
	int ret;

	memset(stats, 0, sizeof(*stats));
	ret = format_string(path, sizeof(path), "/proc/%ld/stat", (long)pid);
	if (ret)
		return ret;
	ret = read_text(path, buffer, sizeof(buffer));
	if (ret)
		return ret;
	cursor = strrchr(buffer, ')');
	if (!cursor || cursor[1] != ' ')
		return -EIO;
	cursor += 2;
	field = 3;
	for (line = strtok_r(cursor, " ", &save); line;
	     line = strtok_r(NULL, " ", &save), field++) {
		char *end;
		unsigned long long value;

		if (field != 14 && field != 15)
			continue;
		errno = 0;
		value = strtoull(line, &end, 10);
		if (errno || *end)
			return -EIO;
		if (field == 14)
			stats->user_ticks = value;
		else
			stats->system_ticks = value;
	}

	ret = format_string(path, sizeof(path), "/proc/%ld/status", (long)pid);
	if (ret)
		return ret;
	ret = read_text(path, buffer, sizeof(buffer));
	if (ret)
		return ret;
	for (line = strtok_r(buffer, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		unsigned long long value;

		if (sscanf(line, "VmHWM: %llu kB", &value) == 1)
			stats->vm_hwm_kb = value;
		else if (sscanf(line, "voluntary_ctxt_switches: %llu",
				&value) == 1)
			stats->voluntary_context_switches = value;
		else if (sscanf(line, "nonvoluntary_ctxt_switches: %llu",
				&value) == 1)
			stats->involuntary_context_switches = value;
	}
	return 0;
}

static int wait_for_fuse_mount(struct sandboxfs_process *sandboxfs)
{
	unsigned int elapsed = 0;

	while (elapsed <= DAEMON_TIMEOUT_MS) {
		struct statfs fs;
		int status;

		if (!statfs(sandboxfs->mountpoint, &fs) &&
		    (unsigned long)fs.f_type == FUSE_SUPER_MAGIC)
			return 0;
		if (waitpid(sandboxfs->pid, &status, WNOHANG) ==
		    sandboxfs->pid) {
			sandboxfs->pid = -1;
			return -EIO;
		}
		usleep(10000);
		elapsed += 10;
	}
	return -ETIMEDOUT;
}

static int start_sandboxfs(struct sandboxfs_process *process,
			   const char *binary, const char *mountpoint,
			   const char *stderr_path)
{
	int input_pipe[2];
	int output_pipe[2];
	pid_t pid;
	int ret;

	memset(process, 0, sizeof(*process));
	process->pid = -1;
	process->input_fd = -1;
	process->output_fd = -1;
	ret = format_string(process->mountpoint, sizeof(process->mountpoint),
			    "%s", mountpoint);
	if (ret)
		return ret;
	if (pipe2(input_pipe, O_CLOEXEC))
		return -errno;
	if (pipe2(output_pipe, O_CLOEXEC)) {
		ret = -errno;
		close(input_pipe[0]);
		close(input_pipe[1]);
		return ret;
	}
	pid = fork();
	if (pid < 0) {
		ret = -errno;
		close(input_pipe[0]);
		close(input_pipe[1]);
		close(output_pipe[0]);
		close(output_pipe[1]);
		return ret;
	}
	if (!pid) {
		int error_fd = open(stderr_path,
				    O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
				    0644);

		if (error_fd < 0)
			_exit(120);
		if (dup2(input_pipe[0], STDIN_FILENO) < 0 ||
		    dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
		    dup2(error_fd, STDERR_FILENO) < 0)
			_exit(121);
		close(error_fd);
		close(input_pipe[0]);
		close(input_pipe[1]);
		close(output_pipe[0]);
		close(output_pipe[1]);
		if (signal(SIGPIPE, SIG_DFL) == SIG_ERR)
			_exit(123);
		execl(binary, binary, "--input=-", "--output=-",
			      "--reconfig_threads=1", mountpoint,
			      (char *)NULL);
		_exit(122);
	}
	close(input_pipe[0]);
	close(output_pipe[1]);
	process->pid = pid;
	process->input_fd = input_pipe[1];
	process->output_fd = output_pipe[0];
	ret = wait_for_fuse_mount(process);
	if (ret) {
		close(process->input_fd);
		close(process->output_fd);
		process->input_fd = -1;
		process->output_fd = -1;
		if (process->pid > 0) {
			kill(process->pid, SIGTERM);
			waitpid(process->pid, NULL, 0);
			process->pid = -1;
		}
	}
	return ret;
}

static int read_response_line(struct sandboxfs_process *process, char *line,
			      size_t size)
{
	size_t length = 0;

	while (length + 1 < size) {
		struct pollfd pollfd = {
			.fd = process->output_fd,
			.events = POLLIN,
		};
		int ret = poll(&pollfd, 1, DAEMON_TIMEOUT_MS);
		char value;
		ssize_t nread;

		if (ret < 0)
			return errno == EINTR ? -EINTR : -errno;
		if (!ret)
			return -ETIMEDOUT;
		if (!(pollfd.revents & POLLIN))
			return -EIO;
		nread = read(process->output_fd, &value, 1);
		if (nread < 0)
			return errno == EINTR ? -EINTR : -errno;
		if (!nread)
			return -EPIPE;
		if (value == '\n') {
			line[length] = '\0';
			return 0;
		}
		line[length++] = value;
	}
	return -E2BIG;
}

static int expect_sandboxfs_ack(struct sandboxfs_process *process,
				const char *id)
{
	char response[RESPONSE_MAX];
	char expected[RESPONSE_MAX];
	int ret;

	ret = read_response_line(process, response, sizeof(response));
	if (ret)
		return ret;
	ret = format_string(expected, sizeof(expected),
			    "{\"id\":\"%s\",\"error\":null}", id);
	if (ret)
		return ret;
	return strcmp(response, expected) ? -EIO : 0;
}

static int sandboxfs_create(struct sandboxfs_process *process,
			    const char *id, const char *lower,
			    unsigned int scale)
{
	char *request = NULL;
	size_t request_size = 0;
	FILE *stream;
	unsigned int index;
	int ret;

	stream = open_memstream(&request, &request_size);
	if (!stream)
		return -errno;
	fprintf(stream, "{\"CreateSandbox\":{\"id\":\"%s\",\"mappings\":[",
		id);
	for (index = 1; index <= scale; index++) {
		if (index > 1)
			fputc(',', stream);
		fprintf(stream,
			"{\"path_prefix\":0,\"path\":\"/declared-%06u.txt\","
			"\"underlying_path_prefix\":0,"
			"\"underlying_path\":\"%s/declared-%06u.txt\","
			"\"writable\":true}",
			index, lower, index);
	}
	fprintf(stream, "],\"prefixes\":{}}}\n");
	if (fclose(stream)) {
		free(request);
		return -errno;
	}
	ret = write_all(process->input_fd, request, request_size);
	free(request);
	if (ret)
		return ret;
	return expect_sandboxfs_ack(process, id);
}

static int sandboxfs_destroy(struct sandboxfs_process *process, const char *id)
{
	char request[RESPONSE_MAX];
	int length;
	int ret;

	length = snprintf(request, sizeof(request),
			  "{\"DestroySandbox\":\"%s\"}\n", id);
	if (length < 0 || (size_t)length >= sizeof(request))
		return -ENAMETOOLONG;
	ret = write_all(process->input_fd, request, (size_t)length);
	if (!ret)
		ret = expect_sandboxfs_ack(process, id);
	return ret;
}

static int run_fusermount_unmount(const char *mountpoint)
{
	pid_t pid = fork();

	if (pid < 0)
		return -errno;
	if (!pid) {
		if (signal(SIGPIPE, SIG_DFL) == SIG_ERR)
			_exit(126);
		execlp("fusermount", "fusermount", "-u", mountpoint,
		       (char *)NULL);
		_exit(127);
	}
	return wait_child_timeout(pid, DAEMON_TIMEOUT_MS, NULL);
}

static int stop_sandboxfs(struct sandboxfs_process *process)
{
	int ret = 0;
	int child_ret;

	if (process->input_fd >= 0) {
		if (close(process->input_fd))
			ret = -errno;
		process->input_fd = -1;
	}
	child_ret = run_fusermount_unmount(process->mountpoint);
	if (!ret && child_ret)
		ret = child_ret;
	if (process->pid > 0) {
		child_ret = wait_child_timeout(process->pid, DAEMON_TIMEOUT_MS,
					       NULL);
		if (child_ret == -ETIMEDOUT)
			terminate_child(process->pid);
		if (!ret && child_ret)
			ret = child_ret;
		process->pid = -1;
	}
	if (process->output_fd >= 0) {
		if (close(process->output_fd) && !ret)
			ret = -errno;
		process->output_fd = -1;
	}
	return ret;
}

static int add_namei_view(struct namei_ext_harness_policy *policy,
			  uint64_t cgroup_id, const char *cgroup,
			  const char *view, const char *target,
			  unsigned int scale, int *rollback_error)
{
	char name[64];
	unsigned int index;
	unsigned int inserted = 0;
	bool target_registered = false;
	bool view_added = false;
	bool root_added = false;
	int cleanup_ret;
	int ret;

	*rollback_error = 0;
	ret = namei_ext_register_target(cgroup, target, ACTION_TARGET_ID);
	if (!ret) {
		target_registered = true;
		ret = namei_ext_component_map_update(
			policy, "build_action_views", cgroup_id, view, "action",
			ACTION_TARGET_ID);
	}
	if (!ret) {
		view_added = true;
		ret = namei_ext_component_map_update(
			policy, "build_action_roots", cgroup_id, target, "", 1);
	}
	if (!ret)
		root_added = true;
	for (index = 1; !ret && index <= scale; index++) {
		ret = format_string(name, sizeof(name),
				    "declared-%06u.txt", index);
		if (!ret)
			ret = namei_ext_component_map_update(
				policy, "build_action_declared_inputs",
				cgroup_id, target, name, 1);
		if (!ret)
			inserted++;
	}
	if (!ret)
		return 0;

	for (index = 1; index <= inserted; index++) {
		cleanup_ret = format_string(name, sizeof(name),
					    "declared-%06u.txt", index);
		if (!cleanup_ret)
			cleanup_ret = namei_ext_component_map_delete(
				policy, "build_action_declared_inputs",
				cgroup_id, target, name);
		if (cleanup_ret && !*rollback_error)
			*rollback_error = cleanup_ret;
	}
	if (root_added) {
		cleanup_ret = namei_ext_component_map_delete(
			policy, "build_action_roots", cgroup_id, target, "");
		if (cleanup_ret && !*rollback_error)
			*rollback_error = cleanup_ret;
	}
	if (view_added) {
		cleanup_ret = namei_ext_component_map_delete(
			policy, "build_action_views", cgroup_id, view, "action");
		if (cleanup_ret && !*rollback_error)
			*rollback_error = cleanup_ret;
	}
	if (target_registered) {
		cleanup_ret = namei_ext_clear_targets(cgroup);
		if (cleanup_ret && !*rollback_error)
			*rollback_error = cleanup_ret;
	}
	return ret;
}

static int remove_namei_view(struct namei_ext_harness_policy *policy,
			     uint64_t cgroup_id, const char *cgroup,
			     const char *view, const char *target,
			     unsigned int scale)
{
	char name[64];
	unsigned int index;
	int first_error = 0;
	int ret;

	for (index = 1; index <= scale; index++) {
		ret = format_string(name, sizeof(name),
				    "declared-%06u.txt", index);
		if (!ret)
			ret = namei_ext_component_map_delete(
				policy, "build_action_declared_inputs",
				cgroup_id, target, name);
		if (ret && !first_error)
			first_error = ret;
	}
	ret = namei_ext_component_map_delete(policy, "build_action_roots",
					     cgroup_id, target, "");
	if (ret && !first_error)
		first_error = ret;
	ret = namei_ext_component_map_delete(policy, "build_action_views",
					     cgroup_id, view, "action");
	if (ret && !first_error)
		first_error = ret;
	ret = namei_ext_clear_targets(cgroup);
	if (ret && !first_error)
		first_error = ret;
	return first_error;
}

static int capacity_probe(FILE *out,
			  struct namei_ext_harness_policy *policy,
			  uint64_t cgroup_id, const char *parent,
			  const char *condition, unsigned int repetition,
			  unsigned int requested)
{
	char name[64];
	unsigned int attempted;
	unsigned int inserted = 0;
	uint32_t value;
	size_t occupied = 0;
	size_t removed = 0;
	size_t remaining = 0;
	int ret = 0;
	int count_ret;

	for (attempted = 0; attempted < requested; attempted++) {
		ret = format_string(name, sizeof(name), "probe-%06u", attempted);
		if (!ret)
			ret = namei_ext_component_map_update(
				policy, "build_action_declared_inputs",
				cgroup_id, parent, name, attempted + 1);
		if (!ret)
			inserted++;
		if (!ret)
			ret = namei_ext_component_map_lookup(
				policy, "build_action_declared_inputs",
				cgroup_id, parent, name, &value);
		if (ret || value != attempted + 1) {
			if (!ret)
				ret = -EIO;
			break;
		}
	}
	count_ret = namei_ext_component_map_count(
		policy, "build_action_declared_inputs", &occupied);
	if (!ret && count_ret)
		ret = count_ret;
	if (!ret && occupied != requested)
		ret = -EIO;
	for (attempted = 0; attempted < inserted; attempted++) {
		int delete_ret;

		delete_ret = format_string(name, sizeof(name),
					   "probe-%06u", attempted);
		if (!delete_ret)
			delete_ret = namei_ext_component_map_delete(
				policy, "build_action_declared_inputs",
				cgroup_id, parent, name);
		if (!delete_ret)
			removed++;
		if (delete_ret && !ret)
			ret = delete_ret;
	}
	count_ret = namei_ext_component_map_count(
		policy, "build_action_declared_inputs", &remaining);
	if (!ret && count_ret)
		ret = count_ret;
	if (!ret && remaining)
		ret = -EIO;
	emit_capacity(out, condition, repetition, requested, inserted, removed,
		      remaining, !ret);
	return ret;
}

static int wait_external_release(const char *ready, const char *release)
{
	int ret = namei_ext_write_text(ready, "ready\n");
	unsigned int elapsed = 0;

	if (ret)
		return ret;
	while (elapsed <= EXTERNAL_EVIDENCE_TIMEOUT_MS) {
		if (!access(release, F_OK))
			return 0;
		usleep(10000);
		elapsed += 10;
	}
	return -ETIMEDOUT;
}

static int run_sample(FILE *out, const char *condition,
		      unsigned int repetition, unsigned int scale,
		      unsigned int sample, unsigned int order_index,
		      const char *root, const char *result_dir,
		      const char *logical_action, const char *view,
		      const char *cgroup_a, const char *cgroup_b,
		      uint64_t cgroup_id_a, uint64_t cgroup_id_b,
		      const char *bazel, const char *install_base_a,
		      const char *install_base_b,
		      struct namei_ext_harness_policy *policy,
		      struct sandboxfs_process *sandboxfs,
		      bool *failure_emitted_out)
{
	struct sample_paths *paths;
	struct action_process action_a = {
		.name = "a",
		.pid = -1,
		.setup_fd = -1,
		.start_fd = -1,
	};
	struct action_process action_b = {
		.name = "b",
		.pid = -1,
		.setup_fd = -1,
		.start_fd = -1,
	};
	struct action_process *failed_action = NULL;
	struct process_stats daemon_before = {};
	struct process_stats daemon_after = {};
	char sandbox_id_a[64] = {};
	char sandbox_id_b[64] = {};
	char sandbox_view_a[PATH_MAX] = {};
	char sandbox_view_b[PATH_MAX] = {};
	char sample_id_a[128];
	char sample_id_b[128];
	char unknown_path[PATH_MAX];
	unsigned long long output_bytes_a = 0;
	unsigned long long output_bytes_b = 0;
	unsigned long long lifecycle_start;
	unsigned long long setup_start = 0;
	unsigned long long setup_end = 0;
	unsigned long long action_start = 0;
	unsigned long long action_end = 0;
	unsigned long long lifecycle_end;
	bool namei = !strcmp(condition, "namei_ext");
	bool view_a_created = false;
	bool view_b_created = false;
	bool controlling_failure_emitted = false;
	const char *failure_stage = "sample-allocate";
	int finish_fd = -1;
	int rollback_error;
	int ret;
	int cleanup_ret;

	*failure_emitted_out = false;
	paths = calloc(1, sizeof(*paths));
	if (!paths) {
		ret = -errno;
		emit_sample_failure(out, condition, repetition, scale, sample,
				    failure_stage, ret, NULL);
		*failure_emitted_out = true;
		return ret;
	}
	lifecycle_start = nsec_now();
	failure_stage = "fixture-create";
	ret = create_fixture(paths, root, result_dir, scale, sample);
	if (ret)
		goto out;
	failure_stage = "sample-id-a";
	ret = format_string(sample_id_a, sizeof(sample_id_a),
			    "r%02u-scale%06u-s%03u-a", repetition, scale,
			    sample);
	if (!ret) {
		failure_stage = "sample-id-b";
		ret = format_string(sample_id_b, sizeof(sample_id_b),
				    "r%02u-scale%06u-s%03u-b", repetition,
				    scale, sample);
	}
	if (!ret) {
		failure_stage = "bazel-workspace-a";
		ret = write_bazel_workspace(
			paths->workspace_a, logical_action, sample_id_a,
			paths->started_a, paths->ready_a, paths->release,
			paths->finished_a, scale);
	}
	if (!ret) {
		failure_stage = "bazel-workspace-b";
		ret = write_bazel_workspace(
			paths->workspace_b, logical_action, sample_id_b,
			paths->started_b, paths->ready_b, paths->release,
			paths->finished_b, scale);
	}
	if (ret)
		goto out;

	if (!namei) {
		failure_stage = "sandboxfs-stats-before";
		ret = read_process_stats(sandboxfs->pid, &daemon_before);
		if (ret)
			goto out;
	}
	setup_start = nsec_now();
	if (namei) {
		failure_stage = "namei-view-a-setup";
		ret = add_namei_view(policy, cgroup_id_a, cgroup_a, view,
				     paths->lower_a, scale, &rollback_error);
		if (rollback_error)
			emit_sample_cleanup_failure(
				out, condition, repetition, scale, sample,
				"namei-view-a-setup-rollback", rollback_error);
		if (!ret)
			view_a_created = true;
		if (!ret) {
			failure_stage = "namei-view-b-setup";
			ret = add_namei_view(policy, cgroup_id_b, cgroup_b, view,
					     paths->lower_b, scale, &rollback_error);
			if (rollback_error)
				emit_sample_cleanup_failure(
					out, condition, repetition, scale, sample,
					"namei-view-b-setup-rollback",
					rollback_error);
		}
		if (!ret)
			view_b_created = true;
	} else {
		failure_stage = "sandbox-id-a";
		ret = format_string(sandbox_id_a, sizeof(sandbox_id_a),
				    "r%02u-n%06u-s%03u-a", repetition, scale,
				    sample);
		if (!ret) {
			failure_stage = "sandbox-id-b";
			ret = format_string(sandbox_id_b, sizeof(sandbox_id_b),
					    "r%02u-n%06u-s%03u-b",
					    repetition, scale, sample);
		}
		if (!ret) {
			failure_stage = "sandbox-a-create";
			ret = sandboxfs_create(sandboxfs, sandbox_id_a,
					       paths->lower_a, scale);
		}
		if (!ret)
			view_a_created = true;
		if (!ret) {
			failure_stage = "sandbox-b-create";
			ret = sandboxfs_create(sandboxfs, sandbox_id_b,
					       paths->lower_b, scale);
		}
		if (!ret)
			view_b_created = true;
		if (!ret) {
			failure_stage = "sandbox-view-a-path";
			ret = namei_ext_path_join(
				sandbox_view_a, sizeof(sandbox_view_a),
				sandboxfs->mountpoint, sandbox_id_a);
		}
		if (!ret) {
			failure_stage = "sandbox-view-b-path";
			ret = namei_ext_path_join(
				sandbox_view_b, sizeof(sandbox_view_b),
				sandboxfs->mountpoint, sandbox_id_b);
		}
	}
	if (ret)
		goto teardown;

	action_a = (struct action_process){
		.name = "a",
		.bazel = bazel,
		.workspace = paths->workspace_a,
		.output_base = paths->output_base_a,
		.install_base = install_base_a,
		.cgroup = cgroup_a,
		.stdout_path = paths->stdout_a,
		.stderr_path = paths->stderr_a,
		.logical_action = logical_action,
		.sandbox_view = namei ? NULL : sandbox_view_a,
		.pid = -1,
		.setup_fd = -1,
		.start_fd = -1,
	};
	action_b = (struct action_process){
		.name = "b",
		.bazel = bazel,
		.workspace = paths->workspace_b,
		.output_base = paths->output_base_b,
		.install_base = install_base_b,
		.cgroup = cgroup_b,
		.stdout_path = paths->stdout_b,
		.stderr_path = paths->stderr_b,
		.logical_action = logical_action,
		.sandbox_view = namei ? NULL : sandbox_view_b,
		.pid = -1,
		.setup_fd = -1,
		.start_fd = -1,
	};
	failure_stage = "action-a-spawn";
	failed_action = &action_a;
	ret = spawn_action(&action_a);
	if (!ret) {
		failure_stage = "action-b-spawn";
		failed_action = &action_b;
		ret = spawn_action(&action_b);
	}
	if (!ret) {
		failure_stage = "action-setup";
		failed_action = NULL;
		ret = wait_for_action_setup(&action_a, &action_b,
					    &failed_action,
					    ACTION_TIMEOUT_MS);
	}
	setup_end = nsec_now();
	if (ret)
		goto wait_actions;

	failure_stage = "unknown-a-path";
	ret = namei_ext_path_join(unknown_path, sizeof(unknown_path),
				  paths->lower_a, "unknown.txt");
	if (!ret) {
		failure_stage = "unknown-a-create";
		ret = namei_ext_write_text(unknown_path,
					    "unknown-after-setup\n");
	}
	if (!ret) {
		failure_stage = "unknown-b-path";
		ret = namei_ext_path_join(unknown_path, sizeof(unknown_path),
					  paths->lower_b, "unknown.txt");
	}
	if (!ret) {
		failure_stage = "unknown-b-create";
		ret = namei_ext_write_text(unknown_path,
					    "unknown-after-setup\n");
	}
	if (!ret) {
		failure_stage = "lower-a-before";
		ret = write_lower_manifest(paths->lower_a,
					   paths->manifest_a_before, scale);
	}
	if (!ret) {
		failure_stage = "lower-b-before";
		ret = write_lower_manifest(paths->lower_b,
					   paths->manifest_b_before, scale);
	}
	if (ret)
		goto wait_actions;
	failure_stage = "action-a-release";
	failed_action = &action_a;
	ret = release_action_setup(&action_a);
	if (!ret) {
		failure_stage = "action-b-release";
		failed_action = &action_b;
		ret = release_action_setup(&action_b);
	}
	if (!ret) {
		failure_stage = "action-ready";
		failed_action = NULL;
		ret = wait_for_paths(paths->ready_a, paths->ready_b,
				     &action_a, &action_b, &failed_action,
				     ACTION_TIMEOUT_MS);
	}
	if (ret)
		goto wait_actions;
	failure_stage = "finish-inotify-init";
	finish_fd = inotify_init1(IN_CLOEXEC);
	if (finish_fd < 0) {
		ret = -errno;
		goto wait_actions;
	}
	failure_stage = "finish-inotify-watch";
	if (inotify_add_watch(finish_fd, paths->sample_root,
			      IN_CLOSE_WRITE) < 0) {
		ret = -errno;
		goto wait_actions;
	}
	action_start = nsec_now();
	failure_stage = "action-barrier-release";
	ret = namei_ext_write_text(paths->release, "release\n");
	if (!ret) {
		failure_stage = "action-finished";
		failed_action = NULL;
		ret = wait_for_paths_inotify(
			finish_fd, paths->finished_a, paths->finished_b,
			&action_a, &action_b, &failed_action,
			ACTION_TIMEOUT_MS);
	}
	action_end = nsec_now();
wait_actions:
	if (finish_fd >= 0) {
		close(finish_fd);
		finish_fd = -1;
	}
	close_action_control(&action_a);
	close_action_control(&action_b);
	if (action_a.pid > 0) {
		if (ret)
			cleanup_ret = terminate_action(&action_a);
		else {
			failure_stage = "action-a-reap";
			failed_action = &action_a;
			cleanup_ret = wait_action_timeout(&action_a,
						  ACTION_TIMEOUT_MS);
		}
		if (cleanup_ret == -ETIMEDOUT) {
			int terminate_ret = terminate_action(&action_a);

			if (terminate_ret)
				emit_sample_cleanup_failure(
					out, condition, repetition, scale, sample,
					"action-a-force-terminate", terminate_ret);
		}
		if (action_a.pid > 0)
			action_a.pid = -1;
		if (cleanup_ret && !ret) {
			emit_sample_failure(out, condition, repetition, scale,
					    sample, failure_stage, cleanup_ret,
					    failed_action);
			controlling_failure_emitted = true;
			ret = cleanup_ret;
		} else if (cleanup_ret) {
			emit_sample_cleanup_failure(
				out, condition, repetition, scale, sample,
				"action-a-reap", cleanup_ret);
		}
	}
	if (action_b.pid > 0) {
		if (ret)
			cleanup_ret = terminate_action(&action_b);
		else {
			failure_stage = "action-b-reap";
			failed_action = &action_b;
			cleanup_ret = wait_action_timeout(&action_b,
						  ACTION_TIMEOUT_MS);
		}
		if (cleanup_ret == -ETIMEDOUT) {
			int terminate_ret = terminate_action(&action_b);

			if (terminate_ret)
				emit_sample_cleanup_failure(
					out, condition, repetition, scale, sample,
					"action-b-force-terminate", terminate_ret);
		}
		if (action_b.pid > 0)
			action_b.pid = -1;
		if (cleanup_ret && !ret) {
			emit_sample_failure(out, condition, repetition, scale,
					    sample, failure_stage, cleanup_ret,
					    failed_action);
			controlling_failure_emitted = true;
			ret = cleanup_ret;
		} else if (cleanup_ret) {
			emit_sample_cleanup_failure(
				out, condition, repetition, scale, sample,
				"action-b-reap", cleanup_ret);
		}
	}
	if (ret)
		goto teardown;
	failure_stage = "action-markers";
	if (!path_text_equals(paths->started_a, sample_id_a) ||
	    !path_text_equals(paths->started_b, sample_id_b) ||
	    !path_text_equals(paths->finished_a, sample_id_a) ||
	    !path_text_equals(paths->finished_b, sample_id_b)) {
		ret = -EIO;
		goto teardown;
	}
	failure_stage = "action-output-a";
	ret = verify_action_output(paths->bazel_output_a, sample_id_a,
				   paths->expected_concat_a, &output_bytes_a);
	if (!ret) {
		failure_stage = "action-output-b";
		ret = verify_action_output(paths->bazel_output_b, sample_id_b,
					   paths->expected_concat_b,
					   &output_bytes_b);
	}
	if (ret)
		goto teardown;
	failure_stage = "action-output-a-save";
	ret = copy_file(paths->bazel_output_a, paths->saved_output_a);
	if (!ret) {
		failure_stage = "action-output-b-save";
		ret = copy_file(paths->bazel_output_b, paths->saved_output_b);
	}
	if (!ret) {
		failure_stage = "lower-a-after";
		ret = write_lower_manifest(paths->lower_a,
					   paths->manifest_a_after, scale);
	}
	if (!ret) {
		failure_stage = "lower-b-after";
		ret = write_lower_manifest(paths->lower_b,
					   paths->manifest_b_after, scale);
	}
	if (!ret) {
		failure_stage = "lower-a-metadata";
		ret = files_equal(paths->manifest_a_before,
				  paths->manifest_a_after);
	}
	if (!ret) {
		failure_stage = "lower-b-metadata";
		ret = files_equal(paths->manifest_b_before,
				  paths->manifest_b_after);
	}
	if (!ret) {
		failure_stage = "lower-a-contents";
		ret = verify_lower_contents(paths->lower_a, 'A', scale);
	}
	if (!ret) {
		failure_stage = "lower-b-contents";
		ret = verify_lower_contents(paths->lower_b, 'B', scale);
	}

teardown:
	if (namei) {
		if (view_b_created) {
			cleanup_ret = remove_namei_view(
				policy, cgroup_id_b, cgroup_b, view,
				paths->lower_b, scale);
			if (cleanup_ret)
				emit_sample_cleanup_failure(
					out, condition, repetition, scale, sample,
					"namei-view-b-remove", cleanup_ret);
			if (!ret && cleanup_ret) {
				ret = cleanup_ret;
				controlling_failure_emitted = true;
			}
		}
		if (view_a_created) {
			cleanup_ret = remove_namei_view(
				policy, cgroup_id_a, cgroup_a, view,
				paths->lower_a, scale);
			if (cleanup_ret)
				emit_sample_cleanup_failure(
					out, condition, repetition, scale, sample,
					"namei-view-a-remove", cleanup_ret);
			if (!ret && cleanup_ret) {
				ret = cleanup_ret;
				controlling_failure_emitted = true;
			}
		}
	} else {
		if (view_b_created) {
			cleanup_ret = sandboxfs_destroy(sandboxfs, sandbox_id_b);
			if (cleanup_ret)
				emit_sample_cleanup_failure(
					out, condition, repetition, scale, sample,
					"sandbox-b-destroy", cleanup_ret);
			if (!ret && cleanup_ret) {
				ret = cleanup_ret;
				controlling_failure_emitted = true;
			}
		}
		if (view_a_created) {
			cleanup_ret = sandboxfs_destroy(sandboxfs, sandbox_id_a);
			if (cleanup_ret)
				emit_sample_cleanup_failure(
					out, condition, repetition, scale, sample,
					"sandbox-a-destroy", cleanup_ret);
			if (!ret && cleanup_ret) {
				ret = cleanup_ret;
				controlling_failure_emitted = true;
			}
		}
		cleanup_ret = read_process_stats(sandboxfs->pid, &daemon_after);
		if (cleanup_ret)
			emit_sample_cleanup_failure(
				out, condition, repetition, scale, sample,
				"sandboxfs-stats-after", cleanup_ret);
		if (!ret && cleanup_ret) {
			ret = cleanup_ret;
			controlling_failure_emitted = true;
		}
	}
	lifecycle_end = nsec_now();
	if (!ret) {
		emit_sample(out, condition, repetition, scale, sample,
			    order_index, setup_end - setup_start,
			    action_end - action_start,
			    lifecycle_end - lifecycle_start, &daemon_before,
			    &daemon_after, output_bytes_a, output_bytes_b);
	}
out:
	if (ret) {
		if (!controlling_failure_emitted)
			emit_sample_failure(out, condition, repetition, scale, sample,
					    failure_stage, ret, failed_action);
		*failure_emitted_out = true;
	}
	free(paths);
	return ret;
}

int main(int argc, char **argv)
{
	const char *condition;
	const char *policy_path;
	const char *sandboxfs_binary;
	const char *bazel;
	const char *result_jsonl;
	const char *result_dir;
	const char *cgroup_root;
	const char *external_ready;
	const char *external_release;
	struct namei_ext_harness_policy policy = {
		.cgroup_fd = -1,
		.prog_fd = -1,
	};
	struct sandboxfs_process sandboxfs = {
		.pid = -1,
		.input_fd = -1,
		.output_fd = -1,
	};
	char root[] = "/tmp/namei-ext-build-action-rq2-XXXXXX";
	char view[PATH_MAX];
	char logical_action[PATH_MAX];
	char install_base_a[PATH_MAX];
	char install_base_b[PATH_MAX];
	char sandbox_mount[PATH_MAX];
	char sandbox_stderr[PATH_MAX];
	char cgroup_a[PATH_MAX];
	char cgroup_b[PATH_MAX];
	char bazel_path[PATH_MAX];
	unsigned int scales[MAX_SCALES];
	size_t scale_count = 0;
	unsigned int repetition;
	unsigned int samples;
	unsigned int capacity;
	unsigned int order_index = 0;
	uint64_t cgroup_id_a;
	uint64_t cgroup_id_b;
	bool namei;
	bool cgroup_a_created = false;
	bool cgroup_b_created = false;
	bool policy_loaded = false;
	bool sandbox_started = false;
	bool sample_failure_emitted = false;
	const char *failure_stage = "runner-paths";
	FILE *out;
	char *end;
	size_t offset;
	size_t index;
	int ret = 0;
	int cleanup_ret;

	if (argc != 14) {
		fprintf(stderr,
			"usage: %s CONDITION POLICY_OR_DASH SANDBOXFS_OR_DASH "
			"BAZEL RESULT_JSONL RESULT_DIR CGROUP_ROOT REPETITION "
			"SAMPLES SCALE_CSV CAPACITY_PROBE READY RELEASE\n",
			argv[0]);
		return 2;
	}
	condition = argv[1];
	policy_path = argv[2];
	sandboxfs_binary = argv[3];
	if (!realpath(argv[4], bazel_path)) {
		perror("realpath bazel");
		return 2;
	}
	bazel = bazel_path;
	result_jsonl = argv[5];
	result_dir = argv[6];
	cgroup_root = argv[7];
	external_ready = argv[12];
	external_release = argv[13];
	namei = !strcmp(condition, "namei_ext");
	if (!namei && strcmp(condition, "sandboxfs")) {
		fprintf(stderr, "invalid condition: %s\n", condition);
		return 2;
	}
	if ((namei && (!strcmp(policy_path, "-") ||
		       strcmp(sandboxfs_binary, "-"))) ||
	    (!namei && (strcmp(policy_path, "-") ||
			!strcmp(sandboxfs_binary, "-")))) {
		fprintf(stderr, "condition artifacts do not match condition\n");
		return 2;
	}
	errno = 0;
	repetition = (unsigned int)strtoul(argv[8], &end, 10);
	if (errno || !repetition || *end)
		return 2;
	errno = 0;
	samples = (unsigned int)strtoul(argv[9], &end, 10);
	if (errno || !samples || *end)
		return 2;
	ret = parse_scales(argv[10], scales, &scale_count);
	if (ret)
		return 2;
	errno = 0;
	capacity = (unsigned int)strtoul(argv[11], &end, 10);
	if (errno || *end)
		return 2;
	out = fopen(result_jsonl, "a");
	if (!out) {
		perror("fopen result");
		return 2;
	}
	failure_stage = "sigpipe-ignore";
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		ret = -errno;
		goto fail;
	}
	if (!mkdtemp(root)) {
		emit_failure(out, condition, repetition, "mkdtemp", -errno);
		fclose(out);
		return 1;
	}
	ret = namei_ext_path_join(view, sizeof(view), root, "view");
	if (!ret)
		ret = namei_ext_path_join(logical_action,
					  sizeof(logical_action), view,
					  "action");
	if (!ret)
		ret = namei_ext_path_join(install_base_a,
					  sizeof(install_base_a), root,
					  "install-base-a");
	if (!ret)
		ret = namei_ext_path_join(install_base_b,
					  sizeof(install_base_b), root,
					  "install-base-b");
	if (!ret)
		ret = namei_ext_path_join(sandbox_mount,
					  sizeof(sandbox_mount), root,
					  "sandboxfs");
	if (!ret)
		ret = namei_ext_path_join(sandbox_stderr,
					  sizeof(sandbox_stderr), result_dir,
					  "sandboxfs.stderr.log");
	if (!ret)
		ret = format_string(cgroup_a, sizeof(cgroup_a),
				    "%s/namei-ext-build-rq2-a-%ld",
				    cgroup_root, (long)getpid());
	if (!ret)
		ret = format_string(cgroup_b, sizeof(cgroup_b),
				    "%s/namei-ext-build-rq2-b-%ld",
				    cgroup_root, (long)getpid());
	if (ret)
		goto fail;
	failure_stage = "runner-directories";
	ret = mkdir_one(view);
	if (!ret)
		ret = mkdir_one(logical_action);
	if (!ret)
		ret = mkdir_one(sandbox_mount);
	if (!ret)
		ret = mkdir_one(cgroup_a);
	if (!ret)
		cgroup_a_created = true;
	if (!ret)
		ret = mkdir_one(cgroup_b);
	if (!ret)
		cgroup_b_created = true;
	if (!ret) {
		failure_stage = "cgroup-a-id";
		ret = namei_ext_cgroup_id(cgroup_a, &cgroup_id_a);
	}
	if (!ret) {
		failure_stage = "cgroup-b-id";
		ret = namei_ext_cgroup_id(cgroup_b, &cgroup_id_b);
	}
	if (ret)
		goto fail;

	if (namei) {
		failure_stage = "policy-load-attach";
		ret = namei_ext_policy_load_attach(policy_path, cgroup_root,
						    &policy);
		if (!ret)
			policy_loaded = true;
		if (!ret && capacity) {
			failure_stage = "capacity-probe";
			ret = capacity_probe(out, &policy, cgroup_id_a,
					     logical_action, condition, repetition,
					     capacity);
		}
	} else {
		failure_stage = "sandboxfs-start";
		ret = start_sandboxfs(&sandboxfs, sandboxfs_binary,
				      sandbox_mount, sandbox_stderr);
		if (!ret)
			sandbox_started = true;
	}
	if (ret)
		goto fail;
	failure_stage = "external-release";
	ret = wait_external_release(external_ready, external_release);
	if (ret)
		goto fail;

	offset = (repetition - 1) % scale_count;
	for (index = 0; index < scale_count && !ret; index++) {
		unsigned int scale = scales[(index + offset) % scale_count];
		unsigned int sample;

		for (sample = 1; sample <= samples; sample++) {
			order_index++;
			sample_failure_emitted = false;
			ret = run_sample(out, condition, repetition, scale,
					 sample, order_index, root, result_dir,
					 logical_action, view, cgroup_a, cgroup_b,
					 cgroup_id_a, cgroup_id_b, bazel,
					 install_base_a, install_base_b, &policy,
					 &sandboxfs, &sample_failure_emitted);
			if (ret)
				break;
		}
	}
	if (ret)
		goto fail;
	if (namei) {
		for (index = 0; index < BAS_COUNTER_MAX; index++) {
			uint64_t value = 0;
			bool required = index != BAS_COUNTER_PASS;

			failure_stage = "policy-counter";
			ret = namei_ext_policy_counter(
				&policy, "build_action_sandboxing_counters",
				(uint32_t)index, &value);
			if (ret)
				goto fail;
			emit_counter(out, condition, repetition,
				     (unsigned int)index, value,
				     !required || value > 0);
			if (required && !value) {
				ret = -EIO;
				goto fail;
			}
		}
	}

fail:
	if (ret && !sample_failure_emitted)
		emit_failure(out, condition, repetition, failure_stage, ret);
	if (policy_loaded) {
		cleanup_ret = namei_ext_policy_destroy(&policy);
		if (cleanup_ret)
			emit_failure(out, condition, repetition, "policy-destroy",
				     cleanup_ret);
		if (!ret && cleanup_ret)
			ret = cleanup_ret;
	}
	if (sandbox_started) {
		cleanup_ret = stop_sandboxfs(&sandboxfs);
		if (cleanup_ret)
			emit_failure(out, condition, repetition, "sandboxfs-stop",
				     cleanup_ret);
		if (!ret && cleanup_ret)
			ret = cleanup_ret;
	}
	if (cgroup_a_created && rmdir(cgroup_a) && errno != ENOENT) {
		cleanup_ret = -errno;
		emit_failure(out, condition, repetition, "cgroup-a-remove",
			     cleanup_ret);
		if (!ret)
			ret = cleanup_ret;
	}
	if (cgroup_b_created && rmdir(cgroup_b) && errno != ENOENT) {
		cleanup_ret = -errno;
		emit_failure(out, condition, repetition, "cgroup-b-remove",
			     cleanup_ret);
		if (!ret)
			ret = cleanup_ret;
	}
	namei_ext_remove_tree(root);
	fprintf(out,
		"{\"event\":\"build-action-rq2-summary\","
		"\"condition\":\"%s\",\"repetition\":%u,"
		"\"scales\":%zu,\"samples_per_scale\":%u,"
		"\"completed_samples\":%u,\"pass\":%s}\n",
		condition, repetition, scale_count, samples, order_index,
		ret ? "false" : "true");
	fclose(out);
	return ret ? 1 : 0;
}

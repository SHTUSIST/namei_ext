// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include "namei_ext_harness.h"

#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/openat2.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct attached_policy {
	struct bpf_object *obj;
	int cgroup_fd;
	int prog_fd;
	bool attached;
};

static void emit_case(FILE *out, const char *name, bool pass, int err,
		      const char *detail)
{
	fprintf(out,
		"{\"event\":\"case\",\"name\":\"%s\",\"pass\":%s,"
		"\"errno\":%d,\"detail\":\"%s\"}\n",
		name, pass ? "true" : "false", err, detail);
	fflush(out);
}

static int write_file(const char *path, const char *value)
{
	int fd;
	ssize_t len;

	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0)
		return -errno;
	len = write(fd, value, strlen(value));
	if (close(fd) && len >= 0)
		return -errno;
	if (len < 0)
		return -errno;
	return 0;
}

static int expect_stat(const char *name, const char *path, int want_errno,
		       FILE *out)
{
	struct stat st;
	int ret;

	errno = 0;
	ret = stat(path, &st);
	if (!want_errno && ret == 0) {
		emit_case(out, name, true, 0, "stat matched");
		return 0;
	}
	if (want_errno && ret < 0 && errno == want_errno) {
		emit_case(out, name, true, errno, "stat errno matched");
		return 0;
	}
	emit_case(out, name, false, errno, "stat mismatch");
	return -1;
}

static int expect_open(const char *name, const char *path, int want_errno,
		       FILE *out)
{
	int fd;

	errno = 0;
	fd = open(path, O_RDONLY);
	if (!want_errno && fd >= 0) {
		close(fd);
		emit_case(out, name, true, 0, "open matched");
		return 0;
	}
	if (fd >= 0)
		close(fd);
	if (want_errno && fd < 0 && errno == want_errno) {
		emit_case(out, name, true, errno, "open errno matched");
		return 0;
	}
	emit_case(out, name, false, errno, "open mismatch");
	return -1;
}

static int expect_open_flags(const char *name, const char *path, int flags,
			     int want_errno, FILE *out)
{
	int fd;

	errno = 0;
	fd = open(path, flags, 0600);
	if (!want_errno && fd >= 0) {
		close(fd);
		emit_case(out, name, true, 0, "open flags matched");
		return 0;
	}
	if (fd >= 0)
		close(fd);
	if (want_errno && fd < 0 && errno == want_errno) {
		emit_case(out, name, true, errno, "open flags errno matched");
		return 0;
	}
	emit_case(out, name, false, errno, "open flags mismatch");
	return -1;
}

static int expect_openat2_resolve(const char *name, int dirfd,
				  const char *path, uint64_t flags,
				  uint64_t resolve, int want_errno, FILE *out)
{
	struct open_how how = {
		.flags = flags,
		.resolve = resolve,
	};
	int fd;

	errno = 0;
	fd = syscall(SYS_openat2, dirfd, path, &how, sizeof(how));
	if (!want_errno && fd >= 0) {
		close(fd);
		emit_case(out, name, true, 0, "openat2 matched");
		return 0;
	}
	if (fd >= 0)
		close(fd);
	if (want_errno && fd < 0 && errno == want_errno) {
		emit_case(out, name, true, errno, "openat2 errno matched");
		return 0;
	}
	emit_case(out, name, false, errno, "openat2 mismatch");
	return -1;
}

static int expect_openat2_cached(const char *name, const char *path,
				 uint64_t flags, FILE *out)
{
	return expect_openat2_resolve(name, AT_FDCWD, path, flags,
				      RESOLVE_CACHED, 0, out);
}

static int expect_read_file(const char *name, const char *path,
			    const char *want, FILE *out)
{
	char buf[64] = {};
	ssize_t nread;
	int fd;
	int err;

	errno = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		emit_case(out, name, false, errno, "open for read failed");
		return -1;
	}
	nread = read(fd, buf, sizeof(buf) - 1);
	err = errno;
	close(fd);
	if (nread < 0) {
		emit_case(out, name, false, err, "read failed");
		return -1;
	}
	if (!strcmp(buf, want)) {
		emit_case(out, name, true, 0, "read content matched");
		return 0;
	}
	emit_case(out, name, false, 0, "read content mismatch");
	return -1;
}

static int expect_different_devices(const char *name, const char *first,
				    const char *second, FILE *out)
{
	struct stat first_st;
	struct stat second_st;

	if (stat(first, &first_st) || stat(second, &second_st)) {
		emit_case(out, name, false, errno, "stat devices failed");
		return -1;
	}
	if (first_st.st_dev != second_st.st_dev) {
		emit_case(out, name, true, 0, "devices differ");
		return 0;
	}
	emit_case(out, name, false, 0, "devices unexpectedly match");
	return -1;
}

static int register_target_path(const char *path, unsigned int target_id);

static bool read_matches_either(const char *path, const char *first,
				const char *second)
{
	char buf[64] = {};
	ssize_t nread;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	nread = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (nread < 0)
		return false;
	return !strcmp(buf, first) || !strcmp(buf, second);
}

static int expect_atomic_target_replacement(const char *name,
					    const char *path,
					    const char *first_target,
					    const char *second_target,
					    unsigned int target_id,
					    FILE *out)
{
	const unsigned int replacements = 128;
	int stop_pipe[2];
	int child_status;
	unsigned int i;
	pid_t child;
	char stop = 1;
	int err = 0;
	int flags;

	if (pipe(stop_pipe)) {
		emit_case(out, name, false, errno,
			  "stop pipe failed");
		return -1;
	}
	flags = fcntl(stop_pipe[0], F_GETFL);
	if (flags < 0 ||
	    fcntl(stop_pipe[0], F_SETFL, flags | O_NONBLOCK)) {
		err = errno;
		close(stop_pipe[0]);
		close(stop_pipe[1]);
		emit_case(out, name, false, err,
			  "set nonblocking stop pipe failed");
		return -1;
	}

	child = fork();
	if (child < 0) {
		err = errno;
		close(stop_pipe[0]);
		close(stop_pipe[1]);
		emit_case(out, name, false, err,
			  "reader fork failed");
		return -1;
	}
	if (child == 0) {
		bool failed = false;

		close(stop_pipe[1]);
		for (;;) {
			ssize_t nread;

			if (!failed &&
			    !read_matches_either(path, "selected\n",
						 "replacement\n"))
				failed = true;
			nread = read(stop_pipe[0], &stop, sizeof(stop));
			if (nread == (ssize_t)sizeof(stop))
				break;
			if (nread < 0 && errno != EAGAIN &&
			    errno != EWOULDBLOCK)
				failed = true;
		}
		close(stop_pipe[0]);
		_exit(failed ? 1 : 0);
	}

	close(stop_pipe[0]);
	for (i = 0; i < replacements; i++) {
		const char *target = i & 1 ? first_target : second_target;

		err = register_target_path(target, target_id);
		if (err)
			break;
	}
	if (write(stop_pipe[1], &stop, sizeof(stop)) !=
	    (ssize_t)sizeof(stop) && !err)
		err = -errno;
	close(stop_pipe[1]);
	if (waitpid(child, &child_status, 0) < 0) {
		if (!err)
			err = -errno;
	} else if ((!WIFEXITED(child_status) ||
		    WEXITSTATUS(child_status)) && !err) {
		err = -EIO;
	}

	if (err) {
		emit_case(out, name, false, -err,
			  "reader observed invalid target state");
		return -1;
	}
	emit_case(out, name, true, 0,
		  "each read observed one complete registered target");
	return 0;
}

static int expect_access(const char *name, const char *path, int mode,
			 int want_errno, FILE *out)
{
	int ret;

	errno = 0;
	ret = access(path, mode);
	if (!want_errno && ret == 0) {
		emit_case(out, name, true, 0, "access matched");
		return 0;
	}
	if (want_errno && ret < 0 && errno == want_errno) {
		emit_case(out, name, true, errno, "access errno matched");
		return 0;
	}
	emit_case(out, name, false, errno, "access mismatch");
	return -1;
}

static int expect_unprivileged_open(const char *name, const char *path,
				    int want_errno, FILE *out)
{
	struct {
		int ret;
		int err;
	} result = {};
	int pipefd[2];
	int status;
	pid_t pid;
	ssize_t nread;

	if (pipe(pipefd)) {
		emit_case(out, name, false, errno, "pipe failed");
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		int err = errno;

		close(pipefd[0]);
		close(pipefd[1]);
		emit_case(out, name, false, err, "fork failed");
		return -1;
	}
	if (!pid) {
		int fd;

		close(pipefd[0]);
		if (setgid(65534) || setuid(65534)) {
			result.ret = -1;
			result.err = errno;
		} else {
			errno = 0;
			fd = open(path, O_RDONLY | O_CLOEXEC);
			result.ret = fd < 0 ? -1 : 0;
			result.err = errno;
			if (fd >= 0)
				close(fd);
		}
		if (write(pipefd[1], &result, sizeof(result)) != sizeof(result))
			_exit(2);
		close(pipefd[1]);
		_exit(0);
	}

	close(pipefd[1]);
	nread = read(pipefd[0], &result, sizeof(result));
	close(pipefd[0]);
	if (waitpid(pid, &status, 0) < 0 ||
	    nread != sizeof(result) || !WIFEXITED(status) ||
	    WEXITSTATUS(status)) {
		emit_case(out, name, false, errno,
			  "unprivileged result collection failed");
		return -1;
	}
	if (!want_errno && result.ret == 0) {
		emit_case(out, name, true, 0, "unprivileged open succeeded");
		return 0;
	}
	if (want_errno && result.ret < 0 && result.err == want_errno) {
		emit_case(out, name, true, result.err,
			  "unprivileged open errno matched");
		return 0;
	}
	emit_case(out, name, false, result.err,
		  "unprivileged open mismatch");
	return -1;
}

static int expect_exec_errno(const char *name, const char *path,
			     int want_errno, FILE *out)
{
	char *const argv[] = { (char *)path, NULL };
	char *const envp[] = { NULL };
	int pipefd[2];
	int status;
	int err = 0;
	ssize_t nread;
	pid_t pid;

	if (pipe(pipefd)) {
		emit_case(out, name, false, errno, "pipe failed");
		return -1;
	}
	if (fcntl(pipefd[1], F_SETFD, FD_CLOEXEC)) {
		err = errno;
		close(pipefd[0]);
		close(pipefd[1]);
		emit_case(out, name, false, err, "fcntl failed");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		err = errno;
		close(pipefd[0]);
		close(pipefd[1]);
		emit_case(out, name, false, err, "fork failed");
		return -1;
	}

	if (pid == 0) {
		close(pipefd[0]);
		execve(path, argv, envp);
		err = errno;
		if (write(pipefd[1], &err, sizeof(err)) != (ssize_t)sizeof(err))
			_exit(126);
		_exit(127);
	}

	close(pipefd[1]);
	nread = read(pipefd[0], &err, sizeof(err));
	close(pipefd[0]);
	if (waitpid(pid, &status, 0) < 0) {
		emit_case(out, name, false, errno, "waitpid failed");
		return -1;
	}

	if (!want_errno && nread == 0 && WIFEXITED(status) &&
	    WEXITSTATUS(status) == 0) {
		emit_case(out, name, true, 0, "execve succeeded");
		return 0;
	}
	if (want_errno && nread == (ssize_t)sizeof(err) &&
	    err == want_errno) {
		emit_case(out, name, true, err, "execve errno matched");
		return 0;
	}
	emit_case(out, name, false, nread == (ssize_t)sizeof(err) ? err : 0,
		  "execve mismatch");
	return -1;
}

static int expect_readdir(const char *path, FILE *out)
{
	bool saw_native = false;
	bool saw_alias = false;
	bool saw_backing = false;
	struct dirent *de;
	DIR *dir;

	errno = 0;
	dir = opendir(path);
	if (!dir) {
		emit_case(out, "readdir_open", false, errno, "opendir failed");
		return -1;
	}

	while ((de = readdir(dir))) {
		if (!strcmp(de->d_name, "native"))
			saw_native = true;
		if (!strcmp(de->d_name, "tool"))
			saw_alias = true;
		if (!strcmp(de->d_name, "tool.real"))
			saw_backing = true;
	}
	if (errno) {
		emit_case(out, "readdir_scan", false, errno, "readdir failed");
		closedir(dir);
		return -1;
	}
	closedir(dir);

	if (saw_native && saw_alias && !saw_backing) {
		emit_case(out, "readdir_view", true, 0,
			  "native and alias listed, backing name rewritten");
		return 0;
	}
	emit_case(out, "readdir_view", false, 0, "unexpected directory view");
	return -1;
}

static int expect_readdir_hidden(const char *path, FILE *out)
{
	bool saw_native = false;
	bool saw_secret = false;
	bool saw_backing = false;
	struct dirent *de;
	DIR *dir;

	errno = 0;
	dir = opendir(path);
	if (!dir) {
		emit_case(out, "hide_readdir_open", false, errno,
			  "opendir failed");
		return -1;
	}

	while ((de = readdir(dir))) {
		if (!strcmp(de->d_name, "native"))
			saw_native = true;
		if (!strcmp(de->d_name, "secret"))
			saw_secret = true;
		if (!strcmp(de->d_name, "tool.real"))
			saw_backing = true;
	}
	if (errno) {
		emit_case(out, "hide_readdir_scan", false, errno,
			  "readdir failed");
		closedir(dir);
		return -1;
	}
	closedir(dir);

	if (saw_native && !saw_secret && saw_backing) {
		emit_case(out, "hide_readdir_view", true, 0,
			  "native and backing listed, secret hidden");
		return 0;
	}
	emit_case(out, "hide_readdir_view", false, 0,
		  "unexpected hidden directory view");
	return -1;
}

static int expect_dirent(const char *name, const char *path,
			 const char *entry, bool want_visible, FILE *out)
{
	bool visible = false;
	struct dirent *de;
	DIR *dir;
	int err = 0;

	errno = 0;
	dir = opendir(path);
	if (!dir) {
		emit_case(out, name, false, errno, "opendir failed");
		return -1;
	}
	while ((de = readdir(dir))) {
		if (!strcmp(de->d_name, entry))
			visible = true;
	}
	if (errno)
		err = errno;
	closedir(dir);
	if (!err && visible == want_visible) {
		emit_case(out, name, true, 0, "directory visibility matched");
		return 0;
	}
	emit_case(out, name, false, err, "directory visibility mismatch");
	return -1;
}

static int expect_select_readdir(const char *path, FILE *out)
{
	bool saw_payload = false;
	struct dirent *de;
	DIR *dir;

	errno = 0;
	dir = opendir(path);
	if (!dir) {
		emit_case(out, "select_readdir_open", false, errno,
			  "opendir selected target failed");
		return -1;
	}

	while ((de = readdir(dir))) {
		if (!strcmp(de->d_name, "payload"))
			saw_payload = true;
	}
	if (errno) {
		emit_case(out, "select_readdir_scan", false, errno,
			  "readdir selected target failed");
		closedir(dir);
		return -1;
	}
	closedir(dir);

	if (saw_payload) {
		emit_case(out, "select_readdir_view", true, 0,
			  "selected target directory entries visible");
		return 0;
	}
	emit_case(out, "select_readdir_view", false, 0,
		  "selected target payload not listed");
	return -1;
}

static int register_target_path_flags(const char *path, unsigned int target_id,
				      int flags)
{
	char register_buf[64];
	int register_fd;
	int target_fd;
	ssize_t nwritten;
	int len;
	int err = 0;

	target_fd = open(path, flags);
	if (target_fd < 0)
		return -errno;
	register_fd = open("/sys/kernel/debug/namei_ext/register_target",
			   O_WRONLY | O_CLOEXEC);
	if (register_fd < 0) {
		err = -errno;
		goto out_close_target;
	}
	len = snprintf(register_buf, sizeof(register_buf), "%u %d\n",
		       target_id, target_fd);
	if (len < 0 || (size_t)len >= sizeof(register_buf)) {
		err = -EOVERFLOW;
		goto out_close_register;
	}
	nwritten = write(register_fd, register_buf, len);
	if (nwritten != (ssize_t)len)
		err = errno ? -errno : -EIO;

out_close_register:
	close(register_fd);
out_close_target:
	close(target_fd);
	return err;
}

static int register_target_path(const char *path, unsigned int target_id)
{
	return register_target_path_flags(path, target_id,
					  O_PATH | O_CLOEXEC);
}

static int clear_targets(FILE *out, const char *name)
{
	const char clear_cmd[] = "clear\n";
	int register_fd;
	ssize_t nwritten;

	register_fd = open("/sys/kernel/debug/namei_ext/register_target",
			   O_WRONLY | O_CLOEXEC);
	if (register_fd < 0) {
		emit_case(out, name, false, errno, "open target registry failed");
		return -1;
	}

	nwritten = write(register_fd, clear_cmd, strlen(clear_cmd));
	if (nwritten != (ssize_t)strlen(clear_cmd)) {
		emit_case(out, name, false, errno, "target clear failed");
		close(register_fd);
		return -1;
	}

	close(register_fd);
	emit_case(out, name, true, 0, "target registry cleared");
	return 0;
}

static int policy_parent_command(FILE *out, const char *name,
				 const char *command, const char *parent)
{
	char command_buf[64];
	int parent_fd = -1;
	int control_fd;
	ssize_t nwritten;
	int len;

	if (parent) {
		parent_fd = open(parent, O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (parent_fd < 0) {
			emit_case(out, name, false, errno,
				  "open policy parent failed");
			return -1;
		}
		len = snprintf(command_buf, sizeof(command_buf), "%s %d\n",
			       command, parent_fd);
	} else {
		len = snprintf(command_buf, sizeof(command_buf), "%s\n",
			       command);
	}
	if (len < 0 || (size_t)len >= sizeof(command_buf)) {
		if (parent_fd >= 0)
			close(parent_fd);
		emit_case(out, name, false, EOVERFLOW,
			  "policy parent command overflow");
		return -1;
	}
	control_fd = open("/sys/kernel/debug/namei_ext/policy_parent",
			  O_WRONLY | O_CLOEXEC);
	if (control_fd < 0) {
		if (parent_fd >= 0)
			close(parent_fd);
		emit_case(out, name, false, errno,
			  "open policy parent control failed");
		return -1;
	}
	nwritten = write(control_fd, command_buf, len);
	close(control_fd);
	if (parent_fd >= 0)
		close(parent_fd);
	if (nwritten != len) {
		emit_case(out, name, false, errno,
			  "policy parent command failed");
		return -1;
	}
	emit_case(out, name, true, 0, "policy parent command applied");
	return 0;
}

struct inherited_scope_result {
	int move_ret;
	int move_errno;
	int clear_ret;
	int clear_errno;
	int stat_ret;
	int stat_errno;
};

static int expect_inherited_scope(FILE *out, const char *cgroup_root,
				  const char *secret)
{
	struct inherited_scope_result result = {};
	char child_cgroup[PATH_MAX];
	char cgroup_procs[PATH_MAX + 32];
	int pipefd[2];
	int status;
	pid_t pid;
	ssize_t nread;
	int len;

	len = snprintf(child_cgroup, sizeof(child_cgroup),
		       "%s/namei-ext-scope-%ld", cgroup_root, (long)getpid());
	if (len < 0 || (size_t)len >= sizeof(child_cgroup)) {
		emit_case(out, "scope_inherit_setup", false, ENAMETOOLONG,
			  "child cgroup path overflow");
		return -1;
	}
	len = snprintf(cgroup_procs, sizeof(cgroup_procs), "%s/cgroup.procs",
		       child_cgroup);
	if (len < 0 || (size_t)len >= sizeof(cgroup_procs)) {
		emit_case(out, "scope_inherit_setup", false, ENAMETOOLONG,
			  "cgroup.procs path overflow");
		return -1;
	}
	if (mkdir(child_cgroup, 0755)) {
		emit_case(out, "scope_inherit_setup", false, errno,
			  "child cgroup creation failed");
		return -1;
	}
	if (pipe(pipefd)) {
		emit_case(out, "scope_inherit_setup", false, errno,
			  "result pipe failed");
		rmdir(child_cgroup);
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		emit_case(out, "scope_inherit_setup", false, errno,
			  "child fork failed");
		close(pipefd[0]);
		close(pipefd[1]);
		rmdir(child_cgroup);
		return -1;
	}
	if (!pid) {
		const char clear[] = "clear\n";
		struct stat st;
		int fd;

		close(pipefd[0]);
		fd = open(cgroup_procs, O_WRONLY | O_CLOEXEC);
		if (fd < 0) {
			result.move_ret = -1;
			result.move_errno = errno;
		} else {
			result.move_ret =
				write(fd, "0\n", 2) == 2 ? 0 : -1;
			result.move_errno = result.move_ret ? errno : 0;
			close(fd);
		}
		if (!result.move_ret) {
			fd = open("/sys/kernel/debug/namei_ext/policy_parent",
				  O_WRONLY | O_CLOEXEC);
			if (fd < 0) {
				result.clear_ret = -1;
				result.clear_errno = errno;
			} else {
				errno = 0;
				result.clear_ret = write(fd, clear,
							 strlen(clear));
				result.clear_errno = errno;
				close(fd);
			}
			errno = 0;
			result.stat_ret = stat(secret, &st);
			result.stat_errno = errno;
		}
		if (write(pipefd[1], &result, sizeof(result)) != sizeof(result))
			_exit(2);
		close(pipefd[1]);
		_exit(0);
	}

	close(pipefd[1]);
	nread = read(pipefd[0], &result, sizeof(result));
	close(pipefd[0]);
	if (waitpid(pid, &status, 0) < 0 ||
	    nread != sizeof(result) || !WIFEXITED(status) ||
	    WEXITSTATUS(status)) {
		emit_case(out, "scope_inherit_setup", false, errno,
			  "child result collection failed");
		rmdir(child_cgroup);
		return -1;
	}
	if (rmdir(child_cgroup)) {
		emit_case(out, "scope_inherit_setup", false, errno,
			  "child cgroup removal failed");
		return -1;
	}
	if (!result.move_ret) {
		emit_case(out, "scope_inherit_setup", true, 0,
			  "child entered inherited policy cgroup");
	} else {
		emit_case(out, "scope_inherit_setup", false,
			  result.move_errno, "child cgroup move failed");
		return -1;
	}
	if (result.clear_ret < 0 && result.clear_errno == ENOENT) {
		emit_case(out, "scope_inherit_clear_rejected", true,
			  result.clear_errno,
			  "child cannot override ancestor policy scope");
	} else {
		emit_case(out, "scope_inherit_clear_rejected", false,
			  result.clear_errno,
			  "child changed inherited policy scope");
		return -1;
	}
	if (result.stat_ret < 0 && result.stat_errno == ENOENT) {
		emit_case(out, "scope_inherit_policy_enforced", true,
			  result.stat_errno,
			  "ancestor policy remains effective in child");
		return 0;
	}
	emit_case(out, "scope_inherit_policy_enforced", false,
		  result.stat_errno, "ancestor policy was bypassed");
	return -1;
}

static unsigned long long ptr_to_u64(const void *ptr)
{
	return (unsigned long long)(uintptr_t)ptr;
}

static int raw_namei_ext_prog_load(enum bpf_attach_type attach_type, int *err_out)
{
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, BPF_NAMEI_EXT_PASS),
		BPF_EXIT_INSN(),
	};
	char log_buf[8192] = {};
	char license[] = "GPL";
	union bpf_attr attr = {};
	int fd;

	attr.prog_type = BPF_PROG_TYPE_NAMEI_EXT;
	attr.expected_attach_type = attach_type;
	attr.insn_cnt = sizeof(insns) / sizeof(insns[0]);
	attr.insns = ptr_to_u64(insns);
	attr.license = ptr_to_u64(license);
	attr.log_buf = ptr_to_u64(log_buf);
	attr.log_size = sizeof(log_buf);
	attr.log_level = 1;
	memcpy(attr.prog_name, "namei_ext_probe", sizeof("namei_ext_probe"));

	fd = syscall(__NR_bpf, BPF_PROG_LOAD, &attr, sizeof(attr));
	if (fd < 0 && err_out)
		*err_out = errno;
	return fd;
}

static int raw_namei_ext_prog_attach(int prog_fd, int cgroup_fd,
				     unsigned int flags)
{
	union bpf_attr attr = {};

	attr.target_fd = cgroup_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type = BPF_CGROUP_NAMEI_EXT;
	attr.attach_flags = flags;
	return syscall(__NR_bpf, BPF_PROG_ATTACH, &attr, sizeof(attr));
}

static int raw_namei_ext_prog_detach(int prog_fd, int cgroup_fd,
				     unsigned int flags)
{
	union bpf_attr attr = {};

	attr.target_fd = cgroup_fd;
	attr.attach_bpf_fd = prog_fd;
	attr.attach_type = BPF_CGROUP_NAMEI_EXT;
	attr.attach_flags = flags;
	return syscall(__NR_bpf, BPF_PROG_DETACH, &attr, sizeof(attr));
}

static int expect_attach_flag_rejected(FILE *out, const char *name,
				       int prog_fd, int cgroup_fd,
				       unsigned int flags)
{
	int ret;
	int err;

	errno = 0;
	ret = raw_namei_ext_prog_attach(prog_fd, cgroup_fd, flags);
	err = errno;
	if (ret < 0 && err == EINVAL) {
		emit_case(out, name, true, err, "attach flag rejected");
		return 0;
	}
	if (ret == 0)
		raw_namei_ext_prog_detach(prog_fd, cgroup_fd, 0);
	emit_case(out, name, false, err, "attach flag was not rejected");
	return -1;
}

static int expect_attach_abi(FILE *out, const char *cgroup_path)
{
	union bpf_attr attr = {};
	int cgroup_fd = -1;
	int err = 0;
	int fd;
	int ret;
	int fails = 0;

	fd = raw_namei_ext_prog_load(BPF_CGROUP_NAMEI_EXT, &err);
	if (fd >= 0) {
		emit_case(out, "load_good_attach_type", true, 0,
			  "expected attach type accepted");
		close(fd);
	} else {
		emit_case(out, "load_good_attach_type", false, err,
			  "expected attach type rejected");
		fails++;
	}

	err = 0;
	fd = raw_namei_ext_prog_load(BPF_CGROUP_INET_INGRESS, &err);
	if (fd < 0 && err == EINVAL) {
		emit_case(out, "load_bad_attach_type", true, err,
			  "wrong attach type rejected");
	} else {
		if (fd >= 0)
			close(fd);
		emit_case(out, "load_bad_attach_type", false, err,
			  "wrong attach type was not rejected");
		fails++;
	}

	fd = raw_namei_ext_prog_load(BPF_CGROUP_NAMEI_EXT, &err);
	if (fd < 0) {
		emit_case(out, "multi_attach_rejected", false, err,
			  "probe load failed");
		emit_case(out, "override_attach_rejected", false, err,
			  "probe load failed");
		emit_case(out, "replace_attach_rejected", false, err,
			  "probe load failed");
		emit_case(out, "link_create_rejected", false, err,
			  "probe load failed");
		return fails + 4;
	}

	cgroup_fd = open(cgroup_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (cgroup_fd < 0) {
		err = errno;
		close(fd);
		emit_case(out, "multi_attach_rejected", false, err,
			  "open cgroup failed");
		emit_case(out, "override_attach_rejected", false, err,
			  "open cgroup failed");
		emit_case(out, "replace_attach_rejected", false, err,
			  "open cgroup failed");
		emit_case(out, "link_create_rejected", false, err,
			  "open cgroup failed");
		return fails + 4;
	}

	fails += !!expect_attach_flag_rejected(out, "multi_attach_rejected",
					      fd, cgroup_fd,
					      BPF_F_ALLOW_MULTI);
	fails += !!expect_attach_flag_rejected(out, "override_attach_rejected",
					      fd, cgroup_fd,
					      BPF_F_ALLOW_OVERRIDE);
	fails += !!expect_attach_flag_rejected(out, "replace_attach_rejected",
					      fd, cgroup_fd,
					      BPF_F_REPLACE);

	attr.link_create.prog_fd = fd;
	attr.link_create.target_fd = cgroup_fd;
	attr.link_create.attach_type = BPF_CGROUP_NAMEI_EXT;
	errno = 0;
	ret = syscall(__NR_bpf, BPF_LINK_CREATE, &attr, sizeof(attr));
	err = errno;
	if (ret < 0 && err == EOPNOTSUPP) {
		emit_case(out, "link_create_rejected", true, err,
			  "cgroup link attach rejected");
	} else {
		if (ret >= 0)
			close(ret);
		emit_case(out, "link_create_rejected", false, err,
			  "cgroup link attach was not rejected");
		fails++;
	}

	close(cgroup_fd);
	close(fd);
	return fails;
}

static int load_and_attach(const char *obj_path, const char *cgroup_path,
			   struct attached_policy *policy)
{
	struct bpf_program *prog;
	struct bpf_object *obj;
	int cgroup_fd;
	int prog_fd;
	int err;

	obj = bpf_object__open_file(obj_path, NULL);
	err = libbpf_get_error(obj);
	if (err) {
		errno = -err;
		return -1;
	}

	err = bpf_object__load(obj);
	if (err) {
		errno = -err;
		goto err_close_obj;
	}

	prog = bpf_object__next_program(obj, NULL);
	if (!prog) {
		errno = EINVAL;
		goto err_close_obj;
	}

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		errno = EINVAL;
		goto err_close_obj;
	}

	cgroup_fd = open(cgroup_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (cgroup_fd < 0)
		goto err_close_obj;

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_NAMEI_EXT, 0);
	if (err) {
		errno = -err;
		goto err_close_cgroup;
	}

	policy->obj = obj;
	policy->cgroup_fd = cgroup_fd;
	policy->prog_fd = prog_fd;
	policy->attached = true;
	return 0;

err_close_cgroup:
	close(cgroup_fd);
err_close_obj:
	bpf_object__close(obj);
	return -1;
}

static int destroy_policy(struct attached_policy *policy)
{
	int err = 0;

	if (policy->attached) {
		err = bpf_prog_detach2(policy->prog_fd, policy->cgroup_fd,
				       BPF_CGROUP_NAMEI_EXT);
		policy->attached = false;
	}
	if (policy->cgroup_fd >= 0)
		close(policy->cgroup_fd);
	bpf_object__close(policy->obj);
	policy->obj = NULL;
	policy->cgroup_fd = -1;
	policy->prog_fd = -1;
	return err;
}

static int monotonic_ns(uint64_t *value)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return -errno;
	*value = (uint64_t)now.tv_sec * 1000000000ULL +
		(uint64_t)now.tv_nsec;
	return 0;
}

static int observe_select_targets(FILE *out, const char *phase,
				  const char *cgroup_root,
				  const char *select_obj_path,
				  const char *portal_payload,
				  const char *selected_file,
				  const char *selected_exec,
				  const char *selected_pinned_file,
				  unsigned int present_mask)
{
	const char *paths[] = {
		portal_payload,
		selected_file,
		selected_exec,
		selected_pinned_file,
	};
	const char *labels[] = { "directory", "file", "exec", "pinned" };
	struct attached_policy policy = {
		.cgroup_fd = -1,
		.prog_fd = -1,
	};
	char name[128];
	unsigned int index;
	int failures = 0;
	int err;

	if (load_and_attach(select_obj_path, cgroup_root, &policy)) {
		snprintf(name, sizeof(name), "%s-attach", phase);
		emit_case(out, name, false, errno, "select policy attach failed");
		return 1;
	}
	for (index = 0; index < sizeof(paths) / sizeof(paths[0]); index++) {
		if (snprintf(name, sizeof(name), "%s-%s", phase,
			     labels[index]) < 0) {
			failures++;
			continue;
		}
		failures += !!expect_stat(name, paths[index],
					  present_mask & (1U << index) ? 0 : ENOENT,
					  out);
	}
	err = destroy_policy(&policy);
	if (snprintf(name, sizeof(name), "%s-detach", phase) < 0)
		return failures + 1;
	emit_case(out, name, !err, err < 0 ? -err : err,
		  err ? "select policy detach failed" :
		  "select policy detached");
	return failures + !!err;
}

static int exercise_target_batch(FILE *out, const char *cgroup_root,
				 const char *functional_root,
				 const char *select_obj_path,
				 const char *target_dir,
				 const char *selected_file_target,
				 const char *selected_exec_target,
				 const char *selected_pinned_target,
				 const char *portal_payload,
				 const char *selected_file,
				 const char *selected_exec,
				 const char *selected_pinned_file)
{
	struct namei_ext_target_registration targets[] = {
		{ .path = target_dir, .target_id = 1 },
		{ .path = selected_file_target, .target_id = 2 },
		{ .path = selected_exec_target, .target_id = 3 },
		{ .path = selected_pinned_target, .target_id = 4 },
	};
	struct namei_ext_target_registration partial[2] = {
		{ .path = target_dir, .target_id = 1 },
	};
	char cgroup[PATH_MAX];
	char missing[PATH_MAX];
	bool registry_dirty = false;
	bool moved = false;
	int failures = 0;
	int ret;

	if (snprintf(cgroup, sizeof(cgroup), "%s/namei-ext-batch-functional-%ld",
		     cgroup_root, (long)getpid()) < 0 ||
	    snprintf(missing, sizeof(missing), "%s/batch-missing", functional_root) < 0)
		return 1;
	partial[1].path = missing;
	partial[1].target_id = 2;
	if (mkdir(cgroup, 0755)) {
		emit_case(out, "target_batch_cgroup_create", false, errno,
			  "batch test cgroup creation failed");
		return 1;
	}
	ret = namei_ext_move_self_to_cgroup(cgroup);
	if (ret) {
		emit_case(out, "target_batch_cgroup_enter", false, -ret,
			  "batch test cgroup move failed");
		failures++;
		goto out;
	}
	moved = true;

	ret = namei_ext_register_target_batch(cgroup, partial, 2);
	registry_dirty = true;
	if (ret) {
		emit_case(out, "target_batch_partial_failure", true, -ret,
			  "missing second path rejected after successful prefix");
	} else {
		emit_case(out, "target_batch_partial_failure", false, 0,
			  "missing second path unexpectedly registered");
		failures++;
	}
	failures += observe_select_targets(
		out, "target-batch-partial-prefix", cgroup_root, select_obj_path,
		portal_payload, selected_file, selected_exec, selected_pinned_file,
		1U);
	ret = namei_ext_clear_targets(cgroup);
	emit_case(out, "target_batch_partial_clear", !ret,
		  ret < 0 ? -ret : ret,
		  ret ? "partial registration clear failed" :
		  "partial registration cleared");
	if (ret) {
		failures++;
		goto out;
	}
	registry_dirty = false;
	failures += observe_select_targets(
		out, "target-batch-partial-clear", cgroup_root, select_obj_path,
		portal_payload, selected_file, selected_exec, selected_pinned_file,
		0U);

	registry_dirty = true;
	ret = namei_ext_register_target_batch(
		cgroup, targets, sizeof(targets) / sizeof(targets[0]));
	emit_case(out, "target_batch_multi_success", !ret,
		  ret < 0 ? -ret : ret,
		  ret ? "multi-target batch failed" :
		  "multi-target batch registered through one control process");
	if (ret) {
		failures++;
		goto out;
	}
	failures += observe_select_targets(
		out, "target-batch-visible", cgroup_root, select_obj_path,
		portal_payload, selected_file, selected_exec, selected_pinned_file,
		0xfU);
	ret = namei_ext_clear_targets(cgroup);
	emit_case(out, "target_batch_multi_clear", !ret,
		  ret < 0 ? -ret : ret,
		  ret ? "multi-target clear failed" : "multi-target batch cleared");
	if (ret) {
		failures++;
		goto out;
	}
	registry_dirty = false;
	failures += observe_select_targets(
		out, "target-batch-cleared", cgroup_root, select_obj_path,
		portal_payload, selected_file, selected_exec, selected_pinned_file,
		0U);

	registry_dirty = true;
	ret = namei_ext_register_target_batch(
		cgroup, targets, sizeof(targets) / sizeof(targets[0]));
	emit_case(out, "target_batch_id_reuse", !ret,
		  ret < 0 ? -ret : ret,
		  ret ? "target ID reuse failed" : "cleared target IDs reused");
	if (ret) {
		failures++;
		goto out;
	}
	failures += observe_select_targets(
		out, "target-batch-reused-visible", cgroup_root, select_obj_path,
		portal_payload, selected_file, selected_exec, selected_pinned_file,
		0xfU);

out:
	if (registry_dirty) {
		ret = namei_ext_clear_targets(cgroup);
		emit_case(out, "target_batch_cleanup_clear", !ret,
			  ret < 0 ? -ret : ret,
			  ret ? "batch cleanup clear failed" :
			  "batch cleanup clear completed");
		failures += !!ret;
	}
	if (moved) {
		ret = namei_ext_move_self_to_cgroup(cgroup_root);
		emit_case(out, "target_batch_cgroup_leave", !ret,
			  ret < 0 ? -ret : ret,
			  ret ? "return to root cgroup failed" :
			  "returned to root cgroup");
		failures += !!ret;
	}
	ret = rmdir(cgroup);
	emit_case(out, "target_batch_cgroup_remove", !ret,
		  ret ? errno : 0,
		  ret ? "batch test cgroup removal failed" :
		  "batch test cgroup removed");
	return failures + !!ret;
}

#define TARGET_BATCH_DIAGNOSTIC_TARGETS 64U
#define TARGET_BATCH_DIAGNOSTIC_PAIRS 5U

static int run_registration_arm(FILE *out, const char *cgroup_root,
				const char *target_path, unsigned int pair,
				unsigned int order, bool batch,
				uint64_t *elapsed_out)
{
	struct namei_ext_target_registration
		targets[TARGET_BATCH_DIAGNOSTIC_TARGETS];
	char cgroup[PATH_MAX];
	uint64_t start = 0;
	uint64_t end = 0;
	unsigned int index;
	int register_error = 0;
	int timing_error;
	int clear_error;
	int remove_error;
	bool pass;

	if (snprintf(cgroup, sizeof(cgroup),
		     "%s/namei-ext-batch-diag-%ld-%u-%u-%s", cgroup_root,
		     (long)getpid(), pair, order, batch ? "batch" : "scalar") < 0)
		return 1;
	if (mkdir(cgroup, 0755))
		return 1;
	for (index = 0; index < TARGET_BATCH_DIAGNOSTIC_TARGETS; index++) {
		targets[index].path = target_path;
		targets[index].target_id = index + 1;
	}
	register_error = monotonic_ns(&start);
	if (!register_error) {
		if (batch) {
			register_error = namei_ext_register_target_batch(
				cgroup, targets, TARGET_BATCH_DIAGNOSTIC_TARGETS);
		} else {
			for (index = 0; index < TARGET_BATCH_DIAGNOSTIC_TARGETS;
			     index++) {
				register_error = namei_ext_register_target(
					cgroup, targets[index].path,
					targets[index].target_id);
				if (register_error)
					break;
			}
		}
	}
	timing_error = monotonic_ns(&end);
	if (timing_error && !register_error)
		register_error = timing_error;
	*elapsed_out = end >= start ? end - start : 0;
	clear_error = namei_ext_clear_targets(cgroup);
	remove_error = rmdir(cgroup) ? -errno : 0;
	pass = !register_error && !clear_error && !remove_error && *elapsed_out > 0;
	fprintf(out,
		"{\"event\":\"target-registration-diagnostic\","
		"\"pair\":%u,\"order\":%u,\"mechanism\":\"%s\","
		"\"targets\":%u,\"elapsed_ns\":%llu,"
		"\"register_error\":%d,\"clear_error\":%d,"
		"\"cgroup_remove_error\":%d,\"pass\":%s}\n",
		pair, order, batch ? "batch" : "scalar",
		TARGET_BATCH_DIAGNOSTIC_TARGETS,
		(unsigned long long)*elapsed_out,
		register_error < 0 ? -register_error : register_error,
		clear_error < 0 ? -clear_error : clear_error,
		remove_error < 0 ? -remove_error : remove_error,
		pass ? "true" : "false");
	fflush(out);
	return pass ? 0 : 1;
}

static int compare_double(const void *left, const void *right)
{
	double first = *(const double *)left;
	double second = *(const double *)right;

	return (first > second) - (first < second);
}

static int run_registration_diagnostic(FILE *out, const char *cgroup_root,
				       const char *target_path)
{
	double ratios[TARGET_BATCH_DIAGNOSTIC_PAIRS];
	double ordered[TARGET_BATCH_DIAGNOSTIC_PAIRS];
	unsigned int batch_wins = 0;
	unsigned int pair;
	int failures = 0;

	for (pair = 0; pair < TARGET_BATCH_DIAGNOSTIC_PAIRS; pair++) {
		uint64_t scalar_ns = 0;
		uint64_t batch_ns = 0;
		bool batch_first = pair & 1;
		int first_error;
		int second_error;

		if (batch_first) {
			first_error = run_registration_arm(
				out, cgroup_root, target_path, pair + 1, 1, true,
				&batch_ns);
			second_error = run_registration_arm(
				out, cgroup_root, target_path, pair + 1, 2, false,
				&scalar_ns);
		} else {
			first_error = run_registration_arm(
				out, cgroup_root, target_path, pair + 1, 1, false,
				&scalar_ns);
			second_error = run_registration_arm(
				out, cgroup_root, target_path, pair + 1, 2, true,
				&batch_ns);
		}
		if (first_error || second_error || !scalar_ns || !batch_ns) {
			failures++;
			break;
		}
		ratios[pair] = (double)batch_ns / (double)scalar_ns;
		if (batch_ns < scalar_ns)
			batch_wins++;
		fprintf(out,
			"{\"event\":\"target-registration-pair\","
			"\"pair\":%u,\"first\":\"%s\",\"targets\":%u,"
			"\"scalar_ns\":%llu,\"batch_ns\":%llu,"
			"\"batch_over_scalar\":%.9f,\"pass\":true}\n",
			pair + 1, batch_first ? "batch" : "scalar",
			TARGET_BATCH_DIAGNOSTIC_TARGETS,
			(unsigned long long)scalar_ns,
			(unsigned long long)batch_ns, ratios[pair]);
	}
	if (failures)
		return failures;
	memcpy(ordered, ratios, sizeof(ordered));
	qsort(ordered, TARGET_BATCH_DIAGNOSTIC_PAIRS, sizeof(ordered[0]),
	      compare_double);
	bool pass = ordered[TARGET_BATCH_DIAGNOSTIC_PAIRS / 2] < 1.0;

	fprintf(out,
		"{\"event\":\"target-registration-diagnostic-summary\","
		"\"pairs\":%u,\"targets\":%u,\"batch_wins\":%u,"
		"\"median_batch_over_scalar\":%.9f,\"pass\":%s}\n",
		TARGET_BATCH_DIAGNOSTIC_PAIRS, TARGET_BATCH_DIAGNOSTIC_TARGETS,
		batch_wins, ordered[TARGET_BATCH_DIAGNOSTIC_PAIRS / 2],
		pass ? "true" : "false");
	fflush(out);
	return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
	const char *cgroup_path = "/sys/fs/cgroup";
	const char *hide_obj_path = NULL;
	const char *select_obj_path = NULL;
	struct attached_policy policy = {
		.cgroup_fd = -1,
		.prog_fd = -1,
	};
	char root[] = "/tmp/namei-ext-functional-XXXXXX";
	char native[PATH_MAX];
	char alias[PATH_MAX];
	char backing[PATH_MAX];
	char secret[PATH_MAX];
	char visible[PATH_MAX];
	char portal[PATH_MAX];
	char portal_payload[PATH_MAX];
	char target_dir[PATH_MAX];
	char target_payload[PATH_MAX];
	char replacement_target_dir[PATH_MAX];
	char replacement_target_payload[PATH_MAX];
	char selected_file[PATH_MAX];
	char selected_file_child[PATH_MAX];
	char selected_file_link[PATH_MAX];
	char selected_target_mount[PATH_MAX];
	char selected_private_dir[PATH_MAX];
	char selected_file_target[PATH_MAX];
	char selected_file_replacement[PATH_MAX];
	char selected_exec[PATH_MAX];
	char selected_exec_target[PATH_MAX];
	char selected_pinned_file[PATH_MAX];
	char selected_pinned_target[PATH_MAX];
	char selected_symlink_target[PATH_MAX];
	char selected_fifo_target[PATH_MAX];
	char scope_other[PATH_MAX];
	char scope_other_secret[PATH_MAX];
	FILE *out;
	bool selected_target_mounted = false;
	bool targets_registered = false;
	int fails = 0;
	int setup_fails = 0;
	int setup_errno = 0;
	int selected_parent_fd = -1;
	int err;

	if (argc < 3 || argc > 6) {
		fprintf(stderr,
			"usage: %s REDIRECT_POLICY_BPF_O RESULT_JSONL [CGROUP] [HIDE_POLICY_BPF_O] [SELECT_POLICY_BPF_O]\n",
			argv[0]);
		return 2;
	}
	if (argc >= 4)
		cgroup_path = argv[3];
	if (argc == 5)
		hide_obj_path = argv[4];
	if (argc == 6) {
		hide_obj_path = argv[4];
		select_obj_path = argv[5];
	}

	out = fopen(argv[2], "a");
	if (!out) {
		perror("fopen result");
		return 2;
	}

	fails += expect_attach_abi(out, cgroup_path);

	if (!mkdtemp(root)) {
		emit_case(out, "mkdtemp", false, errno, "mkdtemp failed");
		fclose(out);
		return 1;
	}
	if (chmod(root, 0755)) {
		emit_case(out, "chmod_root", false, errno,
			  "functional root chmod failed");
		fclose(out);
		return 1;
	}

	snprintf(native, sizeof(native), "%s/native", root);
	snprintf(alias, sizeof(alias), "%s/tool", root);
	snprintf(backing, sizeof(backing), "%s/tool.real", root);
	snprintf(secret, sizeof(secret), "%s/secret", root);
	snprintf(visible, sizeof(visible), "%s/visible", root);
	snprintf(portal, sizeof(portal), "%s/visible/portal", root);
	snprintf(portal_payload, sizeof(portal_payload),
		 "%s/visible/portal/payload", root);
	snprintf(target_dir, sizeof(target_dir), "%s/target", root);
	snprintf(target_payload, sizeof(target_payload), "%s/target/payload", root);
	snprintf(replacement_target_dir, sizeof(replacement_target_dir),
		 "%s/target-replacement", root);
	snprintf(replacement_target_payload,
		 sizeof(replacement_target_payload),
		 "%s/target-replacement/payload", root);
	snprintf(selected_file, sizeof(selected_file),
		 "%s/visible/selected-file", root);
	snprintf(selected_file_child, sizeof(selected_file_child),
		 "%s/visible/selected-file/child", root);
	snprintf(selected_file_link, sizeof(selected_file_link),
		 "%s/selected-file-link", root);
	snprintf(selected_target_mount, sizeof(selected_target_mount),
		 "%s/selected-target-mount", root);
	snprintf(selected_private_dir, sizeof(selected_private_dir),
		 "%s/selected-target-mount/private", root);
	snprintf(selected_file_target, sizeof(selected_file_target),
		 "%s/selected-target-mount/private/selected-file-target", root);
	snprintf(selected_file_replacement, sizeof(selected_file_replacement),
		 "%s/selected-target-mount/selected-file-replacement", root);
	snprintf(selected_exec, sizeof(selected_exec),
		 "%s/visible/selected-exec", root);
	snprintf(selected_exec_target, sizeof(selected_exec_target),
		 "%s/selected-target-mount/selected-exec-target", root);
	snprintf(selected_pinned_file, sizeof(selected_pinned_file),
		 "%s/visible/pinned-file", root);
	snprintf(selected_pinned_target, sizeof(selected_pinned_target),
		 "%s/selected-target-mount/selected-pinned-target", root);
	snprintf(selected_symlink_target, sizeof(selected_symlink_target),
		 "%s/selected-target-mount/selected-symlink-target", root);
	snprintf(selected_fifo_target, sizeof(selected_fifo_target),
		 "%s/selected-target-mount/selected-fifo-target", root);
	snprintf(scope_other, sizeof(scope_other), "%s/other", root);
	snprintf(scope_other_secret, sizeof(scope_other_secret),
		 "%s/other/secret", root);

	err = write_file(native, "native\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	err = write_file(backing, "real-tool\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	err = write_file(secret, "secret\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	if (chmod(native, 0755)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (chmod(backing, 0755)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (mkdir(visible, 0755)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (mkdir(selected_target_mount, 0755)) {
		setup_errno = errno;
		setup_fails++;
	} else if (mount("tmpfs", selected_target_mount, "tmpfs",
			 MS_NOSUID | MS_NODEV, "size=1m")) {
		setup_errno = errno;
		setup_fails++;
	} else {
		selected_target_mounted = true;
	}
	if (mkdir(selected_private_dir, 0700)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (mkdir(target_dir, 0755)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (mkdir(replacement_target_dir, 0755)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (mkdir(scope_other, 0755)) {
		setup_errno = errno;
		setup_fails++;
	}
	err = write_file(target_payload, "selected\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	err = write_file(replacement_target_payload, "replacement\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	err = write_file(selected_file_target, "selected\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	err = write_file(selected_file_replacement, "replacement\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	err = write_file(selected_exec_target, "#!/bin/sh\nexit 0\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	if (chmod(selected_exec_target, 0755)) {
		setup_errno = errno;
		setup_fails++;
	}
	err = write_file(selected_pinned_target, "pinned\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	if (symlink(selected_file_replacement, selected_symlink_target)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (symlink("visible/selected-file", selected_file_link)) {
		setup_errno = errno;
		setup_fails++;
	}
	if (mkfifo(selected_fifo_target, 0600)) {
		setup_errno = errno;
		setup_fails++;
	}
	err = write_file(scope_other_secret, "other-secret\n");
	if (err) {
		setup_errno = -err;
		setup_fails++;
	}
	if (setup_fails) {
		fails += setup_fails;
		emit_case(out, "setup_files", false, setup_errno,
			  "file setup failed");
		goto cleanup;
	}
	emit_case(out, "setup_files", true, 0,
		  "native, backing, and secret files created");
	fails += !!expect_different_devices("select_cross_mount_setup",
					    visible, selected_file_target, out);

	fails += !!expect_stat("alias_before_attach", alias, ENOENT, out);
	fails += !!expect_stat("backing_before_attach", backing, 0, out);
	fails += !!expect_stat("secret_before_attach", secret, 0, out);
	fails += !!expect_stat("select_portal_before_attach", portal_payload,
			       ENOENT, out);
	fails += !!expect_stat("select_file_before_attach", selected_file,
			       ENOENT, out);
	fails += !!expect_stat("select_exec_before_attach", selected_exec,
			       ENOENT, out);
	fails += !!expect_stat("select_pinned_before_attach",
			       selected_pinned_file, ENOENT, out);

	if (hide_obj_path) {
		if (load_and_attach(hide_obj_path, cgroup_path, &policy)) {
			emit_case(out, "attach_hide_policy", false, errno,
				  "load or attach failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "attach_hide_policy", true, 0,
			  "hide policy attached");

		fails += !!expect_stat("hide_native_stat", native, 0, out);
		fails += !!expect_open("hide_native_open", native, 0, out);
		fails += !!expect_stat("hide_secret_stat", secret, ENOENT,
				       out);
		fails += !!expect_open("hide_secret_open", secret, ENOENT,
				       out);
		fails += !!expect_access("hide_secret_access", secret, F_OK,
					 ENOENT, out);
		fails += !!expect_stat("hide_backing_stat", backing, 0, out);
		fails += !!expect_readdir_hidden(root, out);
		fails += !!expect_inherited_scope(out, cgroup_path, secret);

		fails += !!policy_parent_command(out, "scope_exact_root",
						 "exact", root);
		fails += !!expect_stat("scope_exact_root_hidden", secret,
				       ENOENT, out);
		fails += !!expect_dirent("scope_exact_root_readdir", root,
					 "secret", false, out);
		fails += !!expect_stat("scope_exact_other_visible",
				       scope_other_secret, 0, out);
		fails += !!expect_dirent("scope_exact_other_readdir",
					 scope_other, "secret", true, out);

		fails += !!policy_parent_command(out, "scope_add_other", "add",
						 scope_other);
		fails += !!expect_stat("scope_add_other_hidden",
				       scope_other_secret, ENOENT, out);
		fails += !!expect_dirent("scope_add_other_readdir",
					 scope_other, "secret", false, out);

		fails += !!policy_parent_command(out, "scope_clear", "clear",
						 NULL);
		fails += !!expect_stat("scope_empty_root_visible", secret, 0,
				       out);
		fails += !!expect_stat("scope_empty_other_visible",
				       scope_other_secret, 0, out);
		fails += !!expect_dirent("scope_empty_root_readdir", root,
					 "secret", true, out);

		fails += !!policy_parent_command(out, "scope_global", "global",
						 NULL);
		fails += !!expect_stat("scope_global_root_hidden", secret,
				       ENOENT, out);
		fails += !!expect_stat("scope_global_other_hidden",
				       scope_other_secret, ENOENT, out);

		err = destroy_policy(&policy);
		if (err) {
			emit_case(out, "detach_hide_policy", false, -err,
				  "hide policy detach failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "detach_hide_policy", true, 0,
			  "hide policy detached");
		fails += !!expect_stat("secret_after_hide_detach", secret, 0,
				       out);
	}

	if (select_obj_path) {
		err = exercise_target_batch(
			out, cgroup_path, root, select_obj_path, target_dir,
			selected_file_target, selected_exec_target,
			selected_pinned_target, portal_payload, selected_file,
			selected_exec, selected_pinned_file);
		if (err) {
			fails++;
			goto cleanup;
		}
		err = run_registration_diagnostic(out, cgroup_path,
						  selected_file_target);
		if (err) {
			fails++;
			goto cleanup;
		}

		err = register_target_path(target_dir, 1);
		if (err) {
			emit_case(out, "select_register_target", false, -err,
				  "target registration failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_register_target", true, 0,
			  "target directory registered");
		targets_registered = true;
		err = register_target_path_flags(
			selected_symlink_target, 90,
			O_PATH | O_NOFOLLOW | O_CLOEXEC);
		if (err == -ELOOP) {
			emit_case(out, "select_register_symlink_rejected", true,
				  ELOOP, "symlink target rejected");
		} else {
			emit_case(out, "select_register_symlink_rejected", false,
				  err < 0 ? -err : 0,
				  "symlink target was not rejected");
			fails++;
			goto cleanup;
		}
		err = register_target_path(selected_fifo_target, 91);
		if (err == -EOPNOTSUPP) {
			emit_case(out, "select_register_fifo_rejected", true,
				  EOPNOTSUPP, "FIFO target rejected");
		} else {
			emit_case(out, "select_register_fifo_rejected", false,
				  err < 0 ? -err : 0,
				  "FIFO target was not rejected");
			fails++;
			goto cleanup;
		}
		err = register_target_path(selected_file_target, 2);
		if (err) {
			emit_case(out, "select_register_file_target", false,
				  -err, "file target registration failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_register_file_target", true, 0,
			  "file target registered");
		err = register_target_path(selected_exec_target, 3);
		if (err) {
			emit_case(out, "select_register_exec_target", false,
				  -err, "exec target registration failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_register_exec_target", true, 0,
			  "exec target registered");
		err = register_target_path(selected_pinned_target, 4);
		if (err) {
			emit_case(out, "select_register_pinned_target", false,
				  -err, "pinned target registration failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_register_pinned_target", true, 0,
			  "pinned target registered");
		if (chmod(selected_private_dir, 0000)) {
			emit_case(out, "select_private_parent_setup", false,
				  errno, "private target parent chmod failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_private_parent_setup", true, 0,
			  "physical target parent made inaccessible");

		if (load_and_attach(select_obj_path, cgroup_path, &policy)) {
			emit_case(out, "attach_select_policy", false, errno,
				  "load or attach failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "attach_select_policy", true, 0,
			  "select policy attached");
		selected_parent_fd =
			open(visible, O_PATH | O_DIRECTORY | O_CLOEXEC);
		if (selected_parent_fd < 0) {
			emit_case(out, "select_parent_fd", false, errno,
				  "logical parent open failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_parent_fd", true, 0,
			  "logical parent opened");

		fails += !!expect_stat("select_portal_final_stat", portal,
				       0, out);
		fails += !!expect_select_readdir(portal, out);
		fails += !!expect_stat("select_portal_payload_stat",
				       portal_payload, 0, out);
		fails += !!expect_open("select_portal_payload_open",
				       portal_payload, 0, out);
		fails += !!expect_read_file("select_portal_payload_read",
					    portal_payload, "selected\n", out);
		fails += !!expect_openat2_cached(
			"select_portal_final_cached", portal,
			O_PATH | O_DIRECTORY | O_CLOEXEC, out);
		fails += !!expect_openat2_cached(
			"select_portal_payload_cached", portal_payload,
			O_RDONLY | O_CLOEXEC, out);
		fails += !!expect_stat("select_file_final_stat", selected_file,
				       0, out);
		fails += !!expect_open("select_file_final_open", selected_file,
				       0, out);
		fails += !!expect_read_file("select_file_final_read",
					    selected_file, "selected\n", out);
		fails += !!expect_access("select_file_final_access",
					 selected_file, R_OK, 0, out);
		fails += !!expect_openat2_cached(
			"select_file_final_open_cached", selected_file,
			O_RDONLY | O_CLOEXEC, out);
		fails += !!expect_openat2_cached(
			"select_file_final_opath_cached", selected_file,
			O_PATH | O_CLOEXEC, out);
		fails += !!expect_openat2_resolve(
			"select_file_no_xdev_rejected", selected_parent_fd,
			"selected-file", O_RDONLY | O_CLOEXEC,
			RESOLVE_NO_XDEV, EXDEV, out);
		fails += !!expect_openat2_resolve(
			"select_file_beneath_rejected", selected_parent_fd,
			"selected-file", O_RDONLY | O_CLOEXEC,
			RESOLVE_BENEATH, EXDEV, out);
		fails += !!expect_openat2_resolve(
			"select_file_in_root_rejected", selected_parent_fd,
			"selected-file", O_RDONLY | O_CLOEXEC,
			RESOLVE_IN_ROOT, EXDEV, out);
		fails += !!expect_open_flags(
			"select_file_directory_rejected", selected_file,
			O_RDONLY | O_DIRECTORY | O_CLOEXEC, ENOTDIR, out);
		fails += !!expect_open_flags(
			"select_file_create_rejected", selected_file,
			O_RDONLY | O_CREAT | O_CLOEXEC, EOPNOTSUPP, out);
		fails += !!expect_stat("select_file_intermediate_rejected",
				       selected_file_child, ENOTDIR, out);
		fails += !!expect_read_file("select_file_via_symlink",
					    selected_file_link, "selected\n", out);
		fails += !!expect_access("select_exec_final_access",
					 selected_exec, X_OK, 0, out);
		fails += !!expect_exec_errno("select_exec_final_exec",
					     selected_exec, 0, out);
		fails += !!expect_unprivileged_open(
			"select_file_private_parent_capability", selected_file,
			0, out);

		if (chmod(selected_file_target, 0000)) {
			emit_case(out, "select_file_permission_setup", false,
				  errno, "target chmod failed");
			fails++;
		} else {
			emit_case(out, "select_file_permission_setup", true, 0,
				  "target mode set to 000");
			fails += !!expect_unprivileged_open(
				"select_file_permission_denied", selected_file,
				EACCES, out);
			if (chmod(selected_file_target, 0644)) {
				emit_case(out, "select_file_permission_restore",
					  false, errno,
					  "target mode restore failed");
				fails++;
			} else {
				emit_case(out, "select_file_permission_restore",
					  true, 0,
					  "target mode restored");
			}
		}

		err = register_target_path(replacement_target_dir, 1);
		if (err) {
			emit_case(out, "select_replace_target", false, -err,
				  "target replacement failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_replace_target", true, 0,
			  "target replaced atomically");
		fails += !!expect_read_file("select_replacement_payload_read",
					    portal_payload, "replacement\n", out);

		err = register_target_path(target_dir, 1);
		if (err) {
			emit_case(out, "select_restore_target", false, -err,
				  "target restore failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_restore_target", true, 0,
			  "original target restored");
		fails += !!expect_read_file("select_restored_payload_read",
					    portal_payload, "selected\n", out);
		fails += !!expect_atomic_target_replacement(
			"select_directory_concurrent_replacement",
			portal_payload, target_dir, replacement_target_dir, 1,
			out);
		fails += !!expect_atomic_target_replacement(
			"select_file_concurrent_replacement", selected_file,
			selected_file_target, selected_file_replacement, 2, out);
		fails += !!expect_read_file("select_file_restored_read",
					    selected_file, "selected\n", out);
		err = register_target_path(target_dir, 2);
		if (err) {
			emit_case(out, "select_file_replace_with_directory",
				  false, -err,
				  "file target type replacement failed");
			fails++;
			goto cleanup;
		}
		fails += !!expect_open_flags(
			"select_file_directory_replacement_visible",
			selected_file, O_RDONLY | O_DIRECTORY | O_CLOEXEC,
			0, out);
		err = register_target_path(selected_file_target, 2);
		if (err) {
			emit_case(out, "select_file_restore_after_directory",
				  false, -err,
				  "file target restore failed");
			fails++;
			goto cleanup;
		}
		fails += !!expect_read_file(
			"select_file_restored_after_directory", selected_file,
			"selected\n", out);
		fails += !!expect_read_file("select_pinned_before_unlink",
					    selected_pinned_file, "pinned\n", out);
		if (unlink(selected_pinned_target)) {
			emit_case(out, "select_pinned_unlink", false, errno,
				  "physical pinned target unlink failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "select_pinned_unlink", true, 0,
			  "physical pinned target unlinked");
		fails += !!expect_stat("select_pinned_physical_absent",
				       selected_pinned_target, ENOENT, out);
		fails += !!expect_read_file("select_pinned_after_unlink",
					    selected_pinned_file, "pinned\n", out);

		err = destroy_policy(&policy);
		if (err) {
			emit_case(out, "detach_select_policy", false, -err,
				  "select policy detach failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "detach_select_policy", true, 0,
			  "select policy detached");
		fails += !!expect_stat("select_portal_after_detach",
				       portal_payload, ENOENT, out);
		fails += !!expect_stat("select_file_after_detach",
				       selected_file, ENOENT, out);
		fails += !!expect_stat("select_pinned_after_detach",
				       selected_pinned_file, ENOENT, out);

		err = clear_targets(out, "select_clear_targets");
		fails += !!err;
		if (!err)
			targets_registered = false;
		if (load_and_attach(select_obj_path, cgroup_path, &policy)) {
			emit_case(out, "attach_select_policy_after_clear", false,
				  errno, "load or attach failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "attach_select_policy_after_clear", true, 0,
			  "select policy attached after target clear");
		fails += !!expect_stat("select_unregistered_after_clear",
				       portal_payload, ENOENT, out);
		fails += !!expect_stat("select_file_unregistered_after_clear",
				       selected_file, ENOENT, out);
		fails += !!expect_stat("select_pinned_unregistered_after_clear",
				       selected_pinned_file, ENOENT, out);
		err = destroy_policy(&policy);
		if (err) {
			emit_case(out, "detach_select_policy_after_clear", false,
				  -err, "select policy detach failed");
			fails++;
			goto cleanup;
		}
		emit_case(out, "detach_select_policy_after_clear", true, 0,
			  "select policy detached after target clear");
	}

	if (load_and_attach(argv[1], cgroup_path, &policy)) {
		emit_case(out, "attach_policy", false, errno,
			  "load or attach failed");
		fails++;
		goto cleanup;
	}
	emit_case(out, "attach_policy", true, 0, "policy attached");

	fails += !!expect_stat("native_stat", native, 0, out);
	fails += !!expect_open("native_open", native, 0, out);
	fails += !!expect_access("native_access", native, X_OK, 0, out);
	fails += !!expect_stat("alias_stat", alias, 0, out);
	fails += !!expect_open("alias_open", alias, 0, out);
	fails += !!expect_access("alias_access", alias, X_OK, 0, out);
	fails += !!expect_read_file("alias_read", alias, "real-tool\n", out);
	fails += !!expect_exec_errno("alias_exec", alias, ENOEXEC, out);
	fails += !!expect_stat("backing_stat", backing, 0, out);
	fails += !!expect_readdir(root, out);

cleanup:
	if (policy.attached) {
		err = destroy_policy(&policy);
		if (err) {
			emit_case(out, "detach_policy", false, -err,
				  "policy detach failed");
			fails++;
		} else {
			emit_case(out, "detach_policy", true, 0,
				  "policy detached");
			fails += !!expect_stat("alias_after_detach", alias,
					       ENOENT, out);
			fails += !!expect_stat("backing_after_detach", backing,
					       0, out);
		}
	}
	if (selected_parent_fd >= 0)
		close(selected_parent_fd);
	if (targets_registered) {
		err = clear_targets(out, "select_cleanup_targets");
		fails += !!err;
		if (!err)
			targets_registered = false;
	}
	unlink(native);
	unlink(backing);
	unlink(secret);
	unlink(target_payload);
	unlink(replacement_target_payload);
	chmod(selected_private_dir, 0700);
	unlink(selected_file_target);
	unlink(selected_file_replacement);
	unlink(selected_exec_target);
	unlink(selected_pinned_target);
	unlink(selected_symlink_target);
	unlink(selected_fifo_target);
	rmdir(selected_private_dir);
	if (selected_target_mounted && umount(selected_target_mount)) {
		emit_case(out, "select_target_unmount", false, errno,
			  "selected target tmpfs unmount failed");
		fails++;
	} else if (selected_target_mounted) {
		emit_case(out, "select_target_unmount", true, 0,
			  "selected target tmpfs unmounted");
	}
	unlink(selected_file_link);
	unlink(scope_other_secret);
	rmdir(visible);
	rmdir(scope_other);
	rmdir(target_dir);
	rmdir(replacement_target_dir);
	rmdir(selected_target_mount);
	rmdir(root);
	emit_case(out, "functional_summary", fails == 0, fails,
		  fails ? "functional failures" : "functional passed");
	fclose(out);
	return fails ? 1 : 0;
}

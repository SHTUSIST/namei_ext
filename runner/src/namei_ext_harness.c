// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE

#include "namei_ext_harness.h"

#include <bpf/bpf.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <linux/bpf.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NAMEI_EXT_HARNESS_NAME_MAX 64
#define NAMEI_EXT_CONTROL_TIMEOUT_SECONDS 5

struct namei_ext_component_key {
	uint32_t event;
	uint32_t name_len;
	uint64_t cgroup_id;
	uint64_t parent_dev;
	uint64_t parent_ino;
	uint8_t name[NAMEI_EXT_HARNESS_NAME_MAX];
};

int namei_ext_path_join(char *dst, size_t size, const char *dir,
			const char *name)
{
	int ret = snprintf(dst, size, "%s/%s", dir, name);

	if (ret < 0)
		return -errno;
	if ((size_t)ret >= size)
		return -ENAMETOOLONG;
	return 0;
}

int namei_ext_write_text(const char *path, const char *value)
{
	size_t len = strlen(value);
	ssize_t nwritten;
	int fd;

	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
	if (fd < 0)
		return -errno;
	nwritten = write(fd, value, len);
	if (nwritten != (ssize_t)len) {
		int saved_errno = errno ? errno : EIO;

		close(fd);
		return -saved_errno;
	}
	if (close(fd))
		return -errno;
	return 0;
}

bool namei_ext_read_text_equals(const char *path, const char *expected)
{
	char buf[4096] = {};
	size_t expected_len = strlen(expected);
	ssize_t nread;
	int fd;

	if (expected_len >= sizeof(buf))
		return false;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;
	nread = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	return nread == (ssize_t)expected_len && !memcmp(buf, expected,
							 expected_len);
}

int namei_ext_copy_file(const char *source, const char *destination)
{
	char buf[4096];
	ssize_t nread;
	int in_fd;
	int out_fd;

	in_fd = open(source, O_RDONLY | O_CLOEXEC);
	if (in_fd < 0)
		return -errno;
	out_fd = open(destination, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC,
		      0644);
	if (out_fd < 0) {
		int saved_errno = errno;

		close(in_fd);
		return -saved_errno;
	}
	while ((nread = read(in_fd, buf, sizeof(buf))) > 0) {
		ssize_t offset = 0;

		while (offset < nread) {
			ssize_t nwritten = write(out_fd, buf + offset,
						nread - offset);

			if (nwritten < 0) {
				int saved_errno = errno;

				close(out_fd);
				close(in_fd);
				return -saved_errno;
			}
			offset += nwritten;
		}
	}
	if (nread < 0) {
		int saved_errno = errno;

		close(out_fd);
		close(in_fd);
		return -saved_errno;
	}
	if (close(out_fd)) {
		int saved_errno = errno;

		close(in_fd);
		return -saved_errno;
	}
	if (close(in_fd))
		return -errno;
	return 0;
}

static int remove_callback(const char *path, const struct stat *statbuf,
			   int type, struct FTW *ftw)
{
	(void)statbuf;
	(void)type;
	(void)ftw;
	return remove(path);
}

void namei_ext_remove_tree(const char *path)
{
	if (path && path[0])
		nftw(path, remove_callback, 64, FTW_DEPTH | FTW_PHYS);
}

int namei_ext_move_self_to_cgroup(const char *cgroup_path)
{
	char procs_path[PATH_MAX];
	char pid_buf[32];
	ssize_t nwritten;
	int fd;
	int len;

	if (namei_ext_path_join(procs_path, sizeof(procs_path), cgroup_path,
				"cgroup.procs"))
		return -ENAMETOOLONG;
	fd = open(procs_path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	len = snprintf(pid_buf, sizeof(pid_buf), "%ld\n", (long)getpid());
	if (len < 0 || (size_t)len >= sizeof(pid_buf)) {
		close(fd);
		return -EINVAL;
	}
	nwritten = write(fd, pid_buf, len);
	if (nwritten != len) {
		int saved_errno = errno ? errno : EIO;

		close(fd);
		return -saved_errno;
	}
	if (close(fd))
		return -errno;
	return 0;
}

int namei_ext_wait_child(pid_t pid)
{
	int status;

	if (waitpid(pid, &status, 0) != pid)
		return -errno;
	if (!WIFEXITED(status))
		return -ECHILD;
	return WEXITSTATUS(status) ? -EIO : 0;
}

static int namei_ext_monotonic_ns(uint64_t *value_out)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return -errno;
	*value_out = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
	return 0;
}

static int namei_ext_wait_control_child(pid_t pid)
{
	uint64_t now = 0;
	uint64_t deadline = 0;
	int status = 0;
	int ret;

	ret = namei_ext_monotonic_ns(&now);
	if (ret)
		goto terminate;
	deadline = now +
		(uint64_t)NAMEI_EXT_CONTROL_TIMEOUT_SECONDS * 1000000000ULL;
	for (;;) {
		pid_t waited = waitpid(pid, &status, WNOHANG);

		if (waited == pid) {
			if (!WIFEXITED(status))
				return -ECHILD;
			return WEXITSTATUS(status) ? -EIO : 0;
		}
		if (waited < 0 && errno != EINTR)
			return -errno;
		ret = namei_ext_monotonic_ns(&now);
		if (ret || now >= deadline)
			break;
		usleep(10000);
	}

terminate:
	if (kill(pid, SIGKILL) && errno != ESRCH)
		return ret ? ret : -errno;
	for (unsigned int attempt = 0; attempt < 100; attempt++) {
		pid_t waited = waitpid(pid, &status, WNOHANG);

		if (waited == pid || (waited < 0 && errno == ECHILD))
			return ret ? ret : -ETIMEDOUT;
		if (waited < 0 && errno != EINTR)
			return ret ? ret : -errno;
		usleep(10000);
	}
	return ret ? ret : -ETIMEDOUT;
}

int namei_ext_cgroup_id(const char *path, uint64_t *id_out)
{
	union {
		uint64_t cgroup_id;
		unsigned char bytes[8];
	} id = {};
	struct file_handle *handle;
	struct file_handle *resized;
	size_t size = sizeof(*handle);
	int mount_id = 0;
	int saved_errno;
	int ret;

	handle = calloc(1, size);
	if (!handle)
		return -errno;
	errno = 0;
	ret = name_to_handle_at(AT_FDCWD, path, handle, &mount_id, 0);
	if (ret >= 0 || errno != EOVERFLOW || handle->handle_bytes != 8) {
		saved_errno = errno ? errno : EINVAL;
		free(handle);
		return -saved_errno;
	}
	size += handle->handle_bytes;
	resized = realloc(handle, size);
	if (!resized) {
		saved_errno = errno;
		free(handle);
		return -saved_errno;
	}
	handle = resized;
	ret = name_to_handle_at(AT_FDCWD, path, handle, &mount_id, 0);
	if (ret < 0) {
		saved_errno = errno;
		free(handle);
		return -saved_errno;
	}
	memcpy(id.bytes, handle->f_handle, sizeof(id.bytes));
	free(handle);
	if (!id.cgroup_id)
		return -EINVAL;
	*id_out = id.cgroup_id;
	return 0;
}

static int namei_ext_register_target_fd(
	int register_fd, const struct namei_ext_target_registration *target)
{
	char register_buf[64];
	ssize_t nwritten;
	int target_fd;
	int len;
	int ret = 0;

	target_fd = open(target->path, O_PATH | O_CLOEXEC);
	if (target_fd < 0)
		return -errno;
	len = snprintf(register_buf, sizeof(register_buf), "%u %d\n",
		       target->target_id, target_fd);
	if (len < 0 || (size_t)len >= sizeof(register_buf)) {
		ret = -EOVERFLOW;
		goto out_close;
	}
	nwritten = write(register_fd, register_buf, len);
	if (nwritten != len)
		ret = -(errno ? errno : EIO);
out_close:
	if (close(target_fd) && !ret)
		ret = -errno;
	return ret;
}

int namei_ext_register_target_batch(
	const char *cgroup_path,
	const struct namei_ext_target_registration *targets,
	size_t target_count)
{
	pid_t pid;
	size_t index;

	if (!cgroup_path || !targets || !target_count)
		return -EINVAL;
	for (index = 0; index < target_count; index++) {
		if (!targets[index].path || !targets[index].path[0] ||
		    !targets[index].target_id)
			return -EINVAL;
	}

	pid = fork();
	if (pid < 0)
		return -errno;
	if (!pid) {
		int register_fd;

		if (namei_ext_move_self_to_cgroup(cgroup_path))
			_exit(1);
		register_fd = open("/sys/kernel/debug/namei_ext/register_target",
				   O_WRONLY | O_CLOEXEC);
		if (register_fd < 0)
			_exit(1);
		for (index = 0; index < target_count; index++) {
			if (namei_ext_register_target_fd(register_fd,
						       &targets[index])) {
				close(register_fd);
				_exit(1);
			}
		}
		_exit(close(register_fd) ? 1 : 0);
	}
	return namei_ext_wait_control_child(pid);
}

int namei_ext_register_target(const char *cgroup_path,
			       const char *target_path, uint32_t target_id)
{
	const struct namei_ext_target_registration target = {
		.path = target_path,
		.target_id = target_id,
	};

	return namei_ext_register_target_batch(cgroup_path, &target, 1);
}

int namei_ext_clear_targets(const char *cgroup_path)
{
	pid_t pid = fork();

	if (pid < 0)
		return -errno;
	if (!pid) {
		const char clear[] = "clear\n";
		ssize_t nwritten;
		int register_fd;

		if (namei_ext_move_self_to_cgroup(cgroup_path))
			_exit(1);
		register_fd = open("/sys/kernel/debug/namei_ext/register_target",
				   O_WRONLY | O_CLOEXEC);
		if (register_fd < 0)
			_exit(1);
		nwritten = write(register_fd, clear, strlen(clear));
		close(register_fd);
		_exit(nwritten == (ssize_t)strlen(clear) ? 0 : 1);
	}
	return namei_ext_wait_control_child(pid);
}

static int namei_ext_policy_parent_command(const char *cgroup_path,
					    const char *command,
					    const char *parent_dir)
{
	pid_t pid = fork();

	if (pid < 0)
		return -errno;
	if (!pid) {
		char command_buf[64];
		ssize_t nwritten;
		int parent_fd = -1;
		int control_fd;
		int len;

		if (namei_ext_move_self_to_cgroup(cgroup_path))
			_exit(1);
		if (parent_dir) {
			parent_fd = open(parent_dir,
					 O_PATH | O_DIRECTORY | O_CLOEXEC);
			if (parent_fd < 0)
				_exit(1);
			len = snprintf(command_buf, sizeof(command_buf),
				       "%s %d\n", command, parent_fd);
		} else {
			len = snprintf(command_buf, sizeof(command_buf), "%s\n",
				       command);
		}
		if (len < 0 || (size_t)len >= sizeof(command_buf)) {
			if (parent_fd >= 0)
				close(parent_fd);
			_exit(1);
		}
		control_fd = open("/sys/kernel/debug/namei_ext/policy_parent",
				  O_WRONLY | O_CLOEXEC);
		if (control_fd < 0) {
			if (parent_fd >= 0)
				close(parent_fd);
			_exit(1);
		}
		nwritten = write(control_fd, command_buf, len);
		close(control_fd);
		if (parent_fd >= 0)
			close(parent_fd);
		_exit(nwritten == len ? 0 : 1);
	}
	return namei_ext_wait_control_child(pid);
}

int namei_ext_policy_parent_exact(const char *cgroup_path,
				   const char *parent_dir)
{
	return namei_ext_policy_parent_command(cgroup_path, "exact",
					       parent_dir);
}

int namei_ext_policy_parent_add(const char *cgroup_path,
				 const char *parent_dir)
{
	return namei_ext_policy_parent_command(cgroup_path, "add", parent_dir);
}

int namei_ext_policy_parent_clear(const char *cgroup_path)
{
	return namei_ext_policy_parent_command(cgroup_path, "clear", NULL);
}

int namei_ext_policy_parent_global(const char *cgroup_path)
{
	return namei_ext_policy_parent_command(cgroup_path, "global", NULL);
}

int namei_ext_policy_load_attach(const char *obj_path,
				 const char *cgroup_path,
				 struct namei_ext_harness_policy *policy)
{
	struct bpf_program *program;
	struct bpf_object *object;
	int cgroup_fd;
	int program_fd;
	int err;

	if (!policy) {
		errno = EINVAL;
		return -1;
	}
	object = bpf_object__open_file(obj_path, NULL);
	err = libbpf_get_error(object);
	if (err) {
		errno = -err;
		return -1;
	}
	err = bpf_object__load(object);
	if (err) {
		errno = -err;
		goto close_object;
	}
	program = bpf_object__next_program(object, NULL);
	if (!program) {
		errno = EINVAL;
		goto close_object;
	}
	program_fd = bpf_program__fd(program);
	if (program_fd < 0) {
		errno = EINVAL;
		goto close_object;
	}
	cgroup_fd = open(cgroup_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (cgroup_fd < 0)
		goto close_object;
	err = bpf_prog_attach(program_fd, cgroup_fd, BPF_CGROUP_NAMEI_EXT, 0);
	if (err) {
		errno = -err;
		close(cgroup_fd);
		goto close_object;
	}
	policy->obj = object;
	policy->cgroup_fd = cgroup_fd;
	policy->prog_fd = program_fd;
	policy->attached = true;
	return 0;

close_object:
	bpf_object__close(object);
	return -1;
}

int namei_ext_policy_destroy(struct namei_ext_harness_policy *policy)
{
	int err = 0;

	if (!policy)
		return -EINVAL;
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

static int fill_component_key(struct namei_ext_component_key *key,
			      uint64_t cgroup_id, const char *parent,
			      const char *name)
{
	struct stat st;
	size_t name_len = strlen(name);

	if (name_len > sizeof(key->name))
		return -ENAMETOOLONG;
	if (stat(parent, &st))
		return -errno;
	memset(key, 0, sizeof(*key));
	key->name_len = name_len;
	key->cgroup_id = cgroup_id;
	key->parent_dev = st.st_dev;
	key->parent_ino = st.st_ino;
	memcpy(key->name, name, name_len);
	return 0;
}

static int component_map_fd(struct namei_ext_harness_policy *policy,
			    const char *map_name)
{
	struct bpf_map *map;
	int map_fd;

	if (!policy || !policy->obj)
		return -EINVAL;
	map = bpf_object__find_map_by_name(policy->obj, map_name);
	if (!map)
		return -ENOENT;
	map_fd = bpf_map__fd(map);
	return map_fd < 0 ? -EINVAL : map_fd;
}

static int component_key_map_fd(struct namei_ext_harness_policy *policy,
				const char *map_name)
{
	struct bpf_map *map;
	int map_fd;

	map_fd = component_map_fd(policy, map_name);
	if (map_fd < 0)
		return map_fd;
	map = bpf_object__find_map_by_name(policy->obj, map_name);
	if (!map ||
	    bpf_map__key_size(map) != sizeof(struct namei_ext_component_key) ||
	    bpf_map__value_size(map) != sizeof(uint32_t))
		return -EINVAL;
	return map_fd;
}

int namei_ext_component_map_update(
	struct namei_ext_harness_policy *policy, const char *map_name,
	uint64_t cgroup_id, const char *parent, const char *name,
	uint32_t value)
{
	struct namei_ext_component_key key;
	int map_fd;
	int ret;

	ret = fill_component_key(&key, cgroup_id, parent, name);
	if (ret)
		return ret;
	map_fd = component_key_map_fd(policy, map_name);
	if (map_fd < 0)
		return map_fd;
	if (bpf_map_update_elem(map_fd, &key, &value, BPF_ANY))
		return -errno;
	return 0;
}

int namei_ext_component_map_delete(
	struct namei_ext_harness_policy *policy, const char *map_name,
	uint64_t cgroup_id, const char *parent, const char *name)
{
	struct namei_ext_component_key key;
	int map_fd;
	int ret;

	ret = fill_component_key(&key, cgroup_id, parent, name);
	if (ret)
		return ret;
	map_fd = component_key_map_fd(policy, map_name);
	if (map_fd < 0)
		return map_fd;
	if (bpf_map_delete_elem(map_fd, &key) && errno != ENOENT)
		return -errno;
	return 0;
}

int namei_ext_component_map_lookup(
	struct namei_ext_harness_policy *policy, const char *map_name,
	uint64_t cgroup_id, const char *parent, const char *name,
	uint32_t *value_out)
{
	struct namei_ext_component_key key;
	uint32_t value;
	int map_fd;
	int ret;

	if (!value_out)
		return -EINVAL;
	ret = fill_component_key(&key, cgroup_id, parent, name);
	if (ret)
		return ret;
	map_fd = component_key_map_fd(policy, map_name);
	if (map_fd < 0)
		return map_fd;
	if (bpf_map_lookup_elem(map_fd, &key, &value))
		return -errno;
	*value_out = value;
	return 0;
}

int namei_ext_component_map_count(
	struct namei_ext_harness_policy *policy, const char *map_name,
	size_t *count_out)
{
	struct namei_ext_component_key current;
	struct namei_ext_component_key next;
	const void *key = NULL;
	size_t count = 0;
	int map_fd;

	if (!count_out)
		return -EINVAL;
	map_fd = component_key_map_fd(policy, map_name);
	if (map_fd < 0)
		return map_fd;
	while (!bpf_map_get_next_key(map_fd, key, &next)) {
		current = next;
		key = &current;
		count++;
	}
	if (errno != ENOENT)
		return -errno;
	*count_out = count;
	return 0;
}

int namei_ext_policy_counter(struct namei_ext_harness_policy *policy,
			     const char *map_name, uint32_t key,
			     uint64_t *value_out)
{
	uint64_t value = 0;
	int map_fd;

	if (!value_out)
		return -EINVAL;
	map_fd = component_map_fd(policy, map_name);
	if (map_fd < 0)
		return map_fd;
	if (bpf_map_lookup_elem(map_fd, &key, &value))
		return -errno;
	*value_out = value;
	return 0;
}

// SPDX-License-Identifier: GPL-2.0

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)
#define SPINDLE_STAGING_NO_MAIN
#include "namei_ext_spindle_staging.c"

#include <fuse_lowlevel.h>
#include <poll.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>

#define RQ2_SAMPLE_TIMEOUT_MS 120000
#define RQ2_CONTROL_TIMEOUT_MS 5000
#define RQ2_START_TIMEOUT_MS 10000
#define RQ2_FUSE_TIMEOUT 3600.0
#define RQ2_FUSE_THREADS 8

enum rq2_fuse_counter {
	RQ2_FUSE_TOTAL = 0,
	RQ2_FUSE_LOOKUP,
	RQ2_FUSE_GETATTR,
	RQ2_FUSE_OPEN,
	RQ2_FUSE_RELEASE,
	RQ2_FUSE_OPENDIR,
	RQ2_FUSE_READDIR,
	RQ2_FUSE_RELEASEDIR,
	RQ2_FUSE_READLINK,
	RQ2_FUSE_ACCESS,
	RQ2_FUSE_STATFS,
	RQ2_FUSE_READ_FALLBACK,
	RQ2_FUSE_PASSTHROUGH_OPEN,
	RQ2_FUSE_PASSTHROUGH_FAILURE,
	RQ2_FUSE_INVALIDATE_INODE,
	RQ2_FUSE_INVALIDATE_ENTRY,
	RQ2_FUSE_COUNTER_MAX,
};

enum rq2_fuse_command {
	RQ2_FUSE_INVALIDATE = 1,
	RQ2_FUSE_WITHDRAW = 2,
	RQ2_FUSE_STOP = 3,
};

struct rq2_fuse_shared {
	volatile uint64_t counters[RQ2_FUSE_COUNTER_MAX];
	volatile uint64_t target_lookups[FOCAL_OBJECTS];
	volatile uint64_t target_opens[FOCAL_OBJECTS];
	volatile uint64_t target_passthrough[FOCAL_OBJECTS];
	volatile int withdrawn[FOCAL_OBJECTS];
	volatile int initialized;
	volatile int passthrough_negotiated;
};

struct rq2_fuse_inode {
	struct rq2_fuse_inode *next;
	struct rq2_fuse_inode *prev;
	pthread_mutex_t mutex;
	int fd;
	ino_t ino;
	dev_t dev;
	uint64_t refcount;
	uint64_t nopen;
	int backing_id;
	int target_index;
	char logical[PATH_MAX];
};

struct rq2_fuse_state {
	pthread_mutex_t mutex;
	struct rq2_fuse_inode root;
	struct focal_mapping *mappings;
	struct rq2_fuse_shared *shared;
	struct fuse_session *session;
	char lower_root[PATH_MAX];
};

struct rq2_fuse_dir {
	DIR *dir;
	struct dirent *entry;
	off_t offset;
};

struct rq2_fuse_control_request {
	int command;
	int target_index;
};

struct rq2_fuse_control_response {
	int status;
	int inode_status;
	int entry_status;
	int epoch_attempted;
	int epoch_status;
};

struct rq2_fuse_control {
	struct rq2_fuse_state *state;
	int request_fd;
	int response_fd;
};

struct rq2_fuse_process {
	pid_t pid;
	int request_fd;
	int response_fd;
	struct rq2_fuse_shared *shared;
	char lower_root[PATH_MAX];
	char mountpoint[PATH_MAX];
	bool lower_mounted;
};

struct rq2_daemon_resource {
	uint64_t user_ticks;
	uint64_t system_ticks;
	uint64_t cpu_runtime_ns;
	uint64_t runqueue_wait_ns;
	uint64_t voluntary_switches;
	uint64_t involuntary_switches;
	uint64_t threads;
};

static bool rq2_fuse_entry_invalidation_ok(int status)
{
	return !status || status == -ENOENT;
}

static bool rq2_fuse_invalidation_ok(
	const struct rq2_fuse_control_response *response)
{
	bool needs_epoch = response->entry_status == -ENOENT;

	return !response->status && !response->inode_status &&
		rq2_fuse_entry_invalidation_ok(response->entry_status) &&
		!!response->epoch_attempted == needs_epoch &&
		!response->epoch_status;
}

struct rq2_timed_result {
	struct process_result process;
	struct rusage usage;
};

struct rq2_identity_wire {
	struct stat st;
	int error;
	int bytes_equal;
};

struct rq2_errno_wire {
	int setup_error;
	int operation_errno;
};

enum rq2_path_probe_operation {
	RQ2_PATH_PROBE_OPEN,
	RQ2_PATH_PROBE_STAT,
};

static void rq2_fuse_count(struct rq2_fuse_state *state, unsigned int key)
{
	if (key < RQ2_FUSE_COUNTER_MAX) {
		__sync_fetch_and_add(&state->shared->counters[RQ2_FUSE_TOTAL], 1);
		if (key != RQ2_FUSE_TOTAL)
			__sync_fetch_and_add(&state->shared->counters[key], 1);
	}
}

static struct rq2_fuse_state *rq2_fuse_state(fuse_req_t req)
{
	return fuse_req_userdata(req);
}

static struct rq2_fuse_inode *rq2_fuse_inode(fuse_req_t req, fuse_ino_t ino)
{
	if (ino == FUSE_ROOT_ID)
		return &rq2_fuse_state(req)->root;
	return (struct rq2_fuse_inode *)(uintptr_t)ino;
}

static int rq2_fuse_logical_path(char *path, size_t size,
				 const char *parent, const char *name)
{
	int length;

	if (!strcmp(parent, "/"))
		length = snprintf(path, size, "/%s", name);
	else
		length = snprintf(path, size, "%s/%s", parent, name);
	if (length < 0)
		return -errno;
	return (size_t)length < size ? 0 : -ENAMETOOLONG;
}

static int rq2_fuse_mapping_index(const char *logical)
{
	char expected[PATH_MAX];

	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		const struct focal_spec *spec = &focal_specs[index];
		int length;

		if (spec->subdir[0])
			length = snprintf(expected, sizeof(expected), "/%s/%s",
					  spec->subdir, spec->name);
		else
			length = snprintf(expected, sizeof(expected), "/%s",
					  spec->name);
		if (length < 0 || (size_t)length >= sizeof(expected))
			return -ENAMETOOLONG;
		if (!strcmp(logical, expected))
			return (int)index;
	}
	return -1;
}

static struct rq2_fuse_inode *rq2_fuse_find_locked(
	struct rq2_fuse_state *state, const struct stat *st)
{
	struct rq2_fuse_inode *inode;

	for (inode = state->root.next; inode != &state->root;
	     inode = inode->next) {
		if (inode->ino == st->st_ino && inode->dev == st->st_dev)
			return inode;
	}
	return NULL;
}

static struct rq2_fuse_inode *rq2_fuse_find_logical_locked(
	struct rq2_fuse_state *state, const char *logical)
{
	struct rq2_fuse_inode *inode;

	if (!strcmp(logical, "/"))
		return &state->root;
	for (inode = state->root.next; inode != &state->root;
	     inode = inode->next) {
		if (!strcmp(inode->logical, logical))
			return inode;
	}
	return NULL;
}

static void rq2_fuse_unref(struct rq2_fuse_state *state,
			   struct rq2_fuse_inode *inode, uint64_t count)
{
	if (!inode || inode == &state->root)
		return;
	pthread_mutex_lock(&state->mutex);
	if (inode->refcount < count) {
		pthread_mutex_unlock(&state->mutex);
		abort();
	}
	inode->refcount -= count;
	if (inode->refcount) {
		pthread_mutex_unlock(&state->mutex);
		return;
	}
	inode->prev->next = inode->next;
	inode->next->prev = inode->prev;
	pthread_mutex_unlock(&state->mutex);
	pthread_mutex_destroy(&inode->mutex);
	close(inode->fd);
	free(inode);
}

static int rq2_fuse_do_lookup(fuse_req_t req, fuse_ino_t parent,
			       const char *name, struct fuse_entry_param *entry)
{
	struct rq2_fuse_state *state = rq2_fuse_state(req);
	struct rq2_fuse_inode *parent_inode = rq2_fuse_inode(req, parent);
	struct rq2_fuse_inode *inode;
	char logical[PATH_MAX];
	int target_index;
	int newfd = -1;
	int ret;

	memset(entry, 0, sizeof(*entry));
	entry->attr_timeout = RQ2_FUSE_TIMEOUT;
	entry->entry_timeout = RQ2_FUSE_TIMEOUT;
	ret = rq2_fuse_logical_path(logical, sizeof(logical),
				    parent_inode->logical, name);
	if (ret)
		return -ret;
	target_index = rq2_fuse_mapping_index(logical);
	if (target_index < -1)
		return -target_index;
	if (target_index >= 0 &&
	    __atomic_load_n(&state->shared->withdrawn[target_index],
			    __ATOMIC_ACQUIRE))
		return ENOENT;
	if (target_index >= 0)
		newfd = open(state->mappings[target_index].cache,
			     O_PATH | O_NOFOLLOW | O_CLOEXEC);
	else
		newfd = openat(parent_inode->fd, name,
			       O_PATH | O_NOFOLLOW | O_CLOEXEC);
	if (newfd < 0)
		return errno;
	if (fstatat(newfd, "", &entry->attr,
		    AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW)) {
		ret = errno;
		close(newfd);
		return ret;
	}

	pthread_mutex_lock(&state->mutex);
	inode = rq2_fuse_find_locked(state, &entry->attr);
	if (inode) {
		inode->refcount++;
		pthread_mutex_unlock(&state->mutex);
		close(newfd);
	} else {
		inode = calloc(1, sizeof(*inode));
		if (!inode) {
			pthread_mutex_unlock(&state->mutex);
			close(newfd);
			return ENOMEM;
		}
		if (pthread_mutex_init(&inode->mutex, NULL)) {
			pthread_mutex_unlock(&state->mutex);
			close(newfd);
			free(inode);
			return ENOMEM;
		}
		inode->fd = newfd;
		inode->ino = entry->attr.st_ino;
		inode->dev = entry->attr.st_dev;
		inode->refcount = 1;
		inode->target_index = target_index;
		if (snprintf(inode->logical, sizeof(inode->logical), "%s",
			     logical) >= (int)sizeof(inode->logical)) {
			pthread_mutex_unlock(&state->mutex);
			pthread_mutex_destroy(&inode->mutex);
			close(newfd);
			free(inode);
			return ENAMETOOLONG;
		}
		inode->prev = &state->root;
		inode->next = state->root.next;
		state->root.next->prev = inode;
		state->root.next = inode;
		pthread_mutex_unlock(&state->mutex);
	}
	entry->ino = (fuse_ino_t)(uintptr_t)inode;
	if (target_index >= 0)
		__sync_fetch_and_add(
			&state->shared->target_lookups[target_index], 1);
	return 0;
}

static void rq2_fuse_init(void *userdata, struct fuse_conn_info *conn)
{
	struct rq2_fuse_state *state = userdata;

	state->shared->passthrough_negotiated =
		fuse_set_feature_flag(conn, FUSE_CAP_PASSTHROUGH) ? 1 : 0;
	fuse_set_feature_flag(conn, FUSE_CAP_SPLICE_READ);
	fuse_set_feature_flag(conn, FUSE_CAP_SPLICE_WRITE);
	fuse_set_feature_flag(conn, FUSE_CAP_SPLICE_MOVE);
	fuse_set_feature_flag(conn, FUSE_CAP_NO_EXPORT_SUPPORT);
	conn->no_interrupt = 1;
	conn->max_write = 4 * 1024 * 1024;
	__sync_synchronize();
	state->shared->initialized = 1;
}

static void rq2_fuse_destroy(void *userdata)
{
	struct rq2_fuse_state *state = userdata;

	while (state->root.next != &state->root)
		rq2_fuse_unref(state, state->root.next,
				state->root.next->refcount);
}

static void rq2_fuse_lookup(fuse_req_t req, fuse_ino_t parent,
			    const char *name)
{
	struct fuse_entry_param entry;
	int ret;

	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_LOOKUP);
	ret = rq2_fuse_do_lookup(req, parent, name, &entry);
	if (ret)
		fuse_reply_err(req, ret);
	else
		fuse_reply_entry(req, &entry);
}

static void rq2_fuse_forget(fuse_req_t req, fuse_ino_t ino, uint64_t nlookup)
{
	rq2_fuse_unref(rq2_fuse_state(req), rq2_fuse_inode(req, ino), nlookup);
	fuse_reply_none(req);
}

static void rq2_fuse_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct stat st;
	int fd = fi ? (int)fi->fh : rq2_fuse_inode(req, ino)->fd;

	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_GETATTR);
	if (fstatat(fd, "", &st, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		fuse_reply_err(req, errno);
	else
		fuse_reply_attr(req, &st, RQ2_FUSE_TIMEOUT);
}

static void rq2_fuse_readlink(fuse_req_t req, fuse_ino_t ino)
{
	char buffer[PATH_MAX + 1];
	ssize_t length;

	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_READLINK);
	length = readlinkat(rq2_fuse_inode(req, ino)->fd, "", buffer,
			    sizeof(buffer) - 1);
	if (length < 0) {
		fuse_reply_err(req, errno);
		return;
	}
	buffer[length] = '\0';
	fuse_reply_readlink(req, buffer);
}

static int rq2_reopen_inode(const struct rq2_fuse_inode *inode, int flags)
{
	char proc_path[64];
	int length;

	length = snprintf(proc_path, sizeof(proc_path), "/proc/self/fd/%d",
			  inode->fd);
	if (length < 0 || (size_t)length >= sizeof(proc_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return open(proc_path, flags | O_CLOEXEC);
}

static void rq2_fuse_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	struct rq2_fuse_state *state = rq2_fuse_state(req);
	struct rq2_fuse_inode *inode = rq2_fuse_inode(req, ino);
	int fd;
	int backing_id;

	rq2_fuse_count(state, RQ2_FUSE_OPEN);
	fd = rq2_reopen_inode(inode, fi->flags);
	if (fd < 0) {
		fuse_reply_err(req, errno);
		return;
	}
	pthread_mutex_lock(&inode->mutex);
	if (inode->target_index >= 0 &&
	    __atomic_load_n(&state->shared->withdrawn[inode->target_index],
			    __ATOMIC_ACQUIRE)) {
		pthread_mutex_unlock(&inode->mutex);
		close(fd);
		fuse_reply_err(req, ENOENT);
		return;
	}
	backing_id = inode->backing_id;
	if (!backing_id) {
		backing_id = fuse_passthrough_open(req, fd);
		if (backing_id > 0)
			inode->backing_id = backing_id;
	}
	if (backing_id <= 0) {
		__sync_fetch_and_add(
			&state->shared->counters[RQ2_FUSE_PASSTHROUGH_FAILURE], 1);
		pthread_mutex_unlock(&inode->mutex);
		close(fd);
		fuse_reply_err(req, EIO);
		return;
	}
	inode->nopen++;
	fi->fh = (uint64_t)fd;
	fi->backing_id = backing_id;
	fi->keep_cache = 0;
	__sync_fetch_and_add(
		&state->shared->counters[RQ2_FUSE_PASSTHROUGH_OPEN], 1);
	if (inode->target_index >= 0) {
		__sync_fetch_and_add(
			&state->shared->target_opens[inode->target_index], 1);
		__sync_fetch_and_add(
			&state->shared->target_passthrough[inode->target_index], 1);
	}
	fuse_reply_open(req, fi);
	pthread_mutex_unlock(&inode->mutex);
}

static void rq2_fuse_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	char *buffer;
	ssize_t count;

	(void)ino;
	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_READ_FALLBACK);
	buffer = malloc(size);
	if (!buffer) {
		fuse_reply_err(req, ENOMEM);
		return;
	}
	count = pread((int)fi->fh, buffer, size, offset);
	if (count < 0)
		fuse_reply_err(req, errno);
	else
		fuse_reply_buf(req, buffer, (size_t)count);
	free(buffer);
}

static void rq2_fuse_release(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct rq2_fuse_inode *inode = rq2_fuse_inode(req, ino);
	int ret = 0;

	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_RELEASE);
	if (close((int)fi->fh))
		ret = errno;
	pthread_mutex_lock(&inode->mutex);
	if (!inode->nopen) {
		if (!ret)
			ret = EIO;
	} else {
		inode->nopen--;
		if (!inode->nopen && inode->backing_id) {
			if (fuse_passthrough_close(req, inode->backing_id) < 0) {
				__sync_fetch_and_add(
					&rq2_fuse_state(req)->shared->counters[
						RQ2_FUSE_PASSTHROUGH_FAILURE],
					1);
				if (!ret)
					ret = EIO;
			}
			inode->backing_id = 0;
		}
	}
	pthread_mutex_unlock(&inode->mutex);
	fuse_reply_err(req, ret);
}

static void rq2_fuse_opendir(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct rq2_fuse_dir *handle = calloc(1, sizeof(*handle));
	int fd = -1;

	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_OPENDIR);
	if (!handle)
		goto nomem;
	fd = rq2_reopen_inode(rq2_fuse_inode(req, ino), O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		goto error;
	handle->dir = fdopendir(fd);
	if (!handle->dir)
		goto error;
	fi->fh = (uint64_t)(uintptr_t)handle;
	fi->cache_readdir = 1;
	fuse_reply_open(req, fi);
	return;
error:
	{
		int error = errno;
		if (fd >= 0)
			close(fd);
		free(handle);
		fuse_reply_err(req, error);
		return;
	}
nomem:
	fuse_reply_err(req, ENOMEM);
}

static void rq2_fuse_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t offset, struct fuse_file_info *fi)
{
	struct rq2_fuse_dir *handle =
		(struct rq2_fuse_dir *)(uintptr_t)fi->fh;
	char *buffer = calloc(1, size);
	char *cursor = buffer;
	size_t remaining = size;
	int error = 0;

	(void)ino;
	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_READDIR);
	if (!buffer) {
		fuse_reply_err(req, ENOMEM);
		return;
	}
	if (offset != handle->offset) {
		seekdir(handle->dir, offset);
		handle->entry = NULL;
		handle->offset = offset;
	}
	for (;;) {
		struct stat st = {};
		size_t entry_size;
		off_t next_offset;

		if (!handle->entry) {
			errno = 0;
			handle->entry = readdir(handle->dir);
			if (!handle->entry) {
				error = errno;
				break;
			}
		}
		st.st_ino = handle->entry->d_ino;
		st.st_mode = (mode_t)handle->entry->d_type << 12;
		next_offset = handle->entry->d_off;
		entry_size = fuse_add_direntry(req, cursor, remaining,
					   handle->entry->d_name, &st,
					   next_offset);
		if (entry_size > remaining)
			break;
		cursor += entry_size;
		remaining -= entry_size;
		handle->entry = NULL;
		handle->offset = next_offset;
	}
	if (error && remaining == size)
		fuse_reply_err(req, error);
	else
		fuse_reply_buf(req, buffer, size - remaining);
	free(buffer);
}

static void rq2_fuse_releasedir(fuse_req_t req, fuse_ino_t ino,
				struct fuse_file_info *fi)
{
	struct rq2_fuse_dir *handle =
		(struct rq2_fuse_dir *)(uintptr_t)fi->fh;

	(void)ino;
	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_RELEASEDIR);
	closedir(handle->dir);
	free(handle);
	fuse_reply_err(req, 0);
}

static void rq2_fuse_access(fuse_req_t req, fuse_ino_t ino, int mask)
{
	struct stat st;

	(void)mask;
	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_ACCESS);
	if (fstatat(rq2_fuse_inode(req, ino)->fd, "", &st,
		    AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW))
		fuse_reply_err(req, errno);
	else
		fuse_reply_err(req, 0);
}

static void rq2_fuse_statfs(fuse_req_t req, fuse_ino_t ino)
{
	struct statvfs st;

	rq2_fuse_count(rq2_fuse_state(req), RQ2_FUSE_STATFS);
	if (fstatvfs(rq2_fuse_inode(req, ino)->fd, &st))
		fuse_reply_err(req, errno);
	else
		fuse_reply_statfs(req, &st);
}

static const struct fuse_lowlevel_ops rq2_fuse_operations = {
	.init = rq2_fuse_init,
	.destroy = rq2_fuse_destroy,
	.lookup = rq2_fuse_lookup,
	.forget = rq2_fuse_forget,
	.getattr = rq2_fuse_getattr,
	.readlink = rq2_fuse_readlink,
	.open = rq2_fuse_open,
	.read = rq2_fuse_read,
	.release = rq2_fuse_release,
	.opendir = rq2_fuse_opendir,
	.readdir = rq2_fuse_readdir,
	.releasedir = rq2_fuse_releasedir,
	.access = rq2_fuse_access,
	.statfs = rq2_fuse_statfs,
};

static int rq2_fuse_logical_for_index(int target_index, char *logical,
				       size_t size)
{
	const struct focal_spec *spec;
	int length;

	if (target_index < 0 || target_index >= FOCAL_OBJECTS)
		return -EINVAL;
	spec = &focal_specs[target_index];
	if (spec->subdir[0])
		length = snprintf(logical, size, "/%s/%s", spec->subdir,
				  spec->name);
	else
		length = snprintf(logical, size, "/%s", spec->name);
	if (length < 0)
		return -errno;
	return (size_t)length < size ? 0 : -ENAMETOOLONG;
}

static int rq2_fuse_invalidate_target(struct rq2_fuse_state *state,
				       int target_index,
				       struct rq2_fuse_control_response *response)
{
	struct rq2_fuse_inode *inode;
	struct rq2_fuse_inode *parent_inode;
	char logical[PATH_MAX];
	char parent[PATH_MAX];
	char *name;
	fuse_ino_t inode_number;
	fuse_ino_t parent_number;
	int ret;

	ret = rq2_fuse_logical_for_index(target_index, logical,
					 sizeof(logical));
	if (ret)
		return ret;
	if (snprintf(parent, sizeof(parent), "%s", logical) >=
	    (int)sizeof(parent))
		return -ENAMETOOLONG;
	name = strrchr(parent, '/');
	if (!name)
		return -EINVAL;
	*name++ = '\0';
	if (!parent[0] && snprintf(parent, sizeof(parent), "/") >=
			  (int)sizeof(parent))
		return -ENAMETOOLONG;

	pthread_mutex_lock(&state->mutex);
	inode = rq2_fuse_find_logical_locked(state, logical);
	parent_inode = rq2_fuse_find_logical_locked(state, parent);
	if (!inode || !parent_inode) {
		pthread_mutex_unlock(&state->mutex);
		return -ENOENT;
	}
	if (inode != &state->root)
		inode->refcount++;
	if (parent_inode != &state->root)
		parent_inode->refcount++;
	inode_number = inode == &state->root ? FUSE_ROOT_ID :
		(fuse_ino_t)(uintptr_t)inode;
	parent_number = parent_inode == &state->root ? FUSE_ROOT_ID :
		(fuse_ino_t)(uintptr_t)parent_inode;
	pthread_mutex_unlock(&state->mutex);

	response->entry_status = fuse_lowlevel_notify_inval_entry(
		state->session, parent_number, name, strlen(name));
	__sync_fetch_and_add(
		&state->shared->counters[RQ2_FUSE_INVALIDATE_ENTRY], 1);
	if (response->entry_status == -ENOENT) {
		response->epoch_attempted = 1;
		response->epoch_status =
			fuse_lowlevel_notify_increment_epoch(state->session);
	}
	response->inode_status = fuse_lowlevel_notify_inval_inode(
		state->session, inode_number, 0, 0);
	__sync_fetch_and_add(
		&state->shared->counters[RQ2_FUSE_INVALIDATE_INODE], 1);
	rq2_fuse_unref(state, inode, 1);
	rq2_fuse_unref(state, parent_inode, 1);
	if (!rq2_fuse_entry_invalidation_ok(response->entry_status))
		return response->entry_status;
	if (response->epoch_status)
		return response->epoch_status;
	return response->inode_status;
}

static int rq2_fuse_publish_withdrawal(struct rq2_fuse_state *state,
					int target_index)
{
	struct rq2_fuse_inode *inode = NULL;
	int error;

	pthread_mutex_lock(&state->mutex);
	for (struct rq2_fuse_inode *candidate = state->root.next;
	     candidate != &state->root; candidate = candidate->next) {
		if (candidate->target_index != target_index)
			continue;
		candidate->refcount++;
		inode = candidate;
		break;
	}
	pthread_mutex_unlock(&state->mutex);

	if (inode) {
		error = pthread_mutex_lock(&inode->mutex);
		if (error) {
			rq2_fuse_unref(state, inode, 1);
			return -error;
		}
	}
	__atomic_store_n(&state->shared->withdrawn[target_index], 1,
			 __ATOMIC_RELEASE);
	if (!inode)
		return 0;
	error = pthread_mutex_unlock(&inode->mutex);
	rq2_fuse_unref(state, inode, 1);
	return error ? -error : 0;
}

static int rq2_write_full(int fd, const void *buffer, size_t size)
{
	const char *cursor = buffer;

	while (size) {
		ssize_t count = write(fd, cursor, size);

		if (count < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!count)
			return -EIO;
		cursor += count;
		size -= (size_t)count;
	}
	return 0;
}

static int rq2_read_full(int fd, void *buffer, size_t size)
{
	char *cursor = buffer;

	while (size) {
		ssize_t count = read(fd, cursor, size);

		if (count < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (!count)
			return -EPIPE;
		cursor += count;
		size -= (size_t)count;
	}
	return 0;
}

static int rq2_read_full_timeout(int fd, void *buffer, size_t size,
				 unsigned int timeout_ms)
{
	struct pollfd poll_fd = {
		.fd = fd,
		.events = POLLIN,
	};
	int ret;

	do {
		ret = poll(&poll_fd, 1, (int)timeout_ms);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0)
		return -errno;
	if (!ret)
		return -ETIMEDOUT;
	if (!(poll_fd.revents & (POLLIN | POLLHUP)))
		return -EIO;
	return rq2_read_full(fd, buffer, size);
}

static int rq2_waitpid_timeout(pid_t pid, unsigned int timeout_ms,
				       int *status)
{
	struct pollfd poll_fd = {
		.fd = (int)syscall(SYS_pidfd_open, pid, 0),
		.events = POLLIN,
	};
	int ret;

	if (poll_fd.fd < 0)
		return -errno;
	do {
		ret = poll(&poll_fd, 1, (int)timeout_ms);
	} while (ret < 0 && errno == EINTR);
	close(poll_fd.fd);
	if (ret < 0)
		return -errno;
	if (!ret)
		return -ETIMEDOUT;
	while (waitpid(pid, status, 0) < 0) {
		if (errno != EINTR)
			return -errno;
	}
	return 0;
}

static int rq2_kill_and_reap(pid_t pid, int *status)
{
	int ret;

	if (kill(pid, SIGKILL) && errno != ESRCH)
		return -errno;
	ret = rq2_waitpid_timeout(pid, RQ2_CONTROL_TIMEOUT_MS, status);
	return ret;
}

static int rq2_stat_timeout(const char *path, unsigned int timeout_ms)
{
	pid_t pid;
	int status = 0;
	int ret;

	pid = fork();
	if (pid < 0)
		return -errno;
	if (!pid) {
		struct stat st;

		_exit(stat(path, &st) ? 1 : 0);
	}
	ret = rq2_waitpid_timeout(pid, timeout_ms, &status);
	if (ret == -ETIMEDOUT) {
		int reap_status = 0;
		int reap_ret = rq2_kill_and_reap(pid, &reap_status);

		if (reap_ret)
			return reap_ret;
		return ret;
	}
	if (ret)
		return ret;
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		return -EIO;
	return 0;
}

static int rq2_timed_join(pthread_t thread, void **thread_status,
			  unsigned int timeout_ms)
{
	struct timespec deadline;
	int ret;

	if (clock_gettime(CLOCK_REALTIME, &deadline))
		return -errno;
	deadline.tv_sec += timeout_ms / 1000;
	deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
	if (deadline.tv_nsec >= 1000000000L) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000L;
	}
	ret = pthread_timedjoin_np(thread, thread_status, &deadline);
	return ret ? -ret : 0;
}

static void *rq2_fuse_control_loop(void *argument)
{
	struct rq2_fuse_control *control = argument;

	for (;;) {
		struct rq2_fuse_control_request request;
		struct rq2_fuse_control_response response = {};
		int ret = rq2_read_full(control->request_fd, &request,
					sizeof(request));

		if (ret)
			return (void *)(intptr_t)ret;
		if (request.command == RQ2_FUSE_STOP) {
			fuse_session_exit(control->state->session);
			ret = rq2_write_full(control->response_fd, &response,
					     sizeof(response));
			return (void *)(intptr_t)ret;
		}
		if (request.target_index < 0 ||
		    request.target_index >= FOCAL_OBJECTS ||
		    (request.command != RQ2_FUSE_INVALIDATE &&
		     request.command != RQ2_FUSE_WITHDRAW)) {
			response.status = -EINVAL;
		} else {
			if (request.command == RQ2_FUSE_WITHDRAW) {
				response.status = rq2_fuse_publish_withdrawal(
					control->state, request.target_index);
			}
			if (!response.status)
				response.status = rq2_fuse_invalidate_target(
					control->state, request.target_index,
					&response);
		}
		ret = rq2_write_full(control->response_fd, &response,
				     sizeof(response));
		if (ret)
			return (void *)(intptr_t)ret;
	}
}

static int rq2_run_fuse_server(struct focal_mapping mappings[FOCAL_OBJECTS],
			       struct rq2_fuse_shared *shared,
			       const char *lower_root, const char *mountpoint,
			       int ready_fd, int request_fd, int response_fd)
{
	struct rq2_fuse_state state = {
		.mappings = mappings,
		.shared = shared,
	};
	struct rq2_fuse_control control = {
		.state = &state,
		.request_fd = request_fd,
		.response_fd = response_fd,
	};
	struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
	struct fuse_loop_config *loop_config = NULL;
	struct fuse_session *session = NULL;
	pthread_t control_thread;
	bool control_started = false;
	int ready_status = -EIO;
	int loop_status = -1;
	int ret = 1;

	if (pthread_mutex_init(&state.mutex, NULL))
		goto out;
	state.root.next = state.root.prev = &state.root;
	state.root.fd = open(lower_root, O_PATH | O_DIRECTORY | O_CLOEXEC);
	state.root.refcount = UINT64_MAX;
	state.root.target_index = -1;
	if (state.root.fd < 0 ||
	    snprintf(state.root.logical, sizeof(state.root.logical), "/") >=
		    (int)sizeof(state.root.logical))
		goto out_mutex;
	if (fuse_opt_add_arg(&args, "namei_ext_spindle_staging_rq2") ||
	    fuse_opt_add_arg(&args, "-o") ||
	    fuse_opt_add_arg(&args,
			     "allow_other,default_permissions,fsname=namei_ext-spindle-rq2"))
		goto out_root;
	session = fuse_session_new(&args, &rq2_fuse_operations,
				   sizeof(rq2_fuse_operations), &state);
	if (!session)
		goto out_args;
	state.session = session;
	if (fuse_session_mount(session, mountpoint))
		goto out_session;
	loop_config = fuse_loop_cfg_create();
	if (!loop_config)
		goto out_unmount;
	fuse_loop_cfg_set_max_threads(loop_config, RQ2_FUSE_THREADS);
	fuse_loop_cfg_set_idle_threads(loop_config, RQ2_FUSE_THREADS);
	fuse_loop_cfg_set_clone_fd(loop_config, 1);
	if (pthread_create(&control_thread, NULL, rq2_fuse_control_loop,
			   &control))
		goto out_loop;
	control_started = true;
	ready_status = 0;
	if (rq2_write_full(ready_fd, &ready_status, sizeof(ready_status)))
		goto out_control;
	close(ready_fd);
	ready_fd = -1;
	loop_status = fuse_session_loop_mt(session, loop_config);
	ret = loop_status ? 1 : 0;

out_control:
	if (control_started) {
		void *thread_status = NULL;
		int join_status;

		join_status = rq2_timed_join(control_thread, &thread_status,
					     RQ2_CONTROL_TIMEOUT_MS);
		if (join_status == -ETIMEDOUT) {
			if (pthread_cancel(control_thread))
				ret = 1;
			join_status = rq2_timed_join(control_thread, &thread_status,
						     RQ2_CONTROL_TIMEOUT_MS);
		}
		if (join_status)
			_exit(1);
		if ((intptr_t)thread_status)
			ret = 1;
	}
out_loop:
	fuse_loop_cfg_destroy(loop_config);
out_unmount:
	fuse_session_unmount(session);
out_session:
	fuse_session_destroy(session);
out_args:
	fuse_opt_free_args(&args);
out_root:
	if (state.root.fd >= 0)
		close(state.root.fd);
out_mutex:
	pthread_mutex_destroy(&state.mutex);
out:
	if (ready_fd >= 0) {
		rq2_write_full(ready_fd, &ready_status, sizeof(ready_status));
		close(ready_fd);
	}
	close(request_fd);
	close(response_fd);
	return ret;
}

static int rq2_fuse_request(struct rq2_fuse_process *process, int command,
			    int target_index,
			    struct rq2_fuse_control_response *response)
{
	struct rq2_fuse_control_request request = {
		.command = command,
		.target_index = target_index,
	};
	char logical[PATH_MAX];
	char path[PATH_MAX];
	int pinned_fd = -1;
	int ret;

	memset(response, 0, sizeof(*response));
	if (command == RQ2_FUSE_INVALIDATE || command == RQ2_FUSE_WITHDRAW) {
		ret = rq2_fuse_logical_for_index(target_index, logical,
						 sizeof(logical));
		if (ret)
			return ret;
		if (snprintf(path, sizeof(path), "%s%s", process->mountpoint,
			     logical) >= (int)sizeof(path))
			return -ENAMETOOLONG;
		pinned_fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
		if (pinned_fd < 0)
			return -errno;
	}
	ret = rq2_write_full(process->request_fd, &request, sizeof(request));
	if (!ret)
		ret = rq2_read_full_timeout(process->response_fd, response,
					    sizeof(*response),
					    RQ2_CONTROL_TIMEOUT_MS);
	if (!ret)
		ret = response->status;
	if (pinned_fd >= 0 && close(pinned_fd) && !ret)
		ret = -errno;
	return ret;
}

static int rq2_start_fuse(struct rq2_fuse_process *process,
			  struct focal_mapping mappings[FOCAL_OBJECTS],
			  const char *mountpoint)
{
	int request_pipe[2] = { -1, -1 };
	int response_pipe[2] = { -1, -1 };
	int ready_pipe[2] = { -1, -1 };
	int ready_status = -EIO;
	int ret;

	memset(process, 0, sizeof(*process));
	process->pid = -1;
	process->request_fd = -1;
	process->response_fd = -1;
	if (snprintf(process->mountpoint, sizeof(process->mountpoint), "%s",
		     mountpoint) >= (int)sizeof(process->mountpoint))
		return -ENAMETOOLONG;
	if (snprintf(process->lower_root, sizeof(process->lower_root),
		     "/tmp/namei-ext-spindle-rq2-lower-%ld", (long)getpid()) >=
	    (int)sizeof(process->lower_root))
		return -ENAMETOOLONG;
	namei_ext_remove_tree(process->lower_root);
	if (mkdir(process->lower_root, 0755))
		return -errno;
	if (mount(mountpoint, process->lower_root, NULL, MS_BIND, NULL)) {
		ret = -errno;
		goto error;
	}
	process->lower_mounted = true;
	if (mount(NULL, process->lower_root, NULL,
		  MS_BIND | MS_REMOUNT | MS_RDONLY, NULL)) {
		ret = -errno;
		goto error;
	}
	process->shared = mmap(NULL, sizeof(*process->shared),
			       PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (process->shared == MAP_FAILED) {
		process->shared = NULL;
		ret = -errno;
		goto error;
	}
	if (pipe2(request_pipe, O_CLOEXEC) ||
	    pipe2(response_pipe, O_CLOEXEC) || pipe2(ready_pipe, O_CLOEXEC)) {
		ret = -errno;
		goto error;
	}
	process->pid = fork();
	if (process->pid < 0) {
		ret = -errno;
		goto error;
	}
	if (!process->pid) {
		close(request_pipe[1]);
		close(response_pipe[0]);
		close(ready_pipe[0]);
		_exit(rq2_run_fuse_server(
			mappings, process->shared, process->lower_root, mountpoint,
			ready_pipe[1], request_pipe[0], response_pipe[1]));
	}
	close(request_pipe[0]);
	request_pipe[0] = -1;
	close(response_pipe[1]);
	response_pipe[1] = -1;
	close(ready_pipe[1]);
	ready_pipe[1] = -1;
	process->request_fd = request_pipe[1];
	request_pipe[1] = -1;
	process->response_fd = response_pipe[0];
	response_pipe[0] = -1;
	ret = rq2_read_full_timeout(ready_pipe[0], &ready_status,
				    sizeof(ready_status), RQ2_START_TIMEOUT_MS);
	close(ready_pipe[0]);
	ready_pipe[0] = -1;
	if (ret || ready_status) {
		if (!ret)
			ret = ready_status;
		goto error;
	}
	ret = rq2_stat_timeout(mountpoint, RQ2_START_TIMEOUT_MS);
	if (ret)
		goto error;
	if (!__atomic_load_n(&process->shared->initialized, __ATOMIC_ACQUIRE) ||
	    !__atomic_load_n(&process->shared->passthrough_negotiated,
			     __ATOMIC_ACQUIRE)) {
		ret = -EOPNOTSUPP;
		goto error;
	}
	return 0;

error:
	for (size_t index = 0; index < 2; index++) {
		if (request_pipe[index] >= 0)
			close(request_pipe[index]);
		if (response_pipe[index] >= 0)
			close(response_pipe[index]);
		if (ready_pipe[index] >= 0)
			close(ready_pipe[index]);
	}
	if (process->request_fd >= 0)
		close(process->request_fd);
	if (process->response_fd >= 0)
		close(process->response_fd);
	if (process->pid > 0) {
		int cleanup_status = 0;
		int cleanup_ret;

		if (umount2(process->mountpoint, MNT_DETACH) &&
		    errno != EINVAL && errno != ENOENT && !ret)
			ret = -errno;
		cleanup_ret = rq2_kill_and_reap(process->pid, &cleanup_status);
		if (cleanup_ret && !ret)
			ret = cleanup_ret;
	}
	if (process->shared)
		munmap(process->shared, sizeof(*process->shared));
	if (process->lower_mounted)
		umount2(process->lower_root, MNT_DETACH);
	namei_ext_remove_tree(process->lower_root);
	process->pid = -1;
	process->request_fd = process->response_fd = -1;
	process->shared = NULL;
	return ret;
}

static int rq2_stop_fuse(struct rq2_fuse_process *process)
{
	struct rq2_fuse_control_response response;
	int first_error = 0;
	int status = 0;
	int ret;

	if (process->pid > 0) {
		ret = rq2_fuse_request(process, RQ2_FUSE_STOP, 0, &response);
		if (ret)
			first_error = ret;
		if (umount2(process->mountpoint, MNT_DETACH) && errno != EINVAL &&
		    !first_error)
			first_error = -errno;
		ret = rq2_waitpid_timeout(process->pid, RQ2_CONTROL_TIMEOUT_MS,
						  &status);
		if (ret == -ETIMEDOUT) {
			int reap_ret = rq2_kill_and_reap(process->pid, &status);

			if (reap_ret && !first_error)
				first_error = reap_ret;
		}
		if (ret && !first_error)
			first_error = ret;
		else if ((!WIFEXITED(status) || WEXITSTATUS(status)) &&
			   !first_error) {
			first_error = -EIO;
		}
	}
	if (process->request_fd >= 0)
		close(process->request_fd);
	if (process->response_fd >= 0)
		close(process->response_fd);
	if (process->lower_mounted &&
	    umount2(process->lower_root, MNT_DETACH) && !first_error)
		first_error = -errno;
	namei_ext_remove_tree(process->lower_root);
	process->pid = -1;
	process->request_fd = process->response_fd = -1;
	process->lower_mounted = false;
	return first_error;
}

static int rq2_read_task_resource(pid_t pid, const char *task,
				  struct rq2_daemon_resource *resource)
{
	char path[PATH_MAX];
	char line[4096];
	FILE *input;
	unsigned long long runtime;
	unsigned long long wait;
	unsigned long long slices;

	if (snprintf(path, sizeof(path), "/proc/%d/task/%s/schedstat", pid,
		     task) >= (int)sizeof(path))
		return -ENAMETOOLONG;
	input = fopen(path, "r");
	if (!input)
		return errno == ENOENT ? 0 : -errno;
	if (fscanf(input, "%llu %llu %llu", &runtime, &wait, &slices) != 3) {
		fclose(input);
		return -EINVAL;
	}
	if (fclose(input))
		return -errno;
	resource->cpu_runtime_ns += runtime;
	resource->runqueue_wait_ns += wait;
	if (snprintf(path, sizeof(path), "/proc/%d/task/%s/status", pid,
		     task) >= (int)sizeof(path))
		return -ENAMETOOLONG;
	input = fopen(path, "r");
	if (!input)
		return errno == ENOENT ? 0 : -errno;
	while (fgets(line, sizeof(line), input)) {
		unsigned long long value;

		if (sscanf(line, "voluntary_ctxt_switches: %llu", &value) == 1)
			resource->voluntary_switches += value;
		if (sscanf(line, "nonvoluntary_ctxt_switches: %llu", &value) == 1)
			resource->involuntary_switches += value;
	}
	if (ferror(input)) {
		int ret = -(errno ? errno : EIO);

		fclose(input);
		return ret;
	}
	if (fclose(input))
		return -errno;
	resource->threads++;
	return 0;
}

static int rq2_read_daemon_resource(pid_t pid,
				    struct rq2_daemon_resource *resource)
{
	struct dirent *entry;
	char path[PATH_MAX];
	char line[4096];
	char *after_comm;
	char process_state;
	long long ignored[10];
	DIR *tasks;
	FILE *input;
	int matched;

	memset(resource, 0, sizeof(*resource));
	if (snprintf(path, sizeof(path), "/proc/%d/stat", pid) >=
	    (int)sizeof(path))
		return -ENAMETOOLONG;
	input = fopen(path, "r");
	if (!input)
		return -errno;
	if (!fgets(line, sizeof(line), input)) {
		int ret = -(errno ? errno : EIO);

		fclose(input);
		return ret;
	}
	if (fclose(input))
		return -errno;
	after_comm = strrchr(line, ')');
	if (!after_comm || after_comm[1] != ' ')
		return -EINVAL;
	matched = sscanf(after_comm + 2,
			 "%c %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %llu %llu",
			 &process_state, &ignored[0], &ignored[1], &ignored[2],
			 &ignored[3], &ignored[4], &ignored[5], &ignored[6],
			 &ignored[7], &ignored[8], &ignored[9],
			 (unsigned long long *)&resource->user_ticks,
			 (unsigned long long *)&resource->system_ticks);
	if (matched != 13)
		return -EINVAL;
	(void)process_state;
	if (snprintf(path, sizeof(path), "/proc/%d/task", pid) >=
	    (int)sizeof(path))
		return -ENAMETOOLONG;
	tasks = opendir(path);
	if (!tasks)
		return -errno;
	for (;;) {
		const char *cursor;
		int ret;

		errno = 0;
		entry = readdir(tasks);
		if (!entry) {
			if (errno) {
				ret = -errno;
				closedir(tasks);
				return ret;
			}
			break;
		}
		cursor = entry->d_name;
		if (!*cursor)
			continue;
		while (*cursor >= '0' && *cursor <= '9')
			cursor++;
		if (*cursor)
			continue;
		ret = rq2_read_task_resource(pid, entry->d_name, resource);
		if (ret) {
			closedir(tasks);
			return ret;
		}
	}
	if (closedir(tasks))
		return -errno;
	return resource->threads ? 0 : -ESRCH;
}

static uint64_t rq2_timeval_ns(const struct timeval *value)
{
	return (uint64_t)value->tv_sec * 1000000000ULL +
		(uint64_t)value->tv_usec * 1000ULL;
}

static int rq2_run_precise_process(
	const char *working_directory, const char *cgroup_path,
	const struct run_environment *environment, char *const argv[],
	char *const envp[], const char *stdout_path, const char *stderr_path,
	unsigned int timeout_ms, struct rq2_timed_result *result)
{
	uint64_t started = monotonic_ns();
	uint64_t deadline = started + (uint64_t)timeout_ms * 1000000ULL;
	struct pollfd poll_fd = {
		.events = POLLIN,
	};
	pid_t pid = fork();
	int status = 0;
	int ret = 0;

	memset(result, 0, sizeof(*result));
	if (pid < 0)
		return -errno;
	if (!pid) {
		int stdout_fd;
		int stderr_fd;

		if (setpgid(0, 0))
			_exit(120);
		if (cgroup_path && namei_ext_move_self_to_cgroup(cgroup_path))
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
		ret = -errno;
		kill(pid, SIGKILL);
		wait4(pid, &status, 0, &result->usage);
		goto done;
	}
	poll_fd.fd = (int)syscall(SYS_pidfd_open, pid, 0);
	if (poll_fd.fd < 0) {
		ret = -errno;
		kill(-pid, SIGKILL);
		kill(pid, SIGKILL);
		wait4(pid, &status, 0, &result->usage);
		goto done;
	}
	for (;;) {
		uint64_t now = monotonic_ns();
		int remaining_ms;
		int poll_ret;

		if (now >= deadline) {
			ret = -ETIMEDOUT;
			break;
		}
		remaining_ms = (int)((deadline - now + 999999ULL) / 1000000ULL);
		poll_ret = poll(&poll_fd, 1, remaining_ms);
		if (poll_ret > 0)
			break;
		if (!poll_ret) {
			ret = -ETIMEDOUT;
			break;
		}
		if (errno != EINTR) {
			ret = -errno;
			break;
		}
	}
	if (ret) {
		kill(-pid, SIGKILL);
		kill(pid, SIGKILL);
	}
	while (wait4(pid, &status, 0, &result->usage) < 0) {
		if (errno != EINTR) {
			if (!ret)
				ret = -errno;
			break;
		}
	}
	close(poll_fd.fd);

done:
	result->process.duration_ns = monotonic_ns() - started;
	if (WIFEXITED(status))
		result->process.exit_status = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		result->process.exit_status = 128 + WTERMSIG(status);
	else if (!ret)
		ret = -ECHILD;
	result->process.runner_errno = ret ? -ret : 0;
	return ret;
}

static void rq2_emit_sample(FILE *out, const char *condition,
			    const char *phase, unsigned int iteration,
			    const struct rq2_timed_result *result,
			    bool diagnostic_ok, bool pass)
{
	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-sample\","
		"\"condition\":\"%s\",\"phase\":\"%s\","
		"\"iteration\":%u,\"duration_ns\":%llu,"
		"\"user_cpu_ns\":%llu,\"system_cpu_ns\":%llu,"
		"\"minor_faults\":%ld,\"major_faults\":%ld,"
		"\"voluntary_context_switches\":%ld,"
		"\"involuntary_context_switches\":%ld,"
		"\"exit_status\":%d,\"runner_errno\":%d,"
		"\"diagnostic_ok\":%s,\"pass\":%s}\n",
		condition, phase, iteration,
		(unsigned long long)result->process.duration_ns,
		(unsigned long long)rq2_timeval_ns(&result->usage.ru_utime),
		(unsigned long long)rq2_timeval_ns(&result->usage.ru_stime),
		result->usage.ru_minflt, result->usage.ru_majflt,
		result->usage.ru_nvcsw, result->usage.ru_nivcsw,
		result->process.exit_status, result->process.runner_errno,
		diagnostic_ok ? "true" : "false", pass ? "true" : "false");
	fflush(out);
}

static void rq2_emit_daemon_resource(
	FILE *out, const struct rq2_daemon_resource *before,
	const struct rq2_daemon_resource *after, bool pass)
{
	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-fuse-resource\","
		"\"user_ticks\":%llu,\"system_ticks\":%llu,"
		"\"cpu_runtime_ns\":%llu,\"runqueue_wait_ns\":%llu,"
		"\"voluntary_context_switches\":%llu,"
		"\"involuntary_context_switches\":%llu,"
		"\"threads_before\":%llu,\"threads_after\":%llu,"
		"\"pass\":%s}\n",
		(unsigned long long)(after->user_ticks - before->user_ticks),
		(unsigned long long)(after->system_ticks - before->system_ticks),
		(unsigned long long)(after->cpu_runtime_ns -
					 before->cpu_runtime_ns),
		(unsigned long long)(after->runqueue_wait_ns -
					 before->runqueue_wait_ns),
		(unsigned long long)(after->voluntary_switches -
					 before->voluntary_switches),
		(unsigned long long)(after->involuntary_switches -
					 before->involuntary_switches),
		(unsigned long long)before->threads,
		(unsigned long long)after->threads, pass ? "true" : "false");
	fflush(out);
}

static void rq2_emit_target_window(FILE *out, const char *condition,
				   const struct focal_mapping *mapping,
				   uint64_t before, uint64_t after,
				   uint64_t passthrough_before,
				   uint64_t passthrough_after, bool pass)
{
	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-target\","
		"\"condition\":\"%s\",\"target_id\":%u,\"name\":",
		condition, mapping->target_id);
	json_string(out, mapping->spec->name);
	fprintf(out,
		",\"hits_before\":%llu,\"hits_after\":%llu,"
		"\"hits_delta\":%llu,\"passthrough_before\":%llu,"
		"\"passthrough_after\":%llu,\"passthrough_delta\":%llu,"
		"\"pass\":%s}\n",
		(unsigned long long)before, (unsigned long long)after,
		(unsigned long long)(after >= before ? after - before : 0),
		(unsigned long long)passthrough_before,
		(unsigned long long)passthrough_after,
		(unsigned long long)(passthrough_after >= passthrough_before ?
					 passthrough_after - passthrough_before : 0),
		pass ? "true" : "false");
	fflush(out);
}

static void rq2_emit_fuse_window(FILE *out, unsigned int key,
				 const char *name, uint64_t before,
				 uint64_t after, bool pass)
{
	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-fuse-counter\","
		"\"counter_id\":%u,\"counter\":\"%s\","
		"\"before\":%llu,\"after\":%llu,\"delta\":%llu,"
		"\"pass\":%s}\n",
		key, name, (unsigned long long)before,
		(unsigned long long)after,
		(unsigned long long)(after >= before ? after - before : 0),
		pass ? "true" : "false");
	fflush(out);
}

static int rq2_sample_path(char *path, size_t size, const char *result_dir,
			   const char *condition, const char *phase,
			   unsigned int iteration, const char *suffix)
{
	char name[160];
	int length = snprintf(name, sizeof(name), "%s-%s-%03u.%s.log",
			      condition, phase, iteration, suffix);

	if (length < 0)
		return -errno;
	if ((size_t)length >= sizeof(name))
		return -ENAMETOOLONG;
	return namei_ext_path_join(path, size, result_dir, name);
}

static int rq2_run_loader_series(
	FILE *out, const char *condition, const char *phase, unsigned int count,
	const char *working_directory, const char *cgroup_path,
	const struct run_environment *environment, char *const argv[],
	char *const envp[], const char *result_dir)
{
	for (unsigned int iteration = 1; iteration <= count; iteration++) {
		struct rq2_timed_result result = {};
		char stdout_path[PATH_MAX];
		char stderr_path[PATH_MAX];
		bool diagnostic_ok = false;
		int ret = rq2_sample_path(stdout_path, sizeof(stdout_path),
					  result_dir, condition, phase,
					  iteration, "stdout");

		if (!ret)
			ret = rq2_sample_path(stderr_path, sizeof(stderr_path),
					      result_dir, condition, phase,
					      iteration, "stderr");
		if (!ret)
			ret = rq2_run_precise_process(
				working_directory, cgroup_path, environment, argv, envp,
				stdout_path, stderr_path, RQ2_SAMPLE_TIMEOUT_MS, &result);
		if (!ret)
			ret = validate_loader_progress(stderr_path, &diagnostic_ok);
		bool pass = !ret && result.process.exit_status == 0 &&
			diagnostic_ok;

		rq2_emit_sample(out, condition, phase, iteration, &result,
				diagnostic_ok, pass);
		if (!pass)
			return -EINVAL;
	}
	return 0;
}

static int rq2_identity_probe(const char *cgroup_path,
			      const struct run_environment *environment,
			      const char *logical_path,
			      const char *expected_path, struct stat *stat_out,
			      bool *bytes_equal_out)
{
	struct rq2_identity_wire wire = {};
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
		if (cgroup_path && namei_ext_move_self_to_cgroup(cgroup_path)) {
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
		if (!wire.error) {
			bool bytes_equal = false;
			int compare_ret;

			compare_ret = files_equal(logical_path, expected_path,
						  &bytes_equal);
			wire.error = compare_ret ? -compare_ret : 0;
			wire.bytes_equal = bytes_equal ? 1 : 0;
		}
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
	*bytes_equal_out = wire.bytes_equal != 0;
	return 0;
}

static int rq2_path_errno_probe(const char *cgroup_path,
				const struct run_environment *environment,
				const char *logical_path,
				enum rq2_path_probe_operation operation,
				int *observed_errno)
{
	struct rq2_errno_wire wire = {};
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
		close(pipe_fd[0]);
		if (cgroup_path && namei_ext_move_self_to_cgroup(cgroup_path))
			wire.setup_error = errno ? errno : EIO;
		else if (drop_privileges(environment->uid, environment->gid))
			wire.setup_error = errno ? errno : EIO;
		else if (operation == RQ2_PATH_PROBE_OPEN) {
			int fd = open(logical_path, O_RDONLY | O_CLOEXEC);

			if (fd >= 0)
				close(fd);
			else
				wire.operation_errno = errno;
		} else {
			struct stat st;

			if (fstatat(AT_FDCWD, logical_path, &st,
				    AT_SYMLINK_NOFOLLOW))
				wire.operation_errno = errno;
		}
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
	if (wire.setup_error)
		return -wire.setup_error;
	*observed_errno = wire.operation_errno;
	return 0;
}

static int rq2_withdrawal_lookup_probe(
	FILE *out, const char *condition, const char *cgroup_path,
	const struct run_environment *environment, const char *logical_path)
{
	int observed_errno = 0;
	int ret = rq2_path_errno_probe(cgroup_path, environment, logical_path,
				       RQ2_PATH_PROBE_STAT, &observed_errno);
	bool pass = !ret && observed_errno == ENOENT;

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-withdrawal-lookup\","
		"\"condition\":\"%s\",\"operation\":\"fstatat\","
		"\"observed_errno\":%d,\"expected_errno\":%d,\"pass\":%s}\n",
		condition, observed_errno, ENOENT, pass ? "true" : "false");
	fflush(out);
	return pass ? 0 : -EINVAL;
}

static bool rq2_daemon_resource_monotonic(
	const struct rq2_daemon_resource *before,
	const struct rq2_daemon_resource *after)
{
	return after->user_ticks >= before->user_ticks &&
		after->system_ticks >= before->system_ticks &&
		after->cpu_runtime_ns >= before->cpu_runtime_ns &&
		after->runqueue_wait_ns >= before->runqueue_wait_ns &&
		after->voluntary_switches >= before->voluntary_switches &&
		after->involuntary_switches >= before->involuntary_switches &&
		before->threads > 0 && after->threads > 0;
}

static void rq2_emit_identity(FILE *out, const char *condition,
			      const struct focal_mapping *mapping,
			      const struct stat *actual, bool exact_identity,
			      bool bytes_equal, bool pass)
{
	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-identity\","
		"\"condition\":\"%s\",\"target_id\":%u,\"name\":",
		condition, mapping->target_id);
	json_string(out, mapping->spec->name);
	fprintf(out,
		",\"actual_dev\":%llu,\"actual_ino\":%llu,"
		"\"actual_mode\":%u,\"actual_size\":%lld,"
		"\"target_dev\":%llu,\"target_ino\":%llu,"
		"\"target_mode\":%u,\"target_size\":%lld,"
		"\"exact_lower_identity_required\":%s,"
		"\"bytes_equal\":%s,\"pass\":%s}\n",
		(unsigned long long)actual->st_dev,
		(unsigned long long)actual->st_ino,
		(unsigned int)actual->st_mode, (long long)actual->st_size,
		(unsigned long long)mapping->cache_before.st.st_dev,
		(unsigned long long)mapping->cache_before.st.st_ino,
		(unsigned int)mapping->cache_before.st.st_mode,
		(long long)mapping->cache_before.st.st_size,
		exact_identity ? "true" : "false",
		bytes_equal ? "true" : "false", pass ? "true" : "false");
	fflush(out);
}

static int rq2_validate_identities(
	FILE *out, const char *condition, const char *cgroup_path,
	const struct run_environment *environment,
	struct focal_mapping mappings[FOCAL_OBJECTS], bool exact_identity)
{
	int failures = 0;

	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		struct stat actual = {};
		bool bytes_equal = false;
		int ret = rq2_identity_probe(cgroup_path, environment,
					     mappings[index].source,
					     mappings[index].cache, &actual,
					     &bytes_equal);
		bool pass = !ret && bytes_equal &&
			(actual.st_mode & (S_IFMT | 07777)) ==
				(mappings[index].cache_before.st.st_mode &
				 (S_IFMT | 07777)) &&
			actual.st_size == mappings[index].cache_before.st.st_size;

		if (exact_identity)
			pass = pass &&
				actual.st_dev == mappings[index].cache_before.st.st_dev &&
				actual.st_ino == mappings[index].cache_before.st.st_ino;
		rq2_emit_identity(out, condition, &mappings[index], &actual,
				  exact_identity, bytes_equal, pass);
		failures += !pass;
	}
	return failures ? -EINVAL : 0;
}

static int rq2_run_withdrawn(FILE *out, const char *condition,
			     const char *working_directory,
			     const char *cgroup_path,
			     const struct run_environment *environment,
			     char *const argv[], char *const envp[],
			     const char *result_dir,
			     struct process_result *result_out)
{
	char stdout_path[PATH_MAX];
	char stderr_path[PATH_MAX];
	char name[128];
	int ret;

	if (snprintf(name, sizeof(name), "%s-withdrawn.stdout.log", condition) >=
	    (int)sizeof(name))
		return -ENAMETOOLONG;
	ret = namei_ext_path_join(stdout_path, sizeof(stdout_path), result_dir,
				  name);
	if (!ret && snprintf(name, sizeof(name), "%s-withdrawn.stderr.log",
				  condition) >= (int)sizeof(name))
		ret = -ENAMETOOLONG;
	if (!ret)
		ret = namei_ext_path_join(stderr_path, sizeof(stderr_path),
					  result_dir, name);
	if (!ret)
		ret = run_process(working_directory, cgroup_path, environment,
				  argv, envp, stdout_path, stderr_path,
				  WITHDRAWN_TIMEOUT_SECONDS, result_out);
	bool expected = !ret && result_out->exit_status != 0 &&
		file_contains(stderr_path, "Failed to dlopen library") &&
		file_contains(stderr_path, "libtest10.so");

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-withdrawal\","
		"\"condition\":\"%s\",\"exit_status\":%d,"
		"\"runner_errno\":%d,\"expected_diagnostic\":%s,"
		"\"pass\":%s}\n",
		condition, result_out->exit_status, result_out->runner_errno,
		expected ? "true" : "false", expected ? "true" : "false");
	fflush(out);
	return expected ? 0 : -EINVAL;
}

static void rq2_emit_lifecycle(FILE *out, const char *condition,
			       const char *phase, uint64_t duration_ns,
			       bool pass);

static int rq2_run_namei_condition(
	FILE *out, const char *policy_path, const char *result_dir,
	const char *cgroup_root, const char *test_dir,
	const struct run_environment *environment, char *const loader_argv[],
	char *const loader_env[], struct focal_mapping mappings[FOCAL_OBJECTS],
	unsigned int warmups, unsigned int samples)
{
	struct namei_ext_harness_policy policy = {
		.cgroup_fd = -1,
		.prog_fd = -1,
	};
	struct process_result withdrawn_result = {};
	char cgroup_path[PATH_MAX];
	char canary_path[PATH_MAX] = {};
	uint64_t cgroup_id = 0;
	uint64_t select_before = 0;
	uint64_t select_after = 0;
	uint64_t hits_before[FOCAL_OBJECTS] = {};
	uint64_t hits_after[FOCAL_OBJECTS] = {};
	uint64_t withdrawn_before = 0;
	uint64_t withdrawn_after = 0;
	uint64_t hide_before = 0;
	uint64_t hide_after = 0;
	bool canary_mounted = false;
	bool cgroup_created = false;
	bool targets_registered = false;
	uint64_t setup_started = monotonic_ns();
	uint64_t teardown_started;
	int failures = 0;
	int failures_before_teardown;
	int ret;

	if (snprintf(cgroup_path, sizeof(cgroup_path),
		     "%s/namei-ext-spindle-rq2-%ld", cgroup_root,
		     (long)getpid()) >= (int)sizeof(cgroup_path))
		return -ENAMETOOLONG;
	ret = setup_canary(mappings[0].source, canary_path,
			   sizeof(canary_path), &canary_mounted);
	emit_case(out, "rq2_namei_cover_libtest10", !ret, ret ? -ret : 0,
		  "read-only empty bind covers the source implementation");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = mkdir(cgroup_path, 0755) ? -errno : 0;
	if (!ret) {
		cgroup_created = true;
		targets_registered = true;
		ret = configure_policy(&policy, policy_path, cgroup_path,
				       mappings, &cgroup_id);
	}
	emit_case(out, "rq2_namei_configure", !ret, ret ? -ret : 0,
		  "47 targets and exact component rules attached");
	rq2_emit_lifecycle(out, "namei_ext", "setup",
			   monotonic_ns() - setup_started, !ret);
	if (ret) {
		failures++;
		goto cleanup;
	}

	mode_t original_mode = mappings[0].cache_before.st.st_mode & 07777;
	int observed_errno = 0;

	ret = chmod(mappings[0].cache, 0000) ? -errno : 0;
	if (!ret)
		ret = rq2_path_errno_probe(cgroup_path, environment,
					    mappings[0].source,
					    RQ2_PATH_PROBE_OPEN,
					    &observed_errno);
	int restore_ret = chmod(mappings[0].cache, original_mode) ? -errno : 0;
	bool permission_pass = !ret && !restore_ret && observed_errno == EACCES;

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-permission\","
		"\"condition\":\"namei_ext\",\"observed_errno\":%d,"
		"\"restore_errno\":%d,\"pass\":%s}\n",
		observed_errno, restore_ret ? -restore_ret : 0,
		permission_pass ? "true" : "false");
	fflush(out);
	if (!permission_pass) {
		failures++;
		goto cleanup;
	}

	ret = rq2_run_loader_series(out, "namei_ext", "warmup", warmups,
				    test_dir, cgroup_path, environment,
				    loader_argv, loader_env, result_dir);
	if (!ret)
		ret = rq2_validate_identities(out, "namei_ext", cgroup_path,
					      environment, mappings, true);
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
		failures++;
		goto cleanup;
	}
	ret = rq2_run_loader_series(out, "namei_ext", "measured", samples,
				    test_dir, cgroup_path, environment,
				    loader_argv, loader_env, result_dir);
	if (!ret)
		ret = collect_counter(&policy, "spindle_staging_counters",
				      SPINDLE_COUNTER_SELECT, &select_after);
	for (size_t index = 0; !ret && index < FOCAL_OBJECTS; index++)
		ret = collect_counter(&policy, "spindle_staging_rule_hits",
				      mappings[index].target_id,
				      &hits_after[index]);
	if (ret) {
		failures++;
		goto cleanup;
	}
	uint64_t per_target_sum = 0;

	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		uint64_t delta = hits_after[index] >= hits_before[index] ?
			hits_after[index] - hits_before[index] : 0;
		bool pass = delta > 0;

		per_target_sum += delta;
		rq2_emit_target_window(out, "namei_ext", &mappings[index],
				       hits_before[index], hits_after[index], 0, 0,
				       pass);
		failures += !pass;
	}
	bool counter_pass = select_after >= select_before &&
		select_after - select_before == per_target_sum &&
		per_target_sum >= FOCAL_OBJECTS;

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-namei-window\","
		"\"select_before\":%llu,\"select_after\":%llu,"
		"\"select_delta\":%llu,\"per_target_sum\":%llu,"
		"\"pass\":%s}\n",
		(unsigned long long)select_before,
		(unsigned long long)select_after,
		(unsigned long long)(select_after - select_before),
		(unsigned long long)per_target_sum,
		counter_pass ? "true" : "false");
	fflush(out);
	failures += !counter_pass;
	if (failures)
		goto cleanup;

	ret = collect_counter(&policy, "spindle_staging_rule_hits",
			      mappings[0].target_id, &withdrawn_before);
	if (!ret)
		ret = collect_counter(&policy, "spindle_staging_counters",
				      SPINDLE_COUNTER_HIDE_LOOKUP, &hide_before);
	if (!ret)
		ret = namei_ext_component_map_update(
			&policy, "spindle_staging_rules", cgroup_id,
			mappings[0].source_parent, mappings[0].spec->name,
			SPINDLE_STAGING_HIDE_VALUE);
	if (!ret)
		ret = rq2_withdrawal_lookup_probe(out, "namei_ext", cgroup_path,
						 environment, mappings[0].source);
	if (!ret)
		ret = rq2_run_withdrawn(out, "namei_ext", test_dir,
					 cgroup_path, environment, loader_argv,
					 loader_env, result_dir, &withdrawn_result);
	int withdrawal_oracle_ret = ret;
	int counter_ret = collect_counter(&policy, "spindle_staging_rule_hits",
					  mappings[0].target_id,
					  &withdrawn_after);

	if (!counter_ret)
		counter_ret = collect_counter(&policy, "spindle_staging_counters",
					      SPINDLE_COUNTER_HIDE_LOOKUP,
					      &hide_after);
	if (!withdrawal_oracle_ret && counter_ret)
		withdrawal_oracle_ret = counter_ret;
	ret = withdrawal_oracle_ret;
	bool withdrawal_pass = !ret && withdrawn_after == withdrawn_before &&
		hide_after > hide_before;

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-withdrawal-window\","
		"\"condition\":\"namei_ext\",\"before\":%llu,"
		"\"after\":%llu,\"hide_before\":%llu,"
		"\"hide_after\":%llu,\"pass\":%s}\n",
		(unsigned long long)withdrawn_before,
		(unsigned long long)withdrawn_after,
		(unsigned long long)hide_before,
		(unsigned long long)hide_after,
		withdrawal_pass ? "true" : "false");
	fflush(out);
	failures += !withdrawal_pass;

cleanup:
	teardown_started = monotonic_ns();
	failures_before_teardown = failures;

	if (policy.attached) {
		if (namei_ext_policy_parent_clear(cgroup_path))
			failures++;
		if (namei_ext_policy_destroy(&policy))
			failures++;
	}
	if (targets_registered && namei_ext_clear_targets(cgroup_path))
		failures++;
	if (cgroup_created && rmdir(cgroup_path) && errno != ENOENT)
		failures++;
	if (canary_mounted && umount2(mappings[0].source, MNT_DETACH))
		failures++;
	if (canary_path[0] && unlink(canary_path) && errno != ENOENT)
		failures++;
	rq2_emit_lifecycle(out, "namei_ext", "teardown",
			   monotonic_ns() - teardown_started,
			   failures == failures_before_teardown);
	emit_case(out, "rq2_namei_condition", failures == 0, failures,
		  failures ? "namei_ext condition failed" :
			     "namei_ext condition passed");
	return failures ? -EINVAL : 0;
}

static void rq2_emit_invalidation(
	FILE *out, const char *phase,
	const struct rq2_fuse_control_response *response, bool pass)
{
	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-fuse-invalidation\","
		"\"phase\":\"%s\",\"status\":%d,"
		"\"inode_status\":%d,\"entry_status\":%d,"
		"\"epoch_attempted\":%s,\"epoch_status\":%d,"
		"\"pass\":%s}\n",
		phase, response->status, response->inode_status,
		response->entry_status,
		response->epoch_attempted ? "true" : "false",
		response->epoch_status, pass ? "true" : "false");
	fflush(out);
}

static void rq2_emit_lifecycle(FILE *out, const char *condition,
			       const char *phase, uint64_t duration_ns,
			       bool pass)
{
	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-lifecycle\","
		"\"condition\":\"%s\",\"phase\":\"%s\","
		"\"duration_ns\":%llu,\"pass\":%s}\n",
		condition, phase, (unsigned long long)duration_ns,
		pass ? "true" : "false");
	fflush(out);
}

static int rq2_run_fuse_condition(
	FILE *out, const char *result_dir, const char *cgroup_root,
	const char *test_dir,
	const struct run_environment *environment, char *const loader_argv[],
	char *const loader_env[], struct focal_mapping mappings[FOCAL_OBJECTS],
	unsigned int warmups, unsigned int samples)
{
	static const char *const counter_names[RQ2_FUSE_COUNTER_MAX] = {
		"total", "lookup", "getattr", "open", "release", "opendir",
		"readdir", "releasedir", "readlink", "access", "statfs",
		"read_fallback", "passthrough_open", "passthrough_failure",
		"invalidate_inode", "invalidate_entry",
	};
	struct rq2_fuse_process process = {
		.pid = -1,
		.request_fd = -1,
		.response_fd = -1,
	};
	struct rq2_daemon_resource resource_before = {};
	struct rq2_daemon_resource resource_after = {};
	struct rq2_fuse_control_response response = {};
	struct process_result withdrawn_result = {};
	char cgroup_path[PATH_MAX];
	uint64_t counters_before[RQ2_FUSE_COUNTER_MAX] = {};
	uint64_t counters_after[RQ2_FUSE_COUNTER_MAX] = {};
	uint64_t opens_before[FOCAL_OBJECTS] = {};
	uint64_t opens_after[FOCAL_OBJECTS] = {};
	uint64_t passthrough_before[FOCAL_OBJECTS] = {};
	uint64_t passthrough_after[FOCAL_OBJECTS] = {};
	uint64_t withdrawn_before = 0;
	uint64_t withdrawn_after = 0;
	bool started = false;
	bool cgroup_created = false;
	uint64_t setup_started = monotonic_ns();
	uint64_t teardown_started;
	int failures = 0;
	int failures_before_teardown;
	int ret;

	if (snprintf(cgroup_path, sizeof(cgroup_path),
		     "%s/namei-ext-spindle-rq2-fuse-%ld", cgroup_root,
		     (long)getpid()) >= (int)sizeof(cgroup_path))
		return -ENAMETOOLONG;
	ret = mkdir(cgroup_path, 0755) ? -errno : 0;
	if (!ret)
		cgroup_created = true;
	emit_case(out, "rq2_fuse_control_cgroup", !ret, ret ? -ret : 0,
		  "unattached sibling cgroup matches per-launch migration cost");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = rq2_start_fuse(&process, mappings, test_dir);
	emit_case(out, "rq2_fuse_start", !ret, ret ? -ret : 0,
		  "libfuse 3.18.2 low-level multithreaded view mounted");
	if (ret) {
		failures++;
		goto cleanup;
	}
	started = true;
	bool negotiated = process.shared->initialized &&
		process.shared->passthrough_negotiated;

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-fuse-config\","
		"\"libfuse_version\":\"3.18.2\",\"low_level\":true,"
		"\"multithreaded\":true,\"threads\":%d,"
		"\"entry_timeout_seconds\":%.0f,"
		"\"attribute_timeout_seconds\":%.0f,"
		"\"allow_other\":true,\"default_permissions\":true,"
		"\"passthrough_negotiated\":%s,\"pass\":%s}\n",
		RQ2_FUSE_THREADS, RQ2_FUSE_TIMEOUT, RQ2_FUSE_TIMEOUT,
		negotiated ? "true" : "false", negotiated ? "true" : "false");
	fflush(out);
	rq2_emit_lifecycle(out, "fuse", "setup",
			   monotonic_ns() - setup_started, negotiated);
	if (!negotiated) {
		failures++;
		goto cleanup;
	}
	ret = rq2_validate_identities(out, "fuse", cgroup_path, environment,
				      mappings, false);
	if (ret) {
		failures++;
		goto cleanup;
	}

	mode_t original_mode = mappings[0].cache_before.st.st_mode & 07777;
	int observed_errno = 0;

	ret = chmod(mappings[0].cache, 0000) ? -errno : 0;
	if (!ret)
		ret = rq2_fuse_request(&process, RQ2_FUSE_INVALIDATE, 0,
				       &response);
	bool invalidate_zero_pass = !ret && rq2_fuse_invalidation_ok(&response);
	rq2_emit_invalidation(out, "mode_zero", &response,
			      invalidate_zero_pass);
	if (!ret)
		ret = rq2_path_errno_probe(cgroup_path, environment,
					     mappings[0].source,
					     RQ2_PATH_PROBE_OPEN,
					     &observed_errno);
	int restore_ret = chmod(mappings[0].cache, original_mode) ? -errno : 0;
	struct rq2_fuse_control_response restore_response = {};
	int invalidate_restore_ret = restore_ret ? restore_ret :
		rq2_fuse_request(&process, RQ2_FUSE_INVALIDATE, 0,
				 &restore_response);
	bool invalidate_restore_pass = !invalidate_restore_ret &&
		rq2_fuse_invalidation_ok(&restore_response);
	rq2_emit_invalidation(out, "mode_restore", &restore_response,
			      invalidate_restore_pass);
	bool permission_pass = !ret && !restore_ret &&
		invalidate_zero_pass && invalidate_restore_pass &&
		observed_errno == EACCES;

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-permission\","
		"\"condition\":\"fuse\",\"observed_errno\":%d,"
		"\"restore_errno\":%d,\"pass\":%s}\n",
		observed_errno, restore_ret ? -restore_ret : 0,
		permission_pass ? "true" : "false");
	fflush(out);
	if (!permission_pass) {
		failures++;
		goto cleanup;
	}

	ret = rq2_run_loader_series(out, "fuse", "warmup", warmups,
				    test_dir, cgroup_path, environment, loader_argv,
				    loader_env, result_dir);
	if (ret) {
		failures++;
		goto cleanup;
	}
	for (size_t index = 0; index < RQ2_FUSE_COUNTER_MAX; index++)
		counters_before[index] = process.shared->counters[index];
	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		opens_before[index] = process.shared->target_opens[index];
		passthrough_before[index] =
			process.shared->target_passthrough[index];
	}
	ret = rq2_read_daemon_resource(process.pid, &resource_before);
	emit_case(out, "rq2_fuse_resource_window_start", !ret,
		  ret ? -ret : 0, "daemon resource snapshot precedes measurements");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = rq2_run_loader_series(out, "fuse", "measured", samples,
				    test_dir, cgroup_path, environment, loader_argv,
				    loader_env, result_dir);
	if (!ret)
		ret = rq2_read_daemon_resource(process.pid, &resource_after);
	if (ret) {
		failures++;
		goto cleanup;
	}
	for (size_t index = 0; index < RQ2_FUSE_COUNTER_MAX; index++)
		counters_after[index] = process.shared->counters[index];
	for (size_t index = 0; index < FOCAL_OBJECTS; index++) {
		opens_after[index] = process.shared->target_opens[index];
		passthrough_after[index] =
			process.shared->target_passthrough[index];
		bool pass = opens_after[index] > opens_before[index] &&
			passthrough_after[index] > passthrough_before[index] &&
			opens_after[index] - opens_before[index] ==
				passthrough_after[index] -
				passthrough_before[index];

		rq2_emit_target_window(out, "fuse", &mappings[index],
				       opens_before[index], opens_after[index],
				       passthrough_before[index],
				       passthrough_after[index], pass);
		failures += !pass;
	}
	bool resource_pass = rq2_daemon_resource_monotonic(
		&resource_before, &resource_after) &&
		resource_after.cpu_runtime_ns > resource_before.cpu_runtime_ns;
	rq2_emit_daemon_resource(out, &resource_before, &resource_after,
				 resource_pass);
	failures += !resource_pass;
	for (size_t index = 0; index < RQ2_FUSE_COUNTER_MAX; index++) {
		uint64_t delta = counters_after[index] >= counters_before[index] ?
			counters_after[index] - counters_before[index] : 0;
		bool pass = counters_after[index] >= counters_before[index];

		if (index == RQ2_FUSE_PASSTHROUGH_OPEN)
			pass = pass && delta > 0;
		if (index == RQ2_FUSE_READ_FALLBACK ||
		    index == RQ2_FUSE_PASSTHROUGH_FAILURE)
			pass = pass && delta == 0;
		rq2_emit_fuse_window(out, (unsigned int)index,
				     counter_names[index], counters_before[index],
				     counters_after[index], pass);
		failures += !pass;
	}
	if (failures)
		goto cleanup;

	withdrawn_before = process.shared->target_opens[0];
	ret = rq2_fuse_request(&process, RQ2_FUSE_WITHDRAW, 0, &response);
	bool withdraw_invalidation_pass = !ret &&
		rq2_fuse_invalidation_ok(&response);
	rq2_emit_invalidation(out, "withdraw", &response,
			      withdraw_invalidation_pass);
	if (!ret)
		ret = rq2_withdrawal_lookup_probe(out, "fuse", cgroup_path,
						 environment, mappings[0].source);
	if (!ret)
		ret = rq2_run_withdrawn(out, "fuse", test_dir, cgroup_path,
					 environment, loader_argv, loader_env,
					 result_dir, &withdrawn_result);
	withdrawn_after = process.shared->target_opens[0];
	bool withdrawal_pass = !ret && withdraw_invalidation_pass &&
		withdrawn_after == withdrawn_before;

	fprintf(out,
		"{\"event\":\"spindle-staging-rq2-withdrawal-window\","
		"\"condition\":\"fuse\",\"before\":%llu,"
		"\"after\":%llu,\"pass\":%s}\n",
		(unsigned long long)withdrawn_before,
		(unsigned long long)withdrawn_after,
		withdrawal_pass ? "true" : "false");
	fflush(out);
	failures += !withdrawal_pass;

cleanup:
	teardown_started = monotonic_ns();
	failures_before_teardown = failures;

	if (started) {
		ret = rq2_stop_fuse(&process);
		emit_case(out, "rq2_fuse_stop", !ret, ret ? -ret : 0,
			  "FUSE view, daemon, and hidden lower bind removed");
		failures += !!ret;
	}
	if (process.shared) {
		if (process.shared->counters[RQ2_FUSE_PASSTHROUGH_FAILURE])
			failures++;
		munmap(process.shared, sizeof(*process.shared));
		process.shared = NULL;
	}
	if (cgroup_created && rmdir(cgroup_path) && errno != ENOENT)
		failures++;
	rq2_emit_lifecycle(out, "fuse", "teardown",
			   monotonic_ns() - teardown_started,
			   failures == failures_before_teardown);
	emit_case(out, "rq2_fuse_condition", failures == 0, failures,
		  failures ? "FUSE condition failed" : "FUSE condition passed");
	return failures ? -EINVAL : 0;
}

static int rq2_parse_count(const char *value, unsigned int *count)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || !value[0] || !end || *end || !parsed || parsed > UINT_MAX)
		return -EINVAL;
	*count = (unsigned int)parsed;
	return 0;
}

int main(int argc, char **argv)
{
	struct focal_mapping mappings[FOCAL_OBJECTS] = {};
	struct run_environment environment = {};
	struct process_result source_result = {};
	const char *condition;
	const char *policy_path;
	const char *result_path;
	const char *spindle;
	const char *test_dir;
	const char *result_dir;
	const char *cgroup_root;
	char test_driver[PATH_MAX];
	char source_stdout[PATH_MAX];
	char source_stderr[PATH_MAX];
	unsigned int warmups = 0;
	unsigned int samples = 0;
	unsigned int log_count = 0;
	bool cache_mounted = false;
	bool comm_mounted = false;
	bool tmp_mounted = false;
	FILE *out = NULL;
	int failures = 0;
	int ret;

	if (argc != 10) {
		fprintf(stderr,
			"usage: %s CONDITION POLICY_BPF_O RESULT_JSONL "
			"SPINDLE_BIN TEST_DIR RESULT_DIR CGROUP_ROOT "
			"WARMUPS SAMPLES\n",
			argv[0]);
		return 2;
	}
	condition = argv[1];
	policy_path = argv[2];
	result_path = argv[3];
	spindle = argv[4];
	test_dir = argv[5];
	result_dir = argv[6];
	cgroup_root = argv[7];
	if ((strcmp(condition, "namei_ext") && strcmp(condition, "fuse")) ||
	    rq2_parse_count(argv[8], &warmups) ||
	    rq2_parse_count(argv[9], &samples)) {
		fprintf(stderr, "invalid condition or sample count\n");
		return 2;
	}
	signal(SIGPIPE, SIG_IGN);
	out = fopen(result_path, "a");
	if (!out) {
		perror("fopen result");
		return 2;
	}
	ret = build_expected_paths(test_dir, mappings);
	if (!ret)
		ret = namei_ext_path_join(test_driver, sizeof(test_driver),
					  test_dir, "test_driver");
	if (!ret)
		ret = namei_ext_path_join(source_stdout, sizeof(source_stdout),
					  result_dir, "source.stdout.log");
	if (!ret)
		ret = namei_ext_path_join(source_stderr, sizeof(source_stderr),
					  result_dir, "source.stderr.log");
	if (!ret)
		ret = prepare_environment(test_dir, result_dir, &environment);
	emit_case(out, "rq2_fixture_paths_and_identity", !ret,
		  ret ? -ret : 0,
		  "upstream paths and unprivileged run identity resolved");
	if (ret) {
		failures++;
		goto cleanup;
	}
	if (access(spindle, X_OK) || access(test_driver, X_OK) ||
	    access(policy_path, R_OK)) {
		emit_case(out, "rq2_upstream_artifacts", false, errno,
			  "Spindle, test_driver, or policy artifact missing");
		failures++;
		goto cleanup;
	}
	ret = setup_tmpfs(CACHE_ROOT, environment.uid, environment.gid);
	if (!ret)
		cache_mounted = true;
	if (!ret)
		ret = setup_tmpfs(COMM_ROOT, environment.uid, environment.gid);
	if (!ret)
		comm_mounted = true;
	if (!ret)
		ret = setup_tmpfs(TMP_ROOT, environment.uid, environment.gid);
	if (!ret)
		tmp_mounted = true;
	if (!ret)
		ret = clear_source_logs(test_dir);
	emit_case(out, "rq2_dedicated_tmpfs_and_clean_logs", !ret,
		  ret ? -ret : 0,
		  "Spindle cache, communication, and temporary roots are fresh");
	if (ret) {
		failures++;
		goto cleanup;
	}

	char *source_argv[] = {
		(char *)spindle,
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
	char *loader_argv[] = {
		test_driver,
		"--dlopen",
		"--pull",
		"--nompi",
		NULL,
	};
	ret = emit_runtime_contract(out, spindle, test_driver, test_dir,
				    &environment, source_argv, loader_argv);
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = run_process(test_dir, NULL, &environment, source_argv,
			  environment.source_env, source_stdout, source_stderr,
			  SOURCE_TIMEOUT_SECONDS, &source_result);
	bool source_diagnostic_ok = false;
	int diagnostic_ret = file_is_empty(source_stderr,
					   &source_diagnostic_ok);
	bool source_pass = !ret && !diagnostic_ret &&
		source_result.exit_status == 0 && source_diagnostic_ok;

	emit_condition(out, "source_spindle", &source_result,
		       source_diagnostic_ok, source_pass,
		       "official serial pull mode populated node-local objects");
	if (!source_pass) {
		failures++;
		goto cleanup;
	}
	ret = wait_for_spindle_quiescence(getpid(), PROCESS_QUIESCENCE_SECONDS);
	emit_case(out, "rq2_source_process_quiescence", !ret,
		  ret ? -ret : 0, "no live Spindle process remains");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = collect_mapping_logs(test_dir, result_dir, mappings, &log_count);
	if (!ret && !log_count)
		ret = -ENOENT;
	if (!ret)
		ret = validate_mappings(out, mappings);
	emit_case(out, "rq2_source_mapping_contract", !ret,
		  ret ? -ret : 0,
		  "47 exact Spindle global-to-local mappings validated");
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = capture_cache_tree(result_dir);
	if (!ret)
		ret = write_manifest(result_dir, "before", mappings);
	emit_case(out, "rq2_source_cache_manifest", !ret,
		  ret ? -ret : 0, "pre-condition cache manifest recorded");
	if (ret) {
		failures++;
		goto cleanup;
	}

	if (!strcmp(condition, "namei_ext"))
		ret = rq2_run_namei_condition(
			out, policy_path, result_dir, cgroup_root, test_dir,
			&environment, loader_argv, environment.namei_env, mappings,
			warmups, samples);
	else
		ret = rq2_run_fuse_condition(
			out, result_dir, cgroup_root, test_dir, &environment,
			loader_argv,
			environment.namei_env, mappings, warmups, samples);
	if (ret) {
		failures++;
		goto cleanup;
	}
	ret = validate_preservation(out, mappings);
	if (!ret)
		ret = write_manifest(result_dir, "after", mappings);
	emit_case(out, "rq2_source_and_cache_preservation", !ret,
		  ret ? -ret : 0,
		  "all source and Spindle cache payloads remain unchanged");
	if (ret)
		failures++;

cleanup:
	{
		bool process_found = false;
		int process_ret = process_name_starts_spindle(getpid(),
						       &process_found);

		emit_case(out, "rq2_final_process_quiescence",
			  !process_ret && !process_found,
			  process_ret ? -process_ret : 0,
			  "no Spindle executable remains at cleanup");
		failures += !!process_ret || process_found;
	}
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
		"{\"event\":\"spindle-staging-rq2-summary\","
		"\"condition\":\"%s\",\"focal_objects\":%d,"
		"\"source_logs\":%u,\"source_exit\":%d,"
		"\"warmups\":%u,\"samples\":%u,\"failures\":%d,"
		"\"pass\":%s}\n",
		condition, FOCAL_OBJECTS, log_count, source_result.exit_status,
		warmups, samples, failures, failures ? "false" : "true");
	fflush(out);
	fclose(out);
	return failures ? 1 : 0;
}

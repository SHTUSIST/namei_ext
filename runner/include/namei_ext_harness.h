/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NAMEI_EXT_HARNESS_H
#define NAMEI_EXT_HARNESS_H

#include <bpf/libbpf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct namei_ext_harness_policy {
	struct bpf_object *obj;
	int cgroup_fd;
	int prog_fd;
	bool attached;
};

struct namei_ext_target_registration {
	const char *path;
	uint32_t target_id;
};

int namei_ext_path_join(char *dst, size_t size, const char *dir,
			const char *name);
int namei_ext_write_text(const char *path, const char *value);
bool namei_ext_read_text_equals(const char *path, const char *expected);
int namei_ext_copy_file(const char *source, const char *destination);
void namei_ext_remove_tree(const char *path);

int namei_ext_move_self_to_cgroup(const char *cgroup_path);
int namei_ext_wait_child(pid_t pid);
int namei_ext_cgroup_id(const char *path, uint64_t *id_out);

int namei_ext_register_target(const char *cgroup_path,
			       const char *target_path, uint32_t target_id);
int namei_ext_register_target_batch(
	const char *cgroup_path,
	const struct namei_ext_target_registration *targets,
	size_t target_count);
int namei_ext_clear_targets(const char *cgroup_path);
int namei_ext_policy_parent_exact(const char *cgroup_path,
				   const char *parent_dir);
int namei_ext_policy_parent_add(const char *cgroup_path,
				 const char *parent_dir);
int namei_ext_policy_parent_clear(const char *cgroup_path);
int namei_ext_policy_parent_global(const char *cgroup_path);

int namei_ext_policy_load_attach(const char *obj_path,
				 const char *cgroup_path,
				 struct namei_ext_harness_policy *policy);
int namei_ext_policy_destroy(struct namei_ext_harness_policy *policy);
int namei_ext_component_map_update(
	struct namei_ext_harness_policy *policy, const char *map_name,
	uint64_t cgroup_id, const char *parent, const char *name,
	uint32_t value);
int namei_ext_component_map_delete(
	struct namei_ext_harness_policy *policy, const char *map_name,
	uint64_t cgroup_id, const char *parent, const char *name);
int namei_ext_component_map_lookup(
	struct namei_ext_harness_policy *policy, const char *map_name,
	uint64_t cgroup_id, const char *parent, const char *name,
	uint32_t *value_out);
int namei_ext_component_map_count(
	struct namei_ext_harness_policy *policy, const char *map_name,
	size_t *count_out);
int namei_ext_policy_counter(struct namei_ext_harness_policy *policy,
			     const char *map_name, uint32_t key,
			     uint64_t *value_out);

#endif /* NAMEI_EXT_HARNESS_H */

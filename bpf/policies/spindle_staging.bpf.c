// SPDX-License-Identifier: GPL-2.0

#include "namei_ext_policy.h"

enum spindle_staging_counter {
	SPINDLE_COUNTER_TOTAL = 0,
	SPINDLE_COUNTER_LOOKUP = 1,
	SPINDLE_COUNTER_SELECT = 2,
	SPINDLE_COUNTER_PASS = 3,
	SPINDLE_COUNTER_READDIR = 4,
	SPINDLE_COUNTER_HIDE_LOOKUP = 5,
	SPINDLE_COUNTER_HIDE_READDIR = 6,
	SPINDLE_COUNTER_MAX = 7,
};

#define SPINDLE_TARGET_MAX 64

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, SPINDLE_TARGET_MAX);
	__type(key, struct namei_ext_component_key);
	__type(value, __u32);
} spindle_staging_rules SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, SPINDLE_COUNTER_MAX);
	__type(key, __u32);
	__type(value, __u64);
} spindle_staging_counters SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, SPINDLE_TARGET_MAX);
	__type(key, __u32);
	__type(value, __u64);
} spindle_staging_rule_hits SEC(".maps");

static __inline void count_value(void *map, __u32 key)
{
	__u64 *value;

	value = bpf_map_lookup_elem(map, &key);
	if (value)
		__sync_fetch_and_add(value, 1);
}

SEC("cgroup/namei_ext")
int namei_ext_policy(struct bpf_namei_ext_ctx *ctx)
{
	struct namei_ext_component_key key = {};
	__u32 *target_id;

	count_value(&spindle_staging_counters, SPINDLE_COUNTER_TOTAL);
	if (ctx->event == BPF_NAMEI_EXT_LOOKUP)
		count_value(&spindle_staging_counters, SPINDLE_COUNTER_LOOKUP);
	else if (ctx->event == BPF_NAMEI_EXT_READDIR)
		count_value(&spindle_staging_counters, SPINDLE_COUNTER_READDIR);
	else {
		count_value(&spindle_staging_counters, SPINDLE_COUNTER_PASS);
		return BPF_NAMEI_EXT_PASS;
	}

	namei_ext_build_component_key(ctx, &key);
	key.event = 0;
	target_id = bpf_map_lookup_elem(&spindle_staging_rules, &key);
	if (!target_id) {
		count_value(&spindle_staging_counters, SPINDLE_COUNTER_PASS);
		return BPF_NAMEI_EXT_PASS;
	}
	if (!*target_id) {
		if (ctx->event == BPF_NAMEI_EXT_READDIR)
			count_value(&spindle_staging_counters,
				    SPINDLE_COUNTER_HIDE_READDIR);
		else
			count_value(&spindle_staging_counters,
				    SPINDLE_COUNTER_HIDE_LOOKUP);
		return BPF_NAMEI_EXT_HIDE;
	}
	if (ctx->event != BPF_NAMEI_EXT_LOOKUP ||
	    *target_id >= SPINDLE_TARGET_MAX) {
		count_value(&spindle_staging_counters, SPINDLE_COUNTER_PASS);
		return BPF_NAMEI_EXT_PASS;
	}

	ctx->target_id = *target_id;
	count_value(&spindle_staging_counters, SPINDLE_COUNTER_SELECT);
	count_value(&spindle_staging_rule_hits, *target_id);
	return BPF_NAMEI_EXT_SELECT_TARGET;
}

char _license[] SEC("license") = "GPL";

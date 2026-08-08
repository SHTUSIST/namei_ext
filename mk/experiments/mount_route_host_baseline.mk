# Host-kernel baselines for the mount-based routes to a per-task path view.
#
# These targets run on the stock host kernel and deliberately do NOT exercise
# namei_ext. They are not Phase 1 validation. Their purpose is to fill the one
# gap the project has never measured: what the existing mount-based routes cost
# per view, and what a read-only operation pays for a mount that also carries a
# writable layer.

MOUNT_ROUTE_BASELINE_BUILD_DIR ?= $(BUILD_ROOT)/mount-route-host-baseline
VIEW_SUPPLY_COST_HOLDER ?= $(MOUNT_ROUTE_BASELINE_BUILD_DIR)/view_holder
VIEW_SUPPLY_COST_SRC_DIR ?= $(ROOT_DIR)/experiments/view_supply_cost_host
VIEW_SUPPLY_COST_COLLECT ?= $(VIEW_SUPPLY_COST_SRC_DIR)/collect.py
VIEW_SUPPLY_COST_ANALYSIS ?= \
	$(ROOT_DIR)/analysis/view_supply_cost_host/analyze.py
VIEW_SUPPLY_COST_RESULT_DIR ?= \
	$(RESULT_ROOT)/experiments/view-supply-cost-host/$(RUN_ID)
VIEW_SUPPLY_COST_SCRATCH ?= $(MOUNT_ROUTE_BASELINE_BUILD_DIR)/scratch

# The view ladder stops at 1000 rather than the 10000 named in the experiment
# plan. Holding 10000 live mount namespaces needs 10000 resident processes,
# which does not fit in this host's memory beside the rest of its workload. The
# cap is recorded here and in the result review rather than left implicit.
VIEW_SUPPLY_COST_LADDER ?= 1,10,100,1000
VIEW_SUPPLY_COST_TRIALS ?= 3
VIEW_SUPPLY_COST_CLONE_REPS ?= 30
VIEW_SUPPLY_COST_SWITCH_REPS ?= 30
VIEW_SUPPLY_COST_PAGECACHE_VIEWS ?= 64
VIEW_SUPPLY_COST_CONTENT_FILES ?= 64
VIEW_SUPPLY_COST_CONTENT_FILE_BYTES ?= 262144
VIEW_SUPPLY_COST_BTRFS_IMAGE_BYTES ?= 2147483648
VIEW_SUPPLY_COST_MEMORY_GUARD_MB ?= 3072

OVERLAY_READ_TAX_SRC_DIR ?= $(ROOT_DIR)/experiments/overlay_read_path_tax
OVERLAY_READ_TAX_COLLECT ?= $(OVERLAY_READ_TAX_SRC_DIR)/collect.py
OVERLAY_READ_TAX_ANALYSIS ?= \
	$(ROOT_DIR)/analysis/overlay_read_path_tax/analyze.py
OVERLAY_READ_TAX_RESULT_DIR ?= \
	$(RESULT_ROOT)/experiments/overlay-read-path-tax/$(RUN_ID)
# The overlayfs mount option string must fit in one page, so a 50-layer stack
# is only expressible from a short scratch path. This default is short on
# purpose; a long one makes the deep conditions fail with ENOENT.
OVERLAY_READ_TAX_SCRATCH ?= /orpt
OVERLAY_READ_TAX_DEPTHS ?= 1,2,5,10,25,50
OVERLAY_READ_TAX_TRIALS ?= 3
OVERLAY_READ_TAX_WARM_REPS ?= 20000
OVERLAY_READ_TAX_COLD_REPS ?= 30
OVERLAY_READ_TAX_READDIR_REPS ?= 200
OVERLAY_READ_TAX_FILES ?= 2000
OVERLAY_READ_TAX_DIRS ?= 20
OVERLAY_READ_TAX_FILE_BYTES ?= 65536

.PHONY: mount-route-host-baseline view-supply-cost-host overlay-read-path-tax

mount-route-host-baseline: view-supply-cost-host overlay-read-path-tax

$(VIEW_SUPPLY_COST_HOLDER): $(VIEW_SUPPLY_COST_SRC_DIR)/view_holder.c
	install -d $(MOUNT_ROUTE_BASELINE_BUILD_DIR)
	$(MAKE) -C $(VIEW_SUPPLY_COST_SRC_DIR) \
		BUILD_DIR=$(MOUNT_ROUTE_BASELINE_BUILD_DIR)

view-supply-cost-host: $(VIEW_SUPPLY_COST_HOLDER)
	install -d "$(dir $(VIEW_SUPPLY_COST_RESULT_DIR))"
	python3 "$(VIEW_SUPPLY_COST_COLLECT)" \
		--result-dir "$(VIEW_SUPPLY_COST_RESULT_DIR)" \
		--scratch-dir "$(VIEW_SUPPLY_COST_SCRATCH)" \
		--holder-binary "$(VIEW_SUPPLY_COST_HOLDER)" \
		--ladder "$(VIEW_SUPPLY_COST_LADDER)" \
		--trials $(VIEW_SUPPLY_COST_TRIALS) \
		--mountns-clone-reps $(VIEW_SUPPLY_COST_CLONE_REPS) \
		--switch-reps $(VIEW_SUPPLY_COST_SWITCH_REPS) \
		--page-cache-views $(VIEW_SUPPLY_COST_PAGECACHE_VIEWS) \
		--content-files $(VIEW_SUPPLY_COST_CONTENT_FILES) \
		--content-file-bytes $(VIEW_SUPPLY_COST_CONTENT_FILE_BYTES) \
		--btrfs-image-bytes $(VIEW_SUPPLY_COST_BTRFS_IMAGE_BYTES) \
		--memory-guard-mb $(VIEW_SUPPLY_COST_MEMORY_GUARD_MB)
	python3 "$(VIEW_SUPPLY_COST_ANALYSIS)" \
		--result-dir "$(VIEW_SUPPLY_COST_RESULT_DIR)"

overlay-read-path-tax:
	install -d "$(dir $(OVERLAY_READ_TAX_RESULT_DIR))"
	python3 "$(OVERLAY_READ_TAX_COLLECT)" \
		--result-dir "$(OVERLAY_READ_TAX_RESULT_DIR)" \
		--scratch-dir "$(OVERLAY_READ_TAX_SCRATCH)" \
		--depths "$(OVERLAY_READ_TAX_DEPTHS)" \
		--trials $(OVERLAY_READ_TAX_TRIALS) \
		--warm-reps $(OVERLAY_READ_TAX_WARM_REPS) \
		--cold-reps $(OVERLAY_READ_TAX_COLD_REPS) \
		--readdir-reps $(OVERLAY_READ_TAX_READDIR_REPS) \
		--files $(OVERLAY_READ_TAX_FILES) \
		--dirs $(OVERLAY_READ_TAX_DIRS) \
		--file-bytes $(OVERLAY_READ_TAX_FILE_BYTES)
	python3 "$(OVERLAY_READ_TAX_ANALYSIS)" \
		--result-dir "$(OVERLAY_READ_TAX_RESULT_DIR)"

SHELL := /bin/bash
.SHELLFLAGS := -e -o pipefail -c

ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
KERNEL_DIR := $(ROOT_DIR)/kernel
BUILD_ROOT ?= $(ROOT_DIR)/.build
CACHE_ROOT ?= $(ROOT_DIR)/.cache
RESULT_ROOT ?= $(ROOT_DIR)/results

NPROC ?= $(shell nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
JOBS ?= $(NPROC)
ifeq ($(origin RUN_ID), undefined)
RUN_ID := $(shell date -u +%Y%m%dT%H%M%SZ)-$(shell od -An -N4 -tx4 /dev/urandom 2>/dev/null | tr -d ' \n')
endif

include $(ROOT_DIR)/mk/suites.mk
include $(ROOT_DIR)/configs/kvm/x86_64.mk
include $(ROOT_DIR)/configs/benchmarks/phase1.mk
include $(ROOT_DIR)/configs/benchmarks/fxmark.mk
include $(ROOT_DIR)/configs/benchmarks/fxmark_fast_path.mk
include $(ROOT_DIR)/configs/benchmarks/fxmark_readdir.mk
include $(ROOT_DIR)/configs/benchmarks/mdtest_cold_metadata.mk
include $(ROOT_DIR)/configs/benchmarks/namei_ext_target_lifetime.mk
include $(ROOT_DIR)/configs/benchmarks/agent_workspace.mk
include $(ROOT_DIR)/configs/benchmarks/agent_workspace_source_task.mk
include $(ROOT_DIR)/configs/benchmarks/application_file_sharing.mk
include $(ROOT_DIR)/configs/benchmarks/build_action_sandboxing.mk
include $(ROOT_DIR)/configs/benchmarks/build_action_rq2.mk
include $(ROOT_DIR)/configs/benchmarks/service_config_rotation.mk
include $(ROOT_DIR)/configs/benchmarks/checkpoint_restore.mk
include $(ROOT_DIR)/configs/benchmarks/spindle_staging.mk
include $(ROOT_DIR)/configs/benchmarks/spindle_staging_rq2.mk
include $(ROOT_DIR)/configs/benchmarks/toolchain_environment.mk
include $(ROOT_DIR)/configs/benchmarks/kubernetes_configmap_publication.mk
include $(ROOT_DIR)/configs/benchmarks/semantic_continuation.mk
include $(ROOT_DIR)/mk/kernel.mk
include $(ROOT_DIR)/mk/docker.mk
include $(ROOT_DIR)/mk/results.mk
include $(ROOT_DIR)/mk/multi_boot.mk
include $(ROOT_DIR)/mk/kvm.mk
include $(ROOT_DIR)/mk/workload.mk
include $(ROOT_DIR)/mk/experiments/legacy_build_cache.mk
include $(ROOT_DIR)/mk/experiments/agent_workspace.mk
include $(ROOT_DIR)/mk/experiments/agent_workspace_source_task.mk
include $(ROOT_DIR)/mk/experiments/agent_workspace_rq2.mk
include $(ROOT_DIR)/mk/experiments/agent_workspace_rq3.mk
include $(ROOT_DIR)/mk/experiments/application_file_sharing.mk
include $(ROOT_DIR)/mk/experiments/build_action_sandboxing.mk
include $(ROOT_DIR)/mk/experiments/build_action_rq2.mk
include $(ROOT_DIR)/mk/experiments/service_config_rotation.mk
include $(ROOT_DIR)/mk/experiments/checkpoint_restore.mk
include $(ROOT_DIR)/mk/experiments/spindle_staging.mk
include $(ROOT_DIR)/mk/experiments/spindle_staging_rq2.mk
include $(ROOT_DIR)/mk/experiments/toolchain_environment.mk
include $(ROOT_DIR)/mk/experiments/kubernetes_configmap_publication.mk
include $(ROOT_DIR)/mk/experiments/semantic_continuation.mk
include $(ROOT_DIR)/mk/benchmarks/fxmark.mk
include $(ROOT_DIR)/mk/experiments/fxmark_fast_path.mk
include $(ROOT_DIR)/mk/experiments/fxmark_readdir.mk
include $(ROOT_DIR)/mk/experiments/mdtest_cold_metadata.mk
include $(ROOT_DIR)/mk/experiments/namei_ext_target_lifetime.mk
include $(ROOT_DIR)/mk/experiments/mount_route_host_baseline.mk

.DEFAULT_GOAL := phase1

.PHONY: all phase1 phase1-smoke check-prereqs result-contract abi bpf bench functional \
	policy-load policy-semantic runner agent-workspace \
	agent-workspace-source-task application-file-sharing \
	build-action-sandboxing service-config-rotation checkpoint-restore \
	checkpoint-restore-source-feasibility \
	kvm-checkpoint-restore-preflight kvm-checkpoint-restore-rq1 \
	checkpoint-restore-run checkpoint-restore-finalize \
	checkpoint-restore-analyze checkpoint-restore-analysis-test \
	kvm-build-action-sandboxing-preflight kvm-build-action-sandboxing-rq1 \
	build-action-sandboxing-run build-action-sandboxing-finalize \
	build-action-sandboxing-analyze experiment-build-action-sandboxing-rq1 \
	spindle-staging kvm-spindle-staging-preflight kvm-spindle-staging \
	spindle-staging-run-matrix spindle-staging-finalize \
	spindle-staging-analyze experiment-spindle-staging \
	spindle-staging-rq2-build spindle-staging-rq2-host-gate \
	kvm-spindle-staging-rq2-preflight \
	kvm-spindle-staging-rq2 spindle-staging-rq2-run-matrix \
	spindle-staging-rq2-finalize spindle-staging-rq2-report \
	experiment-spindle-staging-rq2 \
	toolchain-environment kvm-toolchain-environment-preflight \
	kvm-toolchain-environment toolchain-environment-run \
	toolchain-environment-finalize toolchain-environment-analyze \
	experiment-toolchain-environment \
	kubernetes-configmap-publication kubernetes-configmap-publication-source \
	kvm-kubernetes-configmap-publication-rq1-preflight \
	kvm-kubernetes-configmap-publication-rq1 \
	kubernetes-configmap-publication-run \
	kubernetes-configmap-publication-finalize \
	kubernetes-configmap-publication-analyze \
	experiment-kubernetes-configmap-publication-rq1 \
	semantic-continuation kvm-semantic-continuation-preflight \
	kvm-semantic-continuation semantic-continuation-run \
	semantic-continuation-finalize semantic-continuation-analyze \
	experiment-semantic-continuation \
	kvm-agent-workspace-source-task-rq1-preflight \
	kvm-agent-workspace-source-task-rq1 \
	agent-workspace-source-task-run \
	agent-workspace-source-task-finalize \
	agent-workspace-source-task-analyze \
	experiment-agent-workspace-source-task-rq1 \
	kvm-application-file-sharing-rq1 application-file-sharing-run \
	application-file-sharing-finalize application-file-sharing-analyze \
	experiment-application-file-sharing-rq1 \
	application-file-sharing-source \
	kvm-application-file-sharing-source-oracle-preflight \
	kvm-application-file-sharing-source-oracle-rq1 \
	application-file-sharing-source-oracle-run \
	application-file-sharing-source-oracle-finalize \
	application-file-sharing-source-oracle-analyze \
	experiment-application-file-sharing-source-oracle-rq1 \
	application-file-sharing-rq2-analysis-test \
	application-file-sharing-rq2-official-host-gate \
	kvm-application-file-sharing-rq2-official-preflight \
	kvm-application-file-sharing-rq2-official \
	application-file-sharing-rq2-official-run \
	application-file-sharing-rq2-official-finalize \
	application-file-sharing-rq2-official-analyze \
	experiment-application-file-sharing-rq2-official \
	fxmark-rq2-build fxmark-kernel-pair kvm-fxmark-rq2-preflight \
	kvm-fxmark-rq2 fxmark-rq2-report experiment-fxmark-rq2 \
	kvm-fxmark-fast-path-preflight kvm-fxmark-fast-path \
	fxmark-fast-path-report experiment-fxmark-fast-path \
	kvm-fxmark-readdir-preflight kvm-fxmark-readdir \
	fxmark-readdir-report experiment-fxmark-readdir \
	mdtest-cold-metadata-source mdtest-cold-metadata-build \
		mdtest-cold-metadata-source-feasibility \
		kvm-mdtest-cold-metadata-preflight kvm-mdtest-cold-metadata-rq2 \
		mdtest-cold-metadata-analyze \
		mdtest-cold-metadata-analysis-test \
		experiment-mdtest-cold-metadata-rq2 \
		namei-ext-target-lifetime namei-ext-target-lifetime-control \
		namei-ext-target-lifetime-analysis-test \
		namei-ext-target-lifetime-debug-kernels \
		kvm-namei-ext-target-lifetime-preflight \
		kvm-namei-ext-target-lifetime \
		experiment-namei-ext-target-lifetime \
	kvm-service-config-rotation-preflight kvm-service-config-rotation \
	service-config-rotation-report experiment-service-config-rotation \
	experiments current-experiment-gates formal-case-studies formal-performance \
	$(NAMEI_EXT_HISTORICAL_TARGETS) \
	w1-oracle \
	help clean clean-results
.NOTPARALLEL: phase1 experiments current-experiment-gates formal-case-studies \
	formal-performance

$(CLEAN_SOURCE_EXPERIMENT_TARGETS): NAMEI_EXT_REQUIRE_CLEAN = 1
$(CLEAN_SOURCE_EXPERIMENT_TARGETS): experiment-source-clean

all: phase1

phase1: phase1-smoke kvm-policy-load kvm-functional

phase1-smoke: check-prereqs result-contract abi bpf functional bench policy-load policy-semantic kernel-objects kvm-smoke

experiments: $(CURRENT_EXPERIMENT_TARGETS)

current-experiment-gates: $(NAMEI_EXT_CURRENT_GATE_TARGETS)

formal-case-studies: $(NAMEI_EXT_FORMAL_CASE_STUDY_TARGETS)

formal-performance: $(NAMEI_EXT_FORMAL_PERFORMANCE_TARGETS)

legacy-build-cache: kvm-build-cache-matrix

check-prereqs:
	command -v make >/dev/null
	command -v clang >/dev/null
	command -v docker >/dev/null
	command -v flock >/dev/null
	command -v jq >/dev/null
	command -v patch >/dev/null
	command -v pkg-config >/dev/null
	command -v $(VNG) >/dev/null
	command -v pahole >/dev/null
	test -r /dev/kvm
	test -w /dev/kvm

result-contract:
	$(MAKE) -C "$(ROOT_DIR)/tests/infrastructure" \
		ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" test

bpf:
	$(MAKE) -C "$(ROOT_DIR)/bpf" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

abi:
	$(MAKE) -C "$(ROOT_DIR)/tests/abi" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" RESULT_DIR="$(PHASE1_RESULT_DIR)" run

bench:
	$(MAKE) -C "$(ROOT_DIR)/bench/workloads" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

functional:
	$(MAKE) -C "$(ROOT_DIR)/tests/functional" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

policy-load:
	$(MAKE) -C "$(ROOT_DIR)/tests/policy_load" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

policy-semantic:
	$(MAKE) -C "$(ROOT_DIR)/tests/policy_semantic" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

runner:
	$(MAKE) -C "$(ROOT_DIR)/runner" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

w1-oracle:
	$(MAKE) -C "$(ROOT_DIR)/experiments/legacy_oracle" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

agent-workspace:
	$(MAKE) -C "$(ROOT_DIR)/experiments/agent_workspace" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

application-file-sharing:
	$(MAKE) -C "$(ROOT_DIR)/experiments/application_file_sharing" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

build-action-sandboxing:
	$(MAKE) -C "$(ROOT_DIR)/experiments/build_action_sandboxing" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

service-config-rotation:
	$(MAKE) -C "$(ROOT_DIR)/experiments/service_config_rotation" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

checkpoint-restore: checkpoint-restore-dmtcp-build
	$(MAKE) -C "$(ROOT_DIR)/experiments/checkpoint_restore" \
		ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" CACHE_ROOT="$(CACHE_ROOT)" \
		RESULT_ROOT="$(RESULT_ROOT)" \
		DMTCP_INSTALL="$(CHECKPOINT_RESTORE_DMTCP_INSTALL)" all

help:
	@printf '%s\n' 'Targets:'
	@printf '%s\n' ''
	@printf '%s\n' 'Current validation:'
	@printf '%s\n' '  make phase1          run current prototype validation: host checks, KVM smoke, policy load, functional KVM'
	@printf '%s\n' '  make phase1-smoke    check tools, build userspace/BPF, compile touched kernel objects, boot KVM smoke'
	@printf '%s\n' ''
	@printf '%s\n' 'Formal experiment lifecycle:'
	@printf '%s\n' '  make experiments'
	@printf '%s\n' '                       run every current case-study matrix or preflight through the shared KVM/result lifecycle'
	@printf '%s\n' '  make current-experiment-gates'
	@printf '%s\n' '                       run the currently implemented dependency and case-study gates'
	@printf '%s\n' '  make formal-case-studies'
	@printf '%s\n' '                       run formal case studies whose dependency gates are complete'
	@printf '%s\n' '  make formal-performance'
	@printf '%s\n' '                       run formal FxMark attached-path and unused-fast-path matrices'
	@printf '%s\n' '  make kvm-agent-workspace-matrix'
	@printf '%s\n' '                       run the Agent workspace lifecycle matrix with namei_ext and FUSE'
	@printf '%s\n' '  make kvm-agent-workspace-preflight'
	@printf '%s\n' '                       boot KVM and run the Agent workspace dependency preflight'
	@printf '%s\n' '  make kvm-agent-workspace-rq2-preflight'
	@printf '%s\n' '                       run the paired two-boot Agent workspace RQ2 preflight'
	@printf '%s\n' '  make experiment-agent-workspace-rq2'
	@printf '%s\n' '                       run ten paired namei_ext/FUSE boots and generate the RQ2 report'
	@printf '%s\n' '  make experiment-agent-workspace-rq3'
	@printf '%s\n' '                       run the matched namei_ext/Wrapfs boundary and fail-closed matrix'
	@printf '%s\n' '  make kvm-agent-workspace-source-task-rq1-preflight'
	@printf '%s\n' '                       run one real Click issue task through concurrent Agent workspace views'
	@printf '%s\n' '  make experiment-agent-workspace-source-task-rq1'
	@printf '%s\n' '                       run three fresh Click source-task workspace boots for RQ1'
	@printf '%s\n' '  make kvm-application-file-sharing-source-oracle-preflight'
	@printf '%s\n' '                       run official xdg-document-portal and namei_ext through the same five-state oracle in one KVM boot'
	@printf '%s\n' '  make experiment-application-file-sharing-source-oracle-rq1'
	@printf '%s\n' '                       run three fresh source-faithful application-file-sharing boots and summarize RQ1 evidence'
	@printf '%s\n' '  make kvm-application-file-sharing-rq2-official-preflight'
	@printf '%s\n' '                       run one paired official portal/namei_ext performance preflight'
	@printf '%s\n' '  make experiment-application-file-sharing-rq2-official'
	@printf '%s\n' '                       run ten paired official portal/namei_ext boots and generate the RQ2 report'
	@printf '%s\n' '  make kvm-build-action-sandboxing-preflight'
	@printf '%s\n' '                       run one real Bazel action-view boot with the current allowlist policy'
	@printf '%s\n' '  make experiment-build-action-sandboxing-rq1'
	@printf '%s\n' '                       run three fresh Bazel action-view boots and summarize RQ1 evidence'
	@printf '%s\n' '                       run two concurrent source-derived Bazel actions through namei_ext in KVM'
	@printf '%s\n' '  make kvm-build-action-rq2-preflight'
	@printf '%s\n' '                       run one paired namei_ext/official-sandboxfs Bazel action-view preflight'
	@printf '%s\n' '  make kvm-service-config-rotation-preflight'
	@printf '%s\n' '                       run one live nginx current/canary/invalid/rollback state machine in KVM'
	@printf '%s\n' '  make experiment-service-config-rotation'
	@printf '%s\n' '                       run ten fresh nginx rotation boots and generate the RQ1 report'
	@printf '%s\n' '  make checkpoint-restore-pathvirt-host-preflight'
	@printf '%s\n' '                       validate patched DMTCP pathvirt restart mapping before KVM integration'
	@printf '%s\n' '  make kvm-checkpoint-restore-preflight'
	@printf '%s\n' '                       run patched DMTCP, namei_ext, and withdrawn control in one modified-kernel boot'
	@printf '%s\n' '  make kvm-spindle-staging-preflight'
	@printf '%s\n' '                       run one source-derived Spindle staging boot with 47 focal objects'
	@printf '%s\n' '  make experiment-spindle-staging'
	@printf '%s\n' '                       run three fresh Spindle staging boots and generate the RQ1 report'
	@printf '%s\n' '  make kvm-toolchain-environment-preflight'
	@printf '%s\n' '                       run CPython 3.10/3.12 environment selection, switch, rollback, and controls in one KVM boot'
	@printf '%s\n' '  make experiment-toolchain-environment'
	@printf '%s\n' '                       run three fresh toolchain-environment boots and generate the RQ1 report'
	@printf '%s\n' '  make kvm-kubernetes-configmap-publication-rq1-preflight'
	@printf '%s\n' '                       run one Kubernetes AtomicWriter publication workload boot in KVM'
	@printf '%s\n' '  make experiment-kubernetes-configmap-publication-rq1'
	@printf '%s\n' '                       run three fresh ConfigMap publication boots and generate the RQ1 report'
	@printf '%s\n' '  make kvm-semantic-continuation-preflight'
	@printf '%s\n' '                       check selected-path create and two-target rename in one modified-kernel boot'
	@printf '%s\n' '  make experiment-semantic-continuation'
	@printf '%s\n' '                       compare direct and selected VFS semantics across the frozen 16-case matrix'
	@printf '%s\n' '  make kvm-fxmark-rq2-preflight'
	@printf '%s\n' '                       run one real MRPL cell in six isolated stock/patched/FUSE KVM boots'
	@printf '%s\n' '  make kvm-fxmark-rq2'
	@printf '%s\n' '                       run the complete paired FxMark RQ2 matrix in isolated KVM boots'
	@printf '%s\n' '  make experiment-fxmark-rq2'
	@printf '%s\n' '                       run the complete FxMark matrix and generate its statistical report and figure'
	@printf '%s\n' '  make kvm-fxmark-fast-path-preflight'
	@printf '%s\n' '                       run one host-pinned stock/unattached paired FxMark block'
	@printf '%s\n' '  make experiment-fxmark-fast-path'
	@printf '%s\n' '                       run the frozen 30-block unused-fast-path confirmation and report'
	@printf '%s\n' '  make kvm-fxmark-readdir-preflight'
	@printf '%s\n' '                       run one host-pinned corrected MRDL/MRDM five-condition block'
	@printf '%s\n' '  make experiment-fxmark-readdir'
	@printf '%s\n' '                       run the frozen corrected directory-enumeration matrix and report'
	@printf '%s\n' '  make kvm-mdtest-cold-metadata-preflight'
	@printf '%s\n' '                       run one five-condition mdtest create/cold-stat/cold-remove preflight block'
	@printf '%s\n' '  make experiment-mdtest-cold-metadata-rq2'
	@printf '%s\n' '                       run the reviewed ten-block mdtest cold/mutating metadata matrix'
	@printf '%s\n' ''
	@printf '%s\n' 'Historical experiment reproduction:'
	@printf '%s\n' '  make legacy-build-cache'
	@printf '%s\n' '                       reproduce the isolated Redis/nginx ccache matrix; not part of make experiments'
	@printf '%s\n' ''
	@printf '%s\n' 'Build and component checks:'
	@printf '%s\n' '  make kernel-config   build the committed x86_64 Phase 1 kernel config'
	@printf '%s\n' '  make kernel-objects  compile fs/namei.o fs/readdir.o fs/namei_ext.o'
	@printf '%s\n' '  make kernel          build the Phase 1 bzImage'
	@printf '%s\n' '  make docker          build and save the Phase 1 runtime image'
	@printf '%s\n' '  make docker-smoke    run the runtime image without a workspace bind mount'
	@printf '%s\n' '  make kvm-smoke       boot the modified kernel and run guest smoke'
	@printf '%s\n' '  make kvm-policy-load boot the modified kernel and load/attach all BPF policies'
	@printf '%s\n' '  make kvm-policy-semantic boot the modified kernel and check policy-family semantics'
	@printf '%s\n' '  make kvm-functional  boot the modified kernel and run functional tests'
	@printf '%s\n' '  make kvm-bench       boot the modified kernel and run microbenchmarks'
	@printf '%s\n' '  make abi             build and run ABI layout checks'
	@printf '%s\n' '  make bpf             build BPF component outputs'
	@printf '%s\n' '  make functional      build functional-test component outputs'
	@printf '%s\n' '  make bench           build benchmark component outputs'
	@printf '%s\n' ''
	@printf '%s\n' 'Cleanup:'
	@printf '%s\n' '  make clean           remove build/cache outputs, keep results'
	@printf '%s\n' '  make clean-results   remove Phase 1 results'

clean: kernel-lock-ready docker-clean
	exec 9>"$(KERNEL_BUILD_LOCK)"; \
	flock 9; \
	$(MAKE) -C "$(ROOT_DIR)/tests/abi" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/bpf" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/bench/workloads" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/tests/functional" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/tests/policy_load" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/tests/policy_semantic" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/tests/infrastructure" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/experiments/legacy_oracle" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/runner" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/experiments/agent_workspace" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/experiments/application_file_sharing" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/experiments/build_action_sandboxing" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/experiments/service_config_rotation" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/experiments/spindle_staging" BUILD_ROOT="$(BUILD_ROOT)" clean; \
	$(MAKE) -C "$(ROOT_DIR)/bench/fxmark" ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" OUTPUT="$(BUILD_ROOT)/fxmark-rq2" clean; \
	rm -rf "$(BUILD_ROOT)/workloads" "$(CACHE_ROOT)/workloads"; \
	rm -rf "$(BUILD_ROOT)"

clean-results:
	rm -rf "$(RESULT_ROOT)/phase1"

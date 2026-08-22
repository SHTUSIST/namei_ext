BUILD_ACTION_RQ2_RESULT_DIR ?= \
	$(RESULT_ROOT)/experiments/build-action-rq2/$(RUN_ID)
BUILD_ACTION_RQ2_PREFLIGHT_RESULT_DIR ?= \
	$(RESULT_ROOT)/experiments/build-action-rq2-preflight/$(RUN_ID)
BUILD_ACTION_RQ2_RUNNER ?= \
	$(BUILD_ROOT)/build-action-sandboxing/namei_ext_build_action_rq2
BUILD_ACTION_RQ2_RUNNER_SOURCE ?= \
	$(ROOT_DIR)/experiments/build_action_sandboxing/namei_ext_build_action_rq2.c
BUILD_ACTION_RQ2_POLICY ?= \
	$(BUILD_ROOT)/bpf/build_action_sandboxing.bpf.o
BUILD_ACTION_RQ2_POLICY_SOURCE ?= \
	$(ROOT_DIR)/bpf/policies/build_action_sandboxing.bpf.c
BUILD_ACTION_RQ2_BPFTOOL ?= $(KERNEL_BPFTOOL)
BUILD_ACTION_RQ2_ANALYSIS ?= \
	$(ROOT_DIR)/analysis/build_action_rq2/analyze.py
BUILD_ACTION_RQ2_PLAN ?= \
	$(ROOT_DIR)/docs/tmp/2026-08-07-build-action-rq2-reentry-plan.md
BUILD_ACTION_RQ2_PLAN_REVIEW ?= \
	$(ROOT_DIR)/docs/tmp/2026-08-07-build-action-rq2-reentry-plan-review.md
BUILD_ACTION_RQ2_SOURCE_AUDIT ?= \
	$(ROOT_DIR)/docs/tmp/2026-07-28-sandboxfs-0.2.0-protocol-audit.md
BUILD_ACTION_RQ2_BOOT_FILES := \
	guest.mk launcher.stdout.log launcher.stderr.log \
	vcpu-affinity.json affinity-barrier.txt boot.json observations.jsonl \
	stdout.log stderr.log runner.ready runner.release runner.status \
	sandboxfs.stderr.log bpf-programs-before.json \
	bpf-programs-middle.json bpf-programs-after.json \
	bpf-cgroup-before.json bpf-cgroup-middle.json bpf-cgroup-after.json \
	fuse-mounts-before.txt fuse-mounts-middle.txt fuse-mounts-after.txt \
	fuse-open-fds-before.txt fuse-open-fds-before.status \
	fuse-open-fds-middle.txt fuse-open-fds-middle.status \
	fuse-open-fds-after.txt fuse-open-fds-after.status \
	kernel.config kernel-commit.txt kernel-build-id.txt kernel-release.txt \
	clocksource-before.txt clocksource-after.txt uname.txt proc-version.txt \
	kernel-cmdline.txt proc-stat-before.txt proc-stat-after.txt dmesg.log
BUILD_ACTION_RQ2_HOST_FILES := \
	host-lscpu.txt host-lscpu-extended.txt host-cpu-pin.txt \
	host-cpu-pin.json host-cpu-frequency-policy.txt vng-version.txt \
	vng-executable.txt vng-run-module.txt host-proc-stat-before.txt \
	host-proc-stat-after.txt host-proc-interrupts-before.txt \
	host-proc-interrupts-after.txt

define BUILD_ACTION_RQ2_VALIDATE_HOST_PIN
$(call NAMEI_EXT_VALIDATE_HOST_CPU_PIN,$(BUILD_ACTION_RQ2_HOST_CPUS),$(KVM_CPUS))
endef

define BUILD_ACTION_RQ2_CAPTURE_HOST
lscpu >"$(1)/host-lscpu.txt"
lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE,MAXMHZ,MINMHZ \
	>"$(1)/host-lscpu-extended.txt"
cat /proc/stat >"$(1)/host-proc-stat-before.txt"
cat /proc/interrupts >"$(1)/host-proc-interrupts-before.txt"
printf '%s\n' "$(2)" >"$(1)/host-cpu-pin.txt"
pin_start=$$(printf '%s\n' "$(2)" | cut -d- -f1); \
pin_end=$$(printf '%s\n' "$(2)" | cut -d- -f2); \
jq -n --argjson start "$$pin_start" --argjson end "$$pin_end" \
	'[range($$start; $$end + 1)]' >"$(1)/host-cpu-pin.json"; \
printf 'intel_pstate_no_turbo=%s\n' \
	"$$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)" \
	>"$(1)/host-cpu-frequency-policy.txt"; \
for cpu in $$(seq "$$pin_start" "$$pin_end"); do \
	printf 'cpu=%s governor=%s driver=%s max_khz=%s\n' "$$cpu" \
		"$$(cat "/sys/devices/system/cpu/cpu$$cpu/cpufreq/scaling_governor")" \
		"$$(cat "/sys/devices/system/cpu/cpu$$cpu/cpufreq/scaling_driver")" \
		"$$(cat "/sys/devices/system/cpu/cpu$$cpu/cpufreq/cpuinfo_max_freq")" \
		>>"$(1)/host-cpu-frequency-policy.txt"; \
done
vng_path=$$(command -v "$(VNG)"); \
vng_module_path=$$(python3 -c 'import virtme_ng.run; print(virtme_ng.run.__file__)'); \
"$(VNG)" --version >"$(1)/vng-version.txt"; \
printf '%s\n' "$$vng_path" >"$(1)/vng-executable.txt"; \
printf '%s\n' "$$vng_module_path" >"$(1)/vng-run-module.txt"
endef

define BUILD_ACTION_RQ2_VALIDATE_HOST_FILES
for file in $(BUILD_ACTION_RQ2_HOST_FILES); do \
	test -s "$(1)/$$file"; \
done
endef

define BUILD_ACTION_RQ2_VALIDATE_RUN_BASE
jq -e --arg schema "$(NAMEI_EXT_RUN_SCHEMA)" --arg run_id "$(RUN_ID)" --arg observations "$(notdir $(2))" '.schema == $$schema and .run_id == $$run_id and .status == "running" and (.completed_at | not) and .observations == $$observations and (.source.commit | type == "string" and length == 40) and (.source.dirty | type == "boolean") and (.kernel.commit | type == "string" and length == 40) and (.kernel.dirty | type == "boolean") and .kernel_commit == .kernel.commit' "$(1)/run.json" >/dev/null
test -s "$(1)/run.json"
test -s "$(2)"
for file in command.txt source-commit.txt kernel-commit.txt; do test -s "$(1)/$$file"; done
for file in source-status.txt kernel-status.txt; do test -e "$(1)/$$file"; done
test "$$(cat "$(1)/source-commit.txt")" = "$$(jq -r '.source.commit' "$(1)/run.json")"
test "$$(cat "$(1)/kernel-commit.txt")" = "$$(jq -r '.kernel.commit' "$(1)/run.json")"
test "$$(test -s "$(1)/source-status.txt" && printf true || printf false)" = "$$(jq -r '.source.dirty' "$(1)/run.json")"
test "$$(test -s "$(1)/kernel-status.txt" && printf true || printf false)" = "$$(jq -r '.kernel.dirty' "$(1)/run.json")"
endef

define BUILD_ACTION_RQ2_CAPTURE_ARTIFACTS
test -x "$(BAZEL_BINARY)"
test "$$($(BAZEL_BINARY) --version)" = "bazel $(BAZEL_VERSION)"
test -x "$(SANDBOXFS_BINARY)"
test "$$($(SANDBOXFS_BINARY) --version)" = \
	"sandboxfs $(SANDBOXFS_VERSION)"
test "$$(pkg-config --modversion fuse)" = "$(SANDBOXFS_LIBFUSE_VERSION)"
test -x "$(BUILD_ACTION_RQ2_BPFTOOL)"
test "$$(cat "$(KERNEL_BPFTOOL_SOURCE_STAMP)")" = \
	"$$(git -C "$(KERNEL_DIR)" rev-parse HEAD)"
grep -aFq 'cgroup/namei_ext' "$(BUILD_ACTION_RQ2_BPFTOOL)"
install -d "$(1)/artifacts/kernel" "$(1)/artifacts/runtime" \
	"$(1)/artifacts/source/sandboxfs"
install -m 0444 "$(KERNEL_IMAGE)" "$(1)/artifacts/kernel/bzImage"
install -m 0444 "$(KERNEL_BUILD_DIR)/.config" \
	"$(1)/artifacts/kernel/config"
install -m 0555 "$(BUILD_ACTION_RQ2_RUNNER)" \
	"$(1)/artifacts/runtime/namei_ext_build_action_rq2"
install -m 0444 "$(BUILD_ACTION_RQ2_POLICY)" \
	"$(1)/artifacts/runtime/build_action_sandboxing.bpf.o"
install -m 0555 "$(SANDBOXFS_BINARY)" \
	"$(1)/artifacts/runtime/sandboxfs"
install -m 0444 "$(SANDBOXFS_LIBFUSE_RUNTIME)" \
	"$(1)/artifacts/runtime/libfuse.so.2"
install -m 0555 "$(BAZEL_BINARY)" "$(1)/artifacts/runtime/bazel"
install -m 0555 "$(BUILD_ACTION_RQ2_BPFTOOL)" \
	"$(1)/artifacts/runtime/bpftool"
install -m 0444 "$(SANDBOXFS_ARCHIVE)" \
	"$(1)/artifacts/source/sandboxfs/source.tar.gz"
install -m 0444 "$(SANDBOXFS_CARGO_LOCK)" \
	"$(1)/artifacts/source/sandboxfs/Cargo.lock"
install -m 0444 "$(SANDBOXFS_BUILD_PROVENANCE)" \
	"$(1)/artifacts/source/sandboxfs/build.json"
install -m 0444 "$(SANDBOXFS_BUILD_LOG)" \
	"$(1)/artifacts/source/sandboxfs/build.log"
install -m 0444 "$(SANDBOXFS_LDD)" \
	"$(1)/artifacts/source/sandboxfs/ldd.txt"
jq -n \
	--arg kernel_commit "$$(cat "$(KERNEL_COMMIT_FILE)")" \
	--arg kernel_release "$$(sed -n 's/^#define UTS_RELEASE "\(.*\)"/\1/p' "$(KERNEL_RELEASE_HEADER)")" \
	--arg kernel_build_id "$$(readelf -n "$(KERNEL_BUILD_DIR)/vmlinux" | awk '/Build ID:/ {print $$3; exit}')" \
	--arg sandboxfs_commit "$(SANDBOXFS_COMMIT)" \
	--arg sandboxfs_version "$(SANDBOXFS_VERSION)" \
	--arg libfuse_version "$(SANDBOXFS_LIBFUSE_VERSION)" \
	--arg bazel_version "$$("$(BAZEL_BINARY)" --version)" \
	--arg bpftool_version "$$("$(1)/artifacts/runtime/bpftool" version)" \
	'{kernel:{commit:$$kernel_commit,release:$$kernel_release,build_id:$$kernel_build_id,image:"artifacts/kernel/bzImage",config:"artifacts/kernel/config"},runtime:{runner:"artifacts/runtime/namei_ext_build_action_rq2",policy:"artifacts/runtime/build_action_sandboxing.bpf.o",sandboxfs:"artifacts/runtime/sandboxfs",libfuse:"artifacts/runtime/libfuse.so.2",bazel:"artifacts/runtime/bazel",bpftool:"artifacts/runtime/bpftool",bpftool_version:$$bpftool_version,bpftool_source_commit:$$kernel_commit},source:{sandboxfs:{commit:$$sandboxfs_commit,version:$$sandboxfs_version,archive:"artifacts/source/sandboxfs/source.tar.gz",cargo_lock:"artifacts/source/sandboxfs/Cargo.lock",build:"artifacts/source/sandboxfs/build.json",build_log:"artifacts/source/sandboxfs/build.log",ldd:"artifacts/source/sandboxfs/ldd.txt",libfuse_version:$$libfuse_version},bazel:{version:$$bazel_version}}}' \
	>"$(1)/artifacts/manifest.json"
jq -e '.kernel.commit | length == 40' \
	"$(1)/artifacts/manifest.json" >/dev/null
jq -e '.kernel.release | length > 0' \
	"$(1)/artifacts/manifest.json" >/dev/null
jq -e '.kernel.build_id | length > 0' \
	"$(1)/artifacts/manifest.json" >/dev/null
jq -e '.runtime.bpftool_version | type == "string" and length > 0' \
	"$(1)/artifacts/manifest.json" >/dev/null
jq -e '.runtime.bpftool_source_commit == .kernel.commit' \
	"$(1)/artifacts/manifest.json" >/dev/null
endef

define BUILD_ACTION_RQ2_START
$(call BUILD_ACTION_RQ2_VALIDATE_HOST_PIN)
$(call NAMEI_EXT_RESULT_ROOT_CREATE,$(1))
$(call NAMEI_EXT_MULTI_BOOT_INIT,$(1))
$(call NAMEI_EXT_RUN_START,$(1),build-action-rq2,bazel+sandboxfs,kvm_build_action_rq2,$(1)/observations.jsonl,build_action_sandboxing.bpf.c,namei_ext_build_action_rq2+sandboxfs-0.2.0)
$(call BUILD_ACTION_RQ2_CAPTURE_ARTIFACTS,$(1))
jq --slurpfile artifacts "$(1)/artifacts/manifest.json" \
	--argjson repetitions "$(2)" \
		--argjson samples "$(3)" \
		--arg scales "$(4)" \
		--argjson capacity_probe "$(5)" \
		--argjson primary_scale "$(BUILD_ACTION_RQ2_PRIMARY_SCALE)" \
		--argjson kvm_cpus "$(KVM_CPUS)" \
		--arg host_cpu_pin "$(BUILD_ACTION_RQ2_HOST_CPUS)" \
		'.protocol_schema = "namei_ext.build_action_rq2.protocol.v1" | .layout = "paired-boot-matrix" | .artifacts = $$artifacts[0] | .matrix = {conditions:["namei_ext","sandboxfs"],repetitions:$$repetitions,samples_per_scale:$$samples,scales:($$scales | split(",") | map(tonumber)),primary_scale:$$primary_scale,capacity_probe:$$capacity_probe,order:"alternating",scale_order:"rotating",kvm_cpus:$$kvm_cpus,host_cpu_pin:$$host_cpu_pin}' \
	"$(1)/run.json" >"$(1)/run.json.tmp"
mv -f "$(1)/run.json.tmp" "$(1)/run.json"
printf '%s\n' "$(6)" >"$(1)/command.txt"
$(call BUILD_ACTION_RQ2_CAPTURE_HOST,$(1),$(BUILD_ACTION_RQ2_HOST_CPUS))
: >"$(1)/launch-order.jsonl"
endef

define BUILD_ACTION_RQ2_WRITE_GUEST_MAKEFILE
printf '%s := %s\n' \
	'CONDITION' "$$condition" \
	'REPETITION' "$$repetition" \
	'BUILD_ACTION_RQ2_BOOT_DIR' "$${boot_dir#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_RUNNER' "$${runner#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_POLICY' "$${policy#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_SANDBOXFS' "$${sandboxfs#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_LIBFUSE' "$${libfuse#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_BAZEL' "$${bazel#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_BPFTOOL' "$${bpftool#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_KERNEL_CONFIG' "$${config#$(ROOT_DIR)/}" \
	'BUILD_ACTION_RQ2_KERNEL_COMMIT' "$$commit" \
	'BUILD_ACTION_RQ2_KERNEL_BUILD_ID' "$$build_id" \
	'BUILD_ACTION_RQ2_KERNEL_RELEASE' "$$release" \
	'BUILD_ACTION_RQ2_SAMPLES_ACTIVE' "$(BUILD_ACTION_RQ2_ACTIVE_SAMPLES)" \
	'BUILD_ACTION_RQ2_SCALES_ACTIVE' "$(BUILD_ACTION_RQ2_ACTIVE_SCALES)" \
	'BUILD_ACTION_RQ2_CAPACITY_ACTIVE' "$(BUILD_ACTION_RQ2_ACTIVE_CAPACITY)" \
	>"$$guest_makefile"; \
	! grep -F "$(ROOT_DIR)/" "$$guest_makefile" >/dev/null
endef

.PHONY: build-action-rq2-analysis-test \
	kvm-build-action-rq2-preflight kvm-build-action-rq2 \
	experiment-build-action-rq2 build-action-rq2-run-matrix \
	build-action-rq2-finalize build-action-rq2-mark-complete \
		build-action-rq2-report __build_action_rq2_guest

kvm-build-action-rq2-preflight: NAMEI_EXT_REQUIRE_CLEAN = 1
kvm-build-action-rq2-preflight: experiment-source-clean

kvm-build-action-rq2: NAMEI_EXT_REQUIRE_CLEAN = 1
kvm-build-action-rq2: experiment-source-clean

build-action-rq2-analysis-test:
	python3 -m unittest discover -s "$(ROOT_DIR)/analysis/build_action_rq2" \
		-p 'test_*.py' -v

kvm-build-action-rq2-preflight: kernel-provenance kernel-bpftool \
		$(KERNEL_IMAGE) bpf \
		build-action-sandboxing workload-bazel workload-sandboxfs-build
	$(call BUILD_ACTION_RQ2_START,$(BUILD_ACTION_RQ2_PREFLIGHT_RESULT_DIR),$(BUILD_ACTION_RQ2_PREFLIGHT_REPETITIONS),$(BUILD_ACTION_RQ2_PREFLIGHT_SAMPLES),$(subst $(NAMEI_EXT_SPACE),$(NAMEI_EXT_COMMA),$(strip $(BUILD_ACTION_RQ2_PREFLIGHT_SCALES))),$(BUILD_ACTION_RQ2_CAPACITY_PROBE),make kvm-build-action-rq2-preflight RUN_ID=$(RUN_ID))
	$(MAKE) --no-print-directory build-action-rq2-run-matrix \
		RUN_ID="$(RUN_ID)" \
		BUILD_ACTION_RQ2_ACTIVE_DIR="$(BUILD_ACTION_RQ2_PREFLIGHT_RESULT_DIR)" \
		BUILD_ACTION_RQ2_ACTIVE_REPETITIONS="$(BUILD_ACTION_RQ2_PREFLIGHT_REPETITIONS)" \
		BUILD_ACTION_RQ2_ACTIVE_SAMPLES="$(BUILD_ACTION_RQ2_PREFLIGHT_SAMPLES)" \
		BUILD_ACTION_RQ2_ACTIVE_SCALES="$(subst $(NAMEI_EXT_SPACE),$(NAMEI_EXT_COMMA),$(strip $(BUILD_ACTION_RQ2_PREFLIGHT_SCALES)))" \
		BUILD_ACTION_RQ2_ACTIVE_CAPACITY="$(BUILD_ACTION_RQ2_CAPACITY_PROBE)"
	$(MAKE) --no-print-directory build-action-rq2-finalize \
		RUN_ID="$(RUN_ID)" \
		BUILD_ACTION_RQ2_ACTIVE_DIR="$(BUILD_ACTION_RQ2_PREFLIGHT_RESULT_DIR)" \
		BUILD_ACTION_RQ2_ACTIVE_REPETITIONS="$(BUILD_ACTION_RQ2_PREFLIGHT_REPETITIONS)" \
		BUILD_ACTION_RQ2_ACTIVE_SAMPLES="$(BUILD_ACTION_RQ2_PREFLIGHT_SAMPLES)" \
		BUILD_ACTION_RQ2_ACTIVE_SCALES="$(subst $(NAMEI_EXT_SPACE),$(NAMEI_EXT_COMMA),$(strip $(BUILD_ACTION_RQ2_PREFLIGHT_SCALES)))" \
		BUILD_ACTION_RQ2_ACTIVE_CAPACITY="$(BUILD_ACTION_RQ2_CAPACITY_PROBE)"
	$(MAKE) --no-print-directory build-action-rq2-mark-complete \
		RUN_ID="$(RUN_ID)" \
		BUILD_ACTION_RQ2_ACTIVE_DIR="$(BUILD_ACTION_RQ2_PREFLIGHT_RESULT_DIR)"

kvm-build-action-rq2: kernel-provenance kernel-bpftool $(KERNEL_IMAGE) bpf \
		build-action-sandboxing workload-bazel workload-sandboxfs-build
	$(call BUILD_ACTION_RQ2_START,$(BUILD_ACTION_RQ2_RESULT_DIR),$(BUILD_ACTION_RQ2_REPETITIONS),$(BUILD_ACTION_RQ2_SAMPLES),$(subst $(NAMEI_EXT_SPACE),$(NAMEI_EXT_COMMA),$(strip $(BUILD_ACTION_RQ2_SCALES))),0,make kvm-build-action-rq2 RUN_ID=$(RUN_ID))
	$(MAKE) --no-print-directory build-action-rq2-run-matrix \
		RUN_ID="$(RUN_ID)" \
		BUILD_ACTION_RQ2_ACTIVE_DIR="$(BUILD_ACTION_RQ2_RESULT_DIR)" \
		BUILD_ACTION_RQ2_ACTIVE_REPETITIONS="$(BUILD_ACTION_RQ2_REPETITIONS)" \
		BUILD_ACTION_RQ2_ACTIVE_SAMPLES="$(BUILD_ACTION_RQ2_SAMPLES)" \
		BUILD_ACTION_RQ2_ACTIVE_SCALES="$(subst $(NAMEI_EXT_SPACE),$(NAMEI_EXT_COMMA),$(strip $(BUILD_ACTION_RQ2_SCALES)))" \
		BUILD_ACTION_RQ2_ACTIVE_CAPACITY=0
	$(MAKE) --no-print-directory build-action-rq2-finalize \
		RUN_ID="$(RUN_ID)" \
		BUILD_ACTION_RQ2_ACTIVE_DIR="$(BUILD_ACTION_RQ2_RESULT_DIR)" \
		BUILD_ACTION_RQ2_ACTIVE_REPETITIONS="$(BUILD_ACTION_RQ2_REPETITIONS)" \
		BUILD_ACTION_RQ2_ACTIVE_SAMPLES="$(BUILD_ACTION_RQ2_SAMPLES)" \
		BUILD_ACTION_RQ2_ACTIVE_SCALES="$(subst $(NAMEI_EXT_SPACE),$(NAMEI_EXT_COMMA),$(strip $(BUILD_ACTION_RQ2_SCALES)))" \
		BUILD_ACTION_RQ2_ACTIVE_CAPACITY=0
	$(MAKE) --no-print-directory build-action-rq2-mark-complete \
		RUN_ID="$(RUN_ID)" \
		BUILD_ACTION_RQ2_ACTIVE_DIR="$(BUILD_ACTION_RQ2_RESULT_DIR)"

build-action-rq2-run-matrix:
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_DIR)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_SAMPLES)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_SCALES)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_CAPACITY)"
	jq -e '.status == "running" and .layout == "paired-boot-matrix"' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/run.json" >/dev/null
	: >"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/expected-boots.txt"
	: >"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/launch-order.jsonl"
	manifest="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/artifacts/manifest.json"; \
	image="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.kernel.image' "$$manifest")"; \
	config="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.kernel.config' "$$manifest")"; \
	commit=$$(jq -r '.kernel.commit' "$$manifest"); \
	build_id=$$(jq -r '.kernel.build_id' "$$manifest"); \
	release=$$(jq -r '.kernel.release' "$$manifest"); \
	runner="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.runtime.runner' "$$manifest")"; \
	policy="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.runtime.policy' "$$manifest")"; \
	sandboxfs="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.runtime.sandboxfs' "$$manifest")"; \
	libfuse="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.runtime.libfuse' "$$manifest")"; \
	bazel="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.runtime.bazel' "$$manifest")"; \
	bpftool="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/$$(jq -r '.runtime.bpftool' "$$manifest")"; \
	order_index=0; \
	for repetition in $$(seq 1 "$(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)"); do \
		if (( repetition % 2 )); then \
			conditions=(namei_ext sandboxfs); \
		else \
			conditions=(sandboxfs namei_ext); \
		fi; \
		for condition in "$${conditions[@]}"; do \
			order_index=$$((order_index + 1)); \
			printf '%s|%s\n' "$$repetition" "$$condition" \
				>>"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/expected-boots.txt"; \
			boot_dir="$(BUILD_ACTION_RQ2_ACTIVE_DIR)/boots/block-$$(printf '%02d' "$$repetition")-$$condition"; \
			install -d "$$boot_dir"; \
			guest_makefile="$$boot_dir/guest.mk"; \
			$(call BUILD_ACTION_RQ2_WRITE_GUEST_MAKEFILE); \
			guest_makefile_rel="$${guest_makefile#$(ROOT_DIR)/}"; \
			host_started_at=$$(date -u +%Y-%m-%dT%H:%M:%S.%NZ); \
			$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
				__namei_ext_kvm_capture \
				RUN_ID="$(RUN_ID)" \
				NAMEI_EXT_KVM_CAPTURE_IMAGE="$$image" \
				NAMEI_EXT_KVM_CAPTURE_GUEST_TARGET="-f Makefile -f $$guest_makefile_rel __build_action_rq2_guest" \
				NAMEI_EXT_KVM_CAPTURE_BOOT_DIR="$$boot_dir" \
				NAMEI_EXT_KVM_CAPTURE_RUN_DIR="$(BUILD_ACTION_RQ2_ACTIVE_DIR)" \
				NAMEI_EXT_KVM_CAPTURE_HOST_CPUS="$(BUILD_ACTION_RQ2_HOST_CPUS)" \
				NAMEI_EXT_KVM_CAPTURE_TIMEOUT="$(BUILD_ACTION_RQ2_KVM_TIMEOUT_SECONDS)"; \
			host_completed_at=$$(date -u +%Y-%m-%dT%H:%M:%S.%NZ); \
			jq -c -n --argjson order_index "$$order_index" \
				--argjson repetition "$$repetition" \
				--arg condition "$$condition" \
				--arg started_at "$$host_started_at" \
				--arg completed_at "$$host_completed_at" \
				'{schema:"namei_ext.build_action_rq2.launch_order.v1",order_index:$$order_index,repetition:$$repetition,condition:$$condition,host_started_at:$$started_at,host_completed_at:$$completed_at}' \
				>>"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/launch-order.jsonl"; \
			jq --argjson order_index "$$order_index" \
				--arg started_at "$$host_started_at" \
				--arg completed_at "$$host_completed_at" \
				'.host_launch = {order_index:$$order_index,started_at:$$started_at,completed_at:$$completed_at}' \
				"$$boot_dir/boot.json" >"$$boot_dir/boot.json.tmp"; \
			mv -f "$$boot_dir/boot.json.tmp" "$$boot_dir/boot.json"; \
		done; \
	done

build-action-rq2-finalize:
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_DIR)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_SAMPLES)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_SCALES)"
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_CAPACITY)"
	jq -e '.status == "running" and (.failed_at | not)' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/run.json" >/dev/null
	jq -e \
		--argjson repetitions "$(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)" \
		--argjson samples "$(BUILD_ACTION_RQ2_ACTIVE_SAMPLES)" \
		--arg scales "$(BUILD_ACTION_RQ2_ACTIVE_SCALES)" \
		--argjson capacity "$(BUILD_ACTION_RQ2_ACTIVE_CAPACITY)" \
		--argjson primary_scale "$(BUILD_ACTION_RQ2_PRIMARY_SCALE)" \
		--argjson kvm_cpus "$(KVM_CPUS)" \
		--arg host_cpu_pin "$(BUILD_ACTION_RQ2_HOST_CPUS)" \
		'.protocol_schema == "namei_ext.build_action_rq2.protocol.v1" and .layout == "paired-boot-matrix" and .matrix.conditions == ["namei_ext","sandboxfs"] and .matrix.repetitions == $$repetitions and .matrix.samples_per_scale == $$samples and .matrix.scales == ($$scales | split(",") | map(tonumber)) and .matrix.primary_scale == $$primary_scale and .matrix.capacity_probe == $$capacity and .matrix.order == "alternating" and .matrix.scale_order == "rotating" and .matrix.kvm_cpus == $$kvm_cpus and .matrix.host_cpu_pin == $$host_cpu_pin' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/run.json" >/dev/null
	$(call NAMEI_EXT_MULTI_BOOT_CAPTURE_PINNED_HOST_AFTER,$(BUILD_ACTION_RQ2_ACTIVE_DIR))
	LC_ALL=C sort -o "$(BUILD_ACTION_RQ2_ACTIVE_DIR)/expected-boots.txt" \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/expected-boots.txt"
	$(call NAMEI_EXT_MULTI_BOOT_COLLECT_OBSERVATIONS,$(BUILD_ACTION_RQ2_ACTIVE_DIR),$$((2 * $(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS))))
	find "$(BUILD_ACTION_RQ2_ACTIVE_DIR)/boots" -mindepth 2 -maxdepth 2 \
		-name boot.json -type f -print0 | sort -z | \
		xargs -0 jq -r '"\(.repetition)|\(.condition)"' | \
		LC_ALL=C sort >"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observed-boots.txt"
	cmp "$(BUILD_ACTION_RQ2_ACTIVE_DIR)/expected-boots.txt" \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observed-boots.txt"
	jq -e -s --argjson repetitions "$(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)" \
		'. as $$rows | length == (2 * $$repetitions) and all(range(0; length); . as $$i | (($$i / 2 | floor) + 1) as $$repetition | (if ($$repetition % 2) == 1 then ["namei_ext","sandboxfs"] else ["sandboxfs","namei_ext"] end) as $$expected | $$rows[$$i].schema == "namei_ext.build_action_rq2.launch_order.v1" and $$rows[$$i].order_index == ($$i + 1) and $$rows[$$i].repetition == $$repetition and $$rows[$$i].condition == $$expected[$$i % 2] and ($$rows[$$i].host_started_at | type == "string" and length > 0) and ($$rows[$$i].host_completed_at | type == "string" and length > 0))' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/launch-order.jsonl" >/dev/null
	$(call NAMEI_EXT_MULTI_BOOT_VALIDATE_BOOT_FILES,$(BUILD_ACTION_RQ2_ACTIVE_DIR),$$((2 * $(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS))),$(BUILD_ACTION_RQ2_BOOT_FILES))
	while IFS= read -r -d '' boot; do \
		test "$$(cat "$$boot/runner.status")" = 0; \
		jq -e '.schema == "namei_ext.build_action_rq2.boot.v1" and .status == "completed" and (.condition == "namei_ext" or .condition == "sandboxfs") and (.repetition | type == "number") and (.completed_at | type == "string" and length > 0)' \
			"$$boot/boot.json" >/dev/null; \
		jq -e '.schema == "namei_ext.vcpu_affinity.v1" and .status == "verified"' \
			"$$boot/vcpu-affinity.json" >/dev/null; \
		! grep -E 'WARNING: Failed to pin vCPUs|Permission denied: cannot set affinity|not enough host CPUs|QMP .*failed|No vCPU threads found|TID .* does not exist' \
			"$$boot/launcher.stderr.log" >/dev/null; \
	done < <(find "$(BUILD_ACTION_RQ2_ACTIVE_DIR)/boots" \
		-mindepth 1 -maxdepth 1 -type d -print0 | LC_ALL=C sort -z)
	! jq -e 'select(.pass != true)' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl" >/dev/null
	scale_count=$$(printf '%s\n' "$(BUILD_ACTION_RQ2_ACTIVE_SCALES)" | \
		awk -F, '{print NF}'); \
	expected=$$((2 * $(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS) * \
		$(BUILD_ACTION_RQ2_ACTIVE_SAMPLES) * scale_count)); \
	test "$$(jq -s '[.[] | select(.event == "build-action-rq2-sample")] | length' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl")" = "$$expected"; \
	test "$$(jq -s '[.[] | select(.event == "build-action-rq2-sample") | "\(.repetition)|\(.condition)|\(.scale)|\(.sample)"] | unique | length' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl")" = "$$expected"
	test "$$(jq -s '[.[] | select(.event == "build-action-rq2-summary")] | length' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$$((2 * $(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)))"
	test "$$(jq -s '[.[] | select(.event == "build-action-rq2-policy-counter")] | length' \
		"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$$((9 * $(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)))"
	if test "$(BUILD_ACTION_RQ2_ACTIVE_CAPACITY)" -gt 0; then \
		test "$$(jq -s '[.[] | select(.event == "build-action-rq2-capacity")] | length' \
			"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
			"$(BUILD_ACTION_RQ2_ACTIVE_REPETITIONS)"; \
		jq -s -e --argjson expected "$(BUILD_ACTION_RQ2_ACTIVE_CAPACITY)" \
			'[.[] | select(.event == "build-action-rq2-capacity")] | all(.[]; .condition == "namei_ext" and .requested == $$expected and .inserted == $$expected and .removed == $$expected and .remaining == 0)' \
			"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl" >/dev/null; \
	else \
		test "$$(jq -s '[.[] | select(.event == "build-action-rq2-capacity")] | length' \
			"$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl")" = 0; \
	fi
	$(call BUILD_ACTION_RQ2_VALIDATE_HOST_FILES,$(BUILD_ACTION_RQ2_ACTIVE_DIR))
	$(call BUILD_ACTION_RQ2_VALIDATE_RUN_BASE,$(BUILD_ACTION_RQ2_ACTIVE_DIR),$(BUILD_ACTION_RQ2_ACTIVE_DIR)/observations.jsonl)

build-action-rq2-mark-complete:
	test -n "$(BUILD_ACTION_RQ2_ACTIVE_DIR)"
	$(call NAMEI_EXT_RUN_COMPLETE,$(BUILD_ACTION_RQ2_ACTIVE_DIR))
	$(call NAMEI_EXT_RUN_VALIDATE_COMPLETE,$(BUILD_ACTION_RQ2_ACTIVE_DIR))

build-action-rq2-report:
	$(call NAMEI_EXT_RUN_VALIDATE_COMPLETE,$(BUILD_ACTION_RQ2_RESULT_DIR))
	$(call NAMEI_EXT_ANALYSIS_PREPARE,$(BUILD_ACTION_RQ2_RESULT_DIR)/analysis)
	python3 "$(BUILD_ACTION_RQ2_ANALYSIS)" \
		--run "$(BUILD_ACTION_RQ2_RESULT_DIR)/run.json" \
		--observations "$(BUILD_ACTION_RQ2_RESULT_DIR)/observations.jsonl" \
		--launch-order "$(BUILD_ACTION_RQ2_RESULT_DIR)/launch-order.jsonl" \
		--seed "$(BUILD_ACTION_RQ2_ANALYSIS_SEED)" \
		--output "$(BUILD_ACTION_RQ2_RESULT_DIR)/analysis.tmp"
	for file in summary.json summary.csv report.md action-time.pdf \
			action-time.png; do \
		test -s "$(BUILD_ACTION_RQ2_RESULT_DIR)/analysis.tmp/$$file"; \
	done
	jq -e '.schema == "namei_ext.build_action_rq2.summary.v1" and (.verdict.tested_hypothesis == "supported" or .verdict.tested_hypothesis == "contradicted" or .verdict.tested_hypothesis == "inconclusive")' \
		"$(BUILD_ACTION_RQ2_RESULT_DIR)/analysis.tmp/summary.json" >/dev/null
	$(call NAMEI_EXT_ANALYSIS_PUBLISH,$(BUILD_ACTION_RQ2_RESULT_DIR)/analysis)

experiment-build-action-rq2: kvm-build-action-rq2 \
	build-action-rq2-report

__build_action_rq2_guest: __namei_ext_guest_prepare
	test "$(notdir $(lastword $(MAKEFILE_LIST)))" = guest.mk
	case "$(CONDITION)" in namei_ext|sandboxfs) ;; *) exit 1;; esac
	test -n "$(REPETITION)"
	test -x "$(BUILD_ACTION_RQ2_RUNNER)"
	test -r "$(BUILD_ACTION_RQ2_POLICY)"
	test -x "$(BUILD_ACTION_RQ2_SANDBOXFS)"
	test -r "$(BUILD_ACTION_RQ2_LIBFUSE)"
	test -x "$(BUILD_ACTION_RQ2_BAZEL)"
	test -x "$(BUILD_ACTION_RQ2_BPFTOOL)"
	affinity_status=waiting; \
	for attempt in $$(seq 1 500); do \
		if test -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/vcpu-affinity.json"; then \
			if jq -e '.schema == "namei_ext.vcpu_affinity.v1" and .status == "verified"' \
					"$(BUILD_ACTION_RQ2_BOOT_DIR)/vcpu-affinity.json" >/dev/null 2>&1; then \
				affinity_status=verified; \
				break; \
			fi; \
			if jq -e '.schema == "namei_ext.vcpu_affinity.v1" and .status == "failed"' \
					"$(BUILD_ACTION_RQ2_BOOT_DIR)/vcpu-affinity.json" >/dev/null 2>&1; then \
				cat "$(BUILD_ACTION_RQ2_BOOT_DIR)/vcpu-affinity.json" >&2; \
				exit 1; \
			fi; \
		fi; \
		sleep 0.05; \
	done; \
	test "$$affinity_status" = verified; \
	jq -r '.verified_at' \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/vcpu-affinity.json" \
		>"$(BUILD_ACTION_RQ2_BOOT_DIR)/affinity-barrier.txt"
	: >"$(BUILD_ACTION_RQ2_BOOT_DIR)/observations.jsonl"
	: >"$(BUILD_ACTION_RQ2_BOOT_DIR)/stdout.log"
	: >"$(BUILD_ACTION_RQ2_BOOT_DIR)/stderr.log"
	: >"$(BUILD_ACTION_RQ2_BOOT_DIR)/sandboxfs.stderr.log"
	rm -f "$(BUILD_ACTION_RQ2_BOOT_DIR)/runner.ready" \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/runner.release"
	$(call NAMEI_EXT_GUEST_CAPTURE_KERNEL_EVIDENCE,\
		$(BUILD_ACTION_RQ2_BOOT_DIR),\
		$(BUILD_ACTION_RQ2_KERNEL_CONFIG),\
		$(BUILD_ACTION_RQ2_KERNEL_COMMIT),\
		$(BUILD_ACTION_RQ2_KERNEL_RELEASE))
	printf '%s\n' "$(BUILD_ACTION_RQ2_KERNEL_BUILD_ID)" \
		>"$(BUILD_ACTION_RQ2_BOOT_DIR)/kernel-build-id.txt"
	grep -q ' [Tt] namei_ext_lookup$$' /proc/kallsyms
	clocksource=$$(cat /sys/devices/system/clocksource/clocksource0/current_clocksource); \
	test "$$clocksource" = tsc; \
	printf '%s\n' "$$clocksource" \
		>"$(BUILD_ACTION_RQ2_BOOT_DIR)/clocksource-before.txt"
	cat /proc/stat >"$(BUILD_ACTION_RQ2_BOOT_DIR)/proc-stat-before.txt"
	$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,\
		$(BUILD_ACTION_RQ2_BOOT_DIR),$(BUILD_ACTION_RQ2_BPFTOOL),before)
	jq -e 'type == "array" and length == 0' \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-programs-before.json" >/dev/null
	jq -e 'type == "array" and all(.[]; has("error") | not) and ([.. | objects | select(has("id"))] | length) == 0' \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-cgroup-before.json" >/dev/null
	test ! -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-mounts-before.txt"
	test "$$(cat "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-before.status")" = 1
	test ! -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-before.txt"
	runner_pid=; \
	cleanup_runner() { \
		if test -n "$$runner_pid" && kill -0 "$$runner_pid" 2>/dev/null; then \
			kill "$$runner_pid" 2>/dev/null || true; \
			wait "$$runner_pid" 2>/dev/null || true; \
		fi; \
	}; \
	trap cleanup_runner EXIT; \
	policy=-; sandboxfs=-; capacity=0; \
	if test "$(CONDITION)" = namei_ext; then \
		policy="$(BUILD_ACTION_RQ2_POLICY)"; \
		capacity="$(BUILD_ACTION_RQ2_CAPACITY_ACTIVE)"; \
	else \
		sandboxfs="$(BUILD_ACTION_RQ2_SANDBOXFS)"; \
	fi; \
	LD_LIBRARY_PATH="$$(dirname "$(BUILD_ACTION_RQ2_LIBFUSE)")" \
		"$(BUILD_ACTION_RQ2_RUNNER)" "$(CONDITION)" "$$policy" "$$sandboxfs" \
		"$(BUILD_ACTION_RQ2_BAZEL)" \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/observations.jsonl" \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)" /sys/fs/cgroup \
		"$(REPETITION)" "$(BUILD_ACTION_RQ2_SAMPLES_ACTIVE)" \
		"$(BUILD_ACTION_RQ2_SCALES_ACTIVE)" "$$capacity" \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/runner.ready" \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/runner.release" \
		>>"$(BUILD_ACTION_RQ2_BOOT_DIR)/stdout.log" \
		2>>"$(BUILD_ACTION_RQ2_BOOT_DIR)/stderr.log" & \
	runner_pid=$$!; \
	ready=0; \
	for attempt in $$(seq 1 12000); do \
		if test -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/runner.ready"; then \
			ready=1; break; \
		fi; \
		kill -0 "$$runner_pid"; \
		sleep 0.01; \
	done; \
	test "$$ready" = 1; \
	$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,\
		$(BUILD_ACTION_RQ2_BOOT_DIR),$(BUILD_ACTION_RQ2_BPFTOOL),middle); \
	if test "$(CONDITION)" = namei_ext; then \
		jq -e 'type == "array" and length == 1' \
			"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-programs-middle.json" >/dev/null; \
		jq -e 'type == "array" and all(.[]; has("error") | not) and ([.. | objects | select(has("id"))] | length) >= 1' \
			"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-cgroup-middle.json" >/dev/null; \
		test ! -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-mounts-middle.txt"; \
		test "$$(cat "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-middle.status")" = 1; \
		test ! -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-middle.txt"; \
	else \
		jq -e 'type == "array" and length == 0' \
			"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-programs-middle.json" >/dev/null; \
		jq -e 'type == "array" and all(.[]; has("error") | not) and ([.. | objects | select(has("id"))] | length) == 0' \
			"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-cgroup-middle.json" >/dev/null; \
		test "$$(wc -l <"$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-mounts-middle.txt")" = 1; \
		test "$$(cat "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-middle.status")" = 0; \
		test "$$(grep -c '^p[0-9][0-9]*$$' "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-middle.txt")" = 1; \
	fi; \
	printf '%s\n' release >"$(BUILD_ACTION_RQ2_BOOT_DIR)/runner.release"; \
	runner_status=0; \
	wait "$$runner_pid" || runner_status=$$?; \
	runner_pid=; \
	printf '%s\n' "$$runner_status" >"$(BUILD_ACTION_RQ2_BOOT_DIR)/runner.status"; \
	test "$$runner_status" = 0; \
	trap - EXIT
	$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,\
		$(BUILD_ACTION_RQ2_BOOT_DIR),$(BUILD_ACTION_RQ2_BPFTOOL),after)
	jq -e 'type == "array" and length == 0' \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-programs-after.json" >/dev/null
	jq -e 'type == "array" and all(.[]; has("error") | not) and ([.. | objects | select(has("id"))] | length) == 0' \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/bpf-cgroup-after.json" >/dev/null
	test ! -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-mounts-after.txt"
	test "$$(cat "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-after.status")" = 1
	test ! -s "$(BUILD_ACTION_RQ2_BOOT_DIR)/fuse-open-fds-after.txt"
	! jq -e 'select(.pass != true)' \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null
	scale_count=$$(printf '%s\n' "$(BUILD_ACTION_RQ2_SCALES_ACTIVE)" | \
		awk -F, '{print NF}'); \
	expected=$$((scale_count * $(BUILD_ACTION_RQ2_SAMPLES_ACTIVE))); \
	test "$$(jq -s '[.[] | select(.event == "build-action-rq2-sample")] | length' \
		"$(BUILD_ACTION_RQ2_BOOT_DIR)/observations.jsonl")" = "$$expected"; \
	test "$$(find "$(BUILD_ACTION_RQ2_BOOT_DIR)" -maxdepth 1 \
		-name 'scale-*-output-*.txt' -type f | wc -l)" = \
		"$$((2 * expected))"; \
	test "$$(find "$(BUILD_ACTION_RQ2_BOOT_DIR)" -maxdepth 1 \
		-name 'scale-*-lower-*-*.txt' -type f | wc -l)" = \
		"$$((4 * expected))"
	cat /proc/stat >"$(BUILD_ACTION_RQ2_BOOT_DIR)/proc-stat-after.txt"
	clocksource=$$(cat /sys/devices/system/clocksource/clocksource0/current_clocksource); \
	test "$$clocksource" = \
		"$$(cat "$(BUILD_ACTION_RQ2_BOOT_DIR)/clocksource-before.txt")"; \
	printf '%s\n' "$$clocksource" \
		>"$(BUILD_ACTION_RQ2_BOOT_DIR)/clocksource-after.txt"
	dmesg >"$(BUILD_ACTION_RQ2_BOOT_DIR)/dmesg.log"
	$(call NAMEI_EXT_GUEST_ASSERT_DMESG_CLEAN,$(BUILD_ACTION_RQ2_BOOT_DIR)/dmesg.log)
	jq -n \
		--argjson repetition "$(REPETITION)" \
		--arg condition "$(CONDITION)" \
		--arg kernel_commit "$(BUILD_ACTION_RQ2_KERNEL_COMMIT)" \
		--arg kernel_build_id "$(BUILD_ACTION_RQ2_KERNEL_BUILD_ID)" \
		--arg kernel_release "$(BUILD_ACTION_RQ2_KERNEL_RELEASE)" \
		--arg scales "$(BUILD_ACTION_RQ2_SCALES_ACTIVE)" \
		--argjson samples "$(BUILD_ACTION_RQ2_SAMPLES_ACTIVE)" \
		--arg completed_at "$$(date -u +%Y-%m-%dT%H:%M:%S.%NZ)" \
		'{schema:"namei_ext.build_action_rq2.boot.v1",status:"completed",repetition:$$repetition,condition:$$condition,kernel_commit:$$kernel_commit,kernel_build_id:$$kernel_build_id,kernel_release:$$kernel_release,scales:($$scales | split(",") | map(tonumber)),samples_per_scale:$$samples,clocksource:"tsc",completed_at:$$completed_at}' \
		>"$(BUILD_ACTION_RQ2_BOOT_DIR)/boot.json"

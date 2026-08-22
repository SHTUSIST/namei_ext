SPINDLE_STAGING_RQ2_BOOT_FILES := \
	guest.mk launcher.stdout.log launcher.stderr.log \
	vcpu-affinity-pin.json vcpu-affinity.json affinity-barrier.txt \
	boot.json raw-runner.jsonl observations.jsonl stdout.log stderr.log \
	kernel.config kernel-commit.txt kernel-release.txt uname.txt \
	proc-version.txt kernel-cmdline.txt dmesg.log \
	runtime-symlinks.txt runtime-lower-fstype.txt runtime-mount-before.status \
	runtime-mount-after.status guest-prepare.status guest-inner.status \
	guest-cleanup.status guest-inventory-after.status guest-dmesg.status \
	bpf-programs-before.json bpf-programs-after.json \
	bpf-cgroup-before.json bpf-cgroup-after.json \
	fuse-mounts-before.txt fuse-mounts-after.txt \
	fuse-open-fds-before.txt fuse-open-fds-before.status \
	fuse-open-fds-after.txt fuse-open-fds-after.status

SPINDLE_STAGING_RQ2_GUEST_RUNTIME_ABS = \
	$(ROOT_DIR)/$(SPINDLE_STAGING_RQ2_GUEST_RUNTIME_ROOT)
SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS = \
	$(ROOT_DIR)/$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ROOT)
SPINDLE_STAGING_RQ2_GUEST_SPINDLE_ABS = \
	$(ROOT_DIR)/$(SPINDLE_STAGING_RQ2_GUEST_SPINDLE)
SPINDLE_STAGING_RQ2_GUEST_TEST_DIR_ABS = \
	$(ROOT_DIR)/$(SPINDLE_STAGING_RQ2_GUEST_TEST_DIR)

define SPINDLE_STAGING_RQ2_CAPTURE_HOST_BEFORE
lscpu >"$(1)/host-lscpu.txt"
lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE,MAXMHZ,MINMHZ \
	>"$(1)/host-lscpu-extended.txt"
printf '%s\n' "$(SPINDLE_STAGING_RQ2_HOST_CPUS)" \
	>"$(1)/host-cpu-pin.txt"
cat /proc/stat >"$(1)/host-proc-stat-before.txt"
cat /proc/interrupts >"$(1)/host-proc-interrupts-before.txt"
"$(VNG)" --version >"$(1)/vng-version.txt"
endef

define SPINDLE_STAGING_RQ2_CAPTURE_ARTIFACTS
$(call SPINDLE_STAGING_CAPTURE_ARTIFACTS,$(1))
install -m 0555 "$(SPINDLE_STAGING_RQ2_RUNNER)" \
	"$(1)/artifacts/runtime/namei_ext_spindle_staging_rq2"
install -m 0444 "$(MDTEST_LIBFUSE_BUILD_LOG)" \
	"$(1)/artifacts/source/libfuse-build.log"
install -m 0444 "$(SPINDLE_STAGING_RQ2_PLAN)" \
	"$(1)/artifacts/source/experiment-plan.md"
install -m 0444 "$(SPINDLE_STAGING_RQ2_PLAN_REVIEW)" \
	"$(1)/artifacts/source/experiment-plan-review.md"
readelf -d "$(1)/artifacts/runtime/namei_ext_spindle_staging_rq2" \
	>"$(1)/artifacts/source/rq2-runner-dynamic.txt"
! grep -F 'libfuse.so' "$(1)/artifacts/source/rq2-runner-dynamic.txt"
git -C "$(MDTEST_LIBFUSE_SOURCE)" show -s --format='%H%n%D%n%s' HEAD \
	>"$(1)/artifacts/source/libfuse-version.txt"
grep -Fx "$(MDTEST_LIBFUSE_COMMIT)" \
	"$(1)/artifacts/source/libfuse-version.txt"
jq \
	--arg runner 'artifacts/runtime/namei_ext_spindle_staging_rq2' \
	--arg libfuse_commit "$(MDTEST_LIBFUSE_COMMIT)" \
	--arg libfuse_tag "$(MDTEST_LIBFUSE_TAG)" \
	'.runtime.rq2_runner = {path:$$runner,static_libfuse:true} | .source.libfuse = {commit:$$libfuse_commit,tag:$$libfuse_tag,build:"Meson release static libfuse; low-level multithreaded API; kernel passthrough required"}' \
	"$(1)/artifacts/manifest.json" >"$(1)/artifacts/manifest.json.tmp"
mv -f "$(1)/artifacts/manifest.json.tmp" "$(1)/artifacts/manifest.json"
endef

define SPINDLE_STAGING_RQ2_START
$(call NAMEI_EXT_VALIDATE_HOST_CPU_PIN,$(SPINDLE_STAGING_RQ2_HOST_CPUS),$(KVM_CPUS))
$(call NAMEI_EXT_RESULT_ROOT_CREATE,$(1))
$(call NAMEI_EXT_MULTI_BOOT_INIT,$(1))
$(call NAMEI_EXT_RUN_START,$(1),spindle-staging-rq2,LLNL-Spindle,kvm_spindle_staging_rq2,$(1)/observations.jsonl,spindle_staging.bpf.c,namei_ext_spindle_staging_rq2+libfuse-3.18.2)
$(call SPINDLE_STAGING_RQ2_CAPTURE_ARTIFACTS,$(1))
jq \
	--argjson repetitions "$(2)" \
	--argjson warmups "$(3)" \
	--argjson samples "$(4)" \
	--arg host_cpu_pin "$(SPINDLE_STAGING_RQ2_HOST_CPUS)" \
	--argjson kvm_cpus "$(KVM_CPUS)" \
	'.protocol_schema = "namei_ext.spindle_staging_rq2.protocol.v1" | .layout = "paired-fresh-boot" | .matrix = {conditions:["namei_ext","fuse"],repetitions:$$repetitions,warmups_per_boot:$$warmups,measured_samples_per_boot:$$samples,order:"alternating",focal_objects:47,host_cpu_pin:$$host_cpu_pin,kvm_cpus:$$kvm_cpus,source_population_per_boot:true}' \
	"$(1)/run.json" >"$(1)/run.json.tmp"
mv -f "$(1)/run.json.tmp" "$(1)/run.json"
printf '%s\n' "$(5)" >"$(1)/command.txt"
: >"$(1)/stdout.log"
: >"$(1)/stderr.log"
: >"$(1)/launch-order.jsonl"
$(call SPINDLE_STAGING_RQ2_CAPTURE_HOST_BEFORE,$(1))
endef

define SPINDLE_STAGING_RQ2_WRITE_GUEST_MAKEFILE
printf '%s := %s\n' \
	'CONDITION' "$$condition" \
	'REPETITION' "$$repetition" \
	'SPINDLE_STAGING_RQ2_BOOT_DIR' "$${boot_dir#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_RUN_DIR' "$${run_dir#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_RUNNER' "$${runner#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_POLICY' "$${policy#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_BPFTOOL' "$${bpftool#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_RUNTIME_ROOT' "$${runtime_root#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_COMPILED_ROOT' "$${compiled_root#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_SPINDLE' "$${spindle#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_TEST_DIR' "$${test_dir#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_KERNEL_CONFIG' "$${config#$(ROOT_DIR)/}" \
	'SPINDLE_STAGING_RQ2_GUEST_KERNEL_COMMIT' "$$commit" \
	'SPINDLE_STAGING_RQ2_GUEST_KERNEL_RELEASE' "$$release" \
	'SPINDLE_STAGING_RQ2_GUEST_WARMUPS' "$(SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS)" \
	'SPINDLE_STAGING_RQ2_GUEST_SAMPLES' "$(SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES)" \
	>"$$guest_makefile"
endef

.PHONY: spindle-staging-rq2-build spindle-staging-rq2-analysis-test \
	spindle-staging-rq2-host-gate \
	kvm-spindle-staging-rq2-preflight kvm-spindle-staging-rq2 \
	spindle-staging-rq2-run-matrix spindle-staging-rq2-finalize \
	spindle-staging-rq2-report experiment-spindle-staging-rq2 \
	__spindle_staging_rq2_guest __spindle_staging_rq2_guest_inner \
	__spindle_staging_rq2_guest_inventory_after

spindle-staging-rq2-build: $(MDTEST_FUSE_BINARY) spindle-staging
	test "$$(git -C "$(MDTEST_LIBFUSE_SOURCE)" rev-parse HEAD)" = \
		"$(MDTEST_LIBFUSE_COMMIT)"
	test -f "$(MDTEST_LIBFUSE_BUILD)/lib/libfuse3.a"
	$(MAKE) -C "$(ROOT_DIR)/experiments/spindle_staging" \
		ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" \
		LIBFUSE_SOURCE="$(MDTEST_LIBFUSE_SOURCE)" \
		LIBFUSE_BUILD="$(MDTEST_LIBFUSE_BUILD)" rq2
	! readelf -d "$(SPINDLE_STAGING_RQ2_RUNNER)" | \
		grep -F 'libfuse.so'

spindle-staging-rq2-analysis-test:
	python3 -m unittest discover -s \
		"$(ROOT_DIR)/analysis/spindle_staging_rq2" -p 'test_*.py' -v

spindle-staging-rq2-host-gate: kernel kernel-provenance kernel-bpftool bpf \
		workload-spindle-build spindle-staging-rq2-build \
		spindle-staging-rq2-analysis-test
	grep -Fx 'Final verdict: **GO**.' "$(SPINDLE_STAGING_RQ2_PLAN_REVIEW)"
	grep -Fx 'Final amendment verdict: **GO**.' "$(SPINDLE_STAGING_RQ2_PLAN_REVIEW)"
	test "$$(cat "$(KERNEL_COMMIT_FILE)")" = \
		"$(SPINDLE_STAGING_RQ2_EXPECTED_KERNEL_COMMIT)"
	grep -Fx 'CONFIG_FUSE_FS=y' "$(KERNEL_BUILD_DIR)/.config"
	grep -Fx 'CONFIG_FUSE_PASSTHROUGH=y' "$(KERNEL_BUILD_DIR)/.config"
	rm -rf "$(SPINDLE_STAGING_RQ2_HOST_GATE_DIR)"
	install -d "$(SPINDLE_STAGING_RQ2_HOST_GATE_DIR)"
	$(call SPINDLE_STAGING_RQ2_CAPTURE_ARTIFACTS,$(SPINDLE_STAGING_RQ2_HOST_GATE_DIR))
	jq -e \
		--arg spindle "$(SPINDLE_COMMIT)" \
		--arg libfuse "$(MDTEST_LIBFUSE_COMMIT)" \
		'.source.spindle_commit == $$spindle and .source.libfuse.commit == $$libfuse and .runtime.rq2_runner.static_libfuse == true' \
		"$(SPINDLE_STAGING_RQ2_HOST_GATE_DIR)/artifacts/manifest.json" >/dev/null

kvm-spindle-staging-rq2-preflight: experiment-source-clean kernel \
		kernel-provenance kernel-bpftool bpf workload-spindle-build \
		spindle-staging-rq2-host-gate
	test "$(SPINDLE_STAGING_RQ2_PREFLIGHT_REPETITIONS)" = 1
	test "$(SPINDLE_STAGING_RQ2_PREFLIGHT_WARMUPS)" = 1
	test "$(SPINDLE_STAGING_RQ2_PREFLIGHT_SAMPLES)" = 5
	test "$$(cat "$(KERNEL_COMMIT_FILE)")" = \
		"$(SPINDLE_STAGING_RQ2_EXPECTED_KERNEL_COMMIT)"
	grep -Fx 'CONFIG_FUSE_FS=y' "$(KERNEL_BUILD_DIR)/.config"
	grep -Fx 'CONFIG_FUSE_PASSTHROUGH=y' "$(KERNEL_BUILD_DIR)/.config"
	grep -Fx 'Final verdict: **GO**.' "$(SPINDLE_STAGING_RQ2_PLAN_REVIEW)"
	grep -Fx 'Final amendment verdict: **GO**.' "$(SPINDLE_STAGING_RQ2_PLAN_REVIEW)"
	$(call SPINDLE_STAGING_RQ2_START,$(SPINDLE_STAGING_RQ2_PREFLIGHT_RESULT_DIR),1,1,5,make kvm-spindle-staging-rq2-preflight RUN_ID=$(RUN_ID))
	if ! $(MAKE) -C "$(ROOT_DIR)" spindle-staging-rq2-run-matrix \
		RUN_ID="$(RUN_ID)" \
		SPINDLE_STAGING_RQ2_ACTIVE_DIR="$(SPINDLE_STAGING_RQ2_PREFLIGHT_RESULT_DIR)" \
		SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS=1 \
		SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS=1 \
		SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES=5; then \
		failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
		$(call NAMEI_EXT_MARK_RUN_FAILED,$(SPINDLE_STAGING_RQ2_PREFLIGHT_RESULT_DIR),$$failed_at,kvm-matrix); \
		exit 1; \
	fi
	if ! $(MAKE) -C "$(ROOT_DIR)" spindle-staging-rq2-finalize \
		RUN_ID="$(RUN_ID)" \
		SPINDLE_STAGING_RQ2_ACTIVE_DIR="$(SPINDLE_STAGING_RQ2_PREFLIGHT_RESULT_DIR)" \
		SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS=1 \
		SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS=1 \
		SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES=5; then \
		failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
		$(call NAMEI_EXT_MARK_RUN_FAILED,$(SPINDLE_STAGING_RQ2_PREFLIGHT_RESULT_DIR),$$failed_at,finalization-gate); \
		exit 1; \
	fi
	$(call NAMEI_EXT_RUN_COMPLETE,$(SPINDLE_STAGING_RQ2_PREFLIGHT_RESULT_DIR))

kvm-spindle-staging-rq2: experiment-source-clean kernel kernel-provenance \
		kernel-bpftool bpf workload-spindle-build \
		spindle-staging-rq2-host-gate
	test "$(SPINDLE_STAGING_RQ2_FORMAL_REPETITIONS)" = 10
	test "$(SPINDLE_STAGING_RQ2_FORMAL_WARMUPS)" = 3
	test "$(SPINDLE_STAGING_RQ2_FORMAL_SAMPLES)" = 50
	test "$$(cat "$(KERNEL_COMMIT_FILE)")" = \
		"$(SPINDLE_STAGING_RQ2_EXPECTED_KERNEL_COMMIT)"
	grep -Fx 'CONFIG_FUSE_PASSTHROUGH=y' "$(KERNEL_BUILD_DIR)/.config"
	$(call SPINDLE_STAGING_RQ2_START,$(SPINDLE_STAGING_RQ2_RESULT_DIR),10,3,50,make kvm-spindle-staging-rq2 RUN_ID=$(RUN_ID))
	if ! $(MAKE) -C "$(ROOT_DIR)" spindle-staging-rq2-run-matrix \
		RUN_ID="$(RUN_ID)" \
		SPINDLE_STAGING_RQ2_ACTIVE_DIR="$(SPINDLE_STAGING_RQ2_RESULT_DIR)" \
		SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS=10 \
		SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS=3 \
		SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES=50; then \
		failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
		$(call NAMEI_EXT_MARK_RUN_FAILED,$(SPINDLE_STAGING_RQ2_RESULT_DIR),$$failed_at,kvm-matrix); \
		exit 1; \
	fi
	if ! $(MAKE) -C "$(ROOT_DIR)" spindle-staging-rq2-finalize \
		RUN_ID="$(RUN_ID)" \
		SPINDLE_STAGING_RQ2_ACTIVE_DIR="$(SPINDLE_STAGING_RQ2_RESULT_DIR)" \
		SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS=10 \
		SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS=3 \
		SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES=50; then \
		failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
		$(call NAMEI_EXT_MARK_RUN_FAILED,$(SPINDLE_STAGING_RQ2_RESULT_DIR),$$failed_at,finalization-gate); \
		exit 1; \
	fi
	if ! $(MAKE) -C "$(ROOT_DIR)" spindle-staging-rq2-report \
		RUN_ID="$(RUN_ID)"; then \
		failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
		$(call NAMEI_EXT_MARK_RUN_FAILED,$(SPINDLE_STAGING_RQ2_RESULT_DIR),$$failed_at,analysis-gate); \
		exit 1; \
	fi
	$(call NAMEI_EXT_RUN_COMPLETE,$(SPINDLE_STAGING_RQ2_RESULT_DIR))

spindle-staging-rq2-run-matrix:
	test -n "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)"
	test -n "$(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS)"
	test -n "$(SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS)"
	test -n "$(SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES)"
	jq -e '.status == "running" and .layout == "paired-fresh-boot"' \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/run.json" >/dev/null
	: >"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/expected-boots.txt"
	manifest="$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/artifacts/manifest.json"; \
	image="$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/$$(jq -r '.kernel.image' "$$manifest")"; \
	config="$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/$$(jq -r '.kernel.config' "$$manifest")"; \
	commit=$$(jq -r '.kernel.commit' "$$manifest"); \
	release=$$(jq -r '.kernel.release' "$$manifest"); \
	run_dir="$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)"; \
	runner="$$run_dir/$$(jq -r '.runtime.rq2_runner.path' "$$manifest")"; \
	policy="$$run_dir/$$(jq -r '.runtime.policy.path' "$$manifest")"; \
	bpftool="$$run_dir/$$(jq -r '.runtime.bpftool.path' "$$manifest")"; \
	runtime_archive="$$run_dir/$$(jq -r '.source.runtime_tree.path' "$$manifest")"; \
	compiled_root=$$(jq -r '.execution.compiled_root' "$$manifest"); \
	order_index=0; \
	for repetition in $$(seq 1 "$(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS)"); do \
		if (( repetition % 2 )); then conditions=(namei_ext fuse); \
		else conditions=(fuse namei_ext); fi; \
		for condition in "$${conditions[@]}"; do \
			order_index=$$((order_index + 1)); \
			printf '%s|%s\n' "$$repetition" "$$condition" \
				>>"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/expected-boots.txt"; \
			boot_dir="$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/boots/block-$$(printf '%02d' "$$repetition")-$$condition"; \
			mkdir "$$boot_dir"; \
			runtime_root="$$boot_dir/runtime/worktree"; \
			install -d "$$runtime_root"; \
			tar -xzf "$$runtime_archive" -C "$$runtime_root"; \
			spindle="$$compiled_root/prefix/bin/spindle"; \
			test_dir="$$compiled_root/build/testsuite"; \
			(cd "$$runtime_root" && find build prefix -type l \
				-printf '%p\t%l\n' | LC_ALL=C sort) \
				>"$$boot_dir/runtime-symlinks.txt"; \
			guest_makefile="$$boot_dir/guest.mk"; \
			$(call SPINDLE_STAGING_RQ2_WRITE_GUEST_MAKEFILE); \
			guest_makefile_rel="$${guest_makefile#$(ROOT_DIR)/}"; \
			host_started_at=$$(date -u +%Y-%m-%dT%H:%M:%S.%NZ); \
			$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
				__namei_ext_kvm_capture RUN_ID="$(RUN_ID)" \
				NAMEI_EXT_KVM_CAPTURE_IMAGE="$$image" \
				NAMEI_EXT_KVM_CAPTURE_GUEST_TARGET="-f Makefile -f $$guest_makefile_rel __spindle_staging_rq2_guest" \
				NAMEI_EXT_KVM_CAPTURE_BOOT_DIR="$$boot_dir" \
				NAMEI_EXT_KVM_CAPTURE_RUN_DIR="$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)" \
				NAMEI_EXT_KVM_CAPTURE_HOST_CPUS="$(SPINDLE_STAGING_RQ2_HOST_CPUS)" \
				NAMEI_EXT_KVM_CAPTURE_TIMEOUT="$(SPINDLE_STAGING_RQ2_KVM_TIMEOUT)"; \
			host_completed_at=$$(date -u +%Y-%m-%dT%H:%M:%S.%NZ); \
			jq -c -n --argjson order_index "$$order_index" \
				--argjson repetition "$$repetition" \
				--arg condition "$$condition" \
				--arg started_at "$$host_started_at" \
				--arg completed_at "$$host_completed_at" \
				'{schema:"namei_ext.spindle_staging_rq2.launch.v1",order_index:$$order_index,repetition:$$repetition,condition:$$condition,host_started_at:$$started_at,host_completed_at:$$completed_at}' \
				>>"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/launch-order.jsonl"; \
		done; \
	done

spindle-staging-rq2-finalize:
	test -n "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)"
	jq -e '.status == "running" and (.failed_at | not)' \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/run.json" >/dev/null
	cat /proc/stat >"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/host-proc-stat-after.txt"
	cat /proc/interrupts \
		>"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/host-proc-interrupts-after.txt"
	LC_ALL=C sort -o "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/expected-boots.txt" \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/expected-boots.txt"
	$(call NAMEI_EXT_MULTI_BOOT_COLLECT_OBSERVATIONS,$(SPINDLE_STAGING_RQ2_ACTIVE_DIR),$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2)))
	find "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/boots" -mindepth 1 -maxdepth 1 \
		-type d -print0 | LC_ALL=C sort -z | \
		xargs -0 -I{} jq -r '"\(.repetition)|\(.condition)"' '{}/boot.json' | \
		LC_ALL=C sort >"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observed-boots.txt"
	cmp "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/expected-boots.txt" \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observed-boots.txt"
	test "$$(wc -l <"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/launch-order.jsonl")" = \
		"$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2))"
	$(call NAMEI_EXT_MULTI_BOOT_VALIDATE_BOOT_FILES,$(SPINDLE_STAGING_RQ2_ACTIVE_DIR),$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2)),$(SPINDLE_STAGING_RQ2_BOOT_FILES))
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-sample" and .phase == "warmup")] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2 * $(SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS)))"
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-sample" and .phase == "measured")] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2 * $(SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES)))"
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-summary" and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2))"
	for event in spindle-staging-mapping spindle-staging-rq2-target \
		spindle-staging-preservation; do \
		test "$$(jq -s --arg event "$$event" '[.[] | select(.event == $$event and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
			"$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2 * 47))"; \
	done
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-identity" and .bytes_equal == true and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$$(($(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS) * 2 * 47))"
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-fuse-config" and .allow_other == true and .default_permissions == true and .passthrough_negotiated == true and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS)"
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-fuse-counter" and .counter == "read_fallback" and .delta == 0 and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS)"
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-fuse-resource" and .cpu_runtime_ns > 0 and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl")" = \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS)"
	! jq -e 'select(.pass != true)' \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/observations.jsonl" >/dev/null
	while IFS= read -r -d '' boot; do \
		jq -e '.status == "completed" and .prepare_status == 0 and .inner_status == 0 and .cleanup_status == 0 and .inventory_status == 0 and .dmesg_status == 0' "$$boot/boot.json" >/dev/null; \
		jq -e '.schema == "namei_ext.vcpu_affinity.v1" and .status == "verified"' "$$boot/vcpu-affinity.json" >/dev/null; \
		test "$$(cat "$$boot/affinity-barrier.txt")" = "$$(jq -r '.verified_at' "$$boot/vcpu-affinity.json")"; \
	done < <(find "$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/boots" \
		-mindepth 1 -maxdepth 1 -type d -print0 | LC_ALL=C sort -z)
	jq -e --argjson repetitions "$(SPINDLE_STAGING_RQ2_ACTIVE_REPETITIONS)" \
		--argjson warmups "$(SPINDLE_STAGING_RQ2_ACTIVE_WARMUPS)" \
		--argjson samples "$(SPINDLE_STAGING_RQ2_ACTIVE_SAMPLES)" \
		'.status == "running" and .source.dirty == false and .kernel.dirty == false and .matrix.repetitions == $$repetitions and .matrix.warmups_per_boot == $$warmups and .matrix.measured_samples_per_boot == $$samples' \
		"$(SPINDLE_STAGING_RQ2_ACTIVE_DIR)/run.json" >/dev/null

spindle-staging-rq2-report:
	jq -e '.status == "running" and (.completed_at | not) and (.failed_at | not)' \
		"$(SPINDLE_STAGING_RQ2_RESULT_DIR)/run.json" >/dev/null
	$(call NAMEI_EXT_ANALYSIS_PREPARE,$(SPINDLE_STAGING_RQ2_RESULT_DIR)/analysis)
	python3 "$(SPINDLE_STAGING_RQ2_ANALYSIS)" \
		--input "$(SPINDLE_STAGING_RQ2_RESULT_DIR)/observations.jsonl" \
		--run "$(SPINDLE_STAGING_RQ2_RESULT_DIR)/run.json" \
		--launch-order "$(SPINDLE_STAGING_RQ2_RESULT_DIR)/launch-order.jsonl" \
		--output "$(SPINDLE_STAGING_RQ2_RESULT_DIR)/analysis.tmp" \
		--seed "$(SPINDLE_STAGING_RQ2_ANALYSIS_SEED)"
	for file in summary.json summary.csv report.md; do \
		test -s "$(SPINDLE_STAGING_RQ2_RESULT_DIR)/analysis.tmp/$$file"; \
	done
	$(call NAMEI_EXT_ANALYSIS_PUBLISH,$(SPINDLE_STAGING_RQ2_RESULT_DIR)/analysis)

experiment-spindle-staging-rq2: kvm-spindle-staging-rq2

__spindle_staging_rq2_guest:
	install -d "$(SPINDLE_STAGING_RQ2_BOOT_DIR)"
	prepare_status=0; \
	$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
		__namei_ext_guest_prepare || prepare_status=$$?; \
	printf '%s\n' "$$prepare_status" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/guest-prepare.status"; \
	inner_status=125; \
	if test "$$prepare_status" -eq 0; then \
		inner_status=0; \
		$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
			-f Makefile -f "$(lastword $(MAKEFILE_LIST))" \
			__spindle_staging_rq2_guest_inner || inner_status=$$?; \
	fi; \
	printf '%s\n' "$$inner_status" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/guest-inner.status"; \
	if test -s "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/raw-runner.jsonl" && \
	   test ! -e "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl"; then \
		jq -c --arg condition "$(CONDITION)" \
			--argjson repetition "$(REPETITION)" \
			'. + {condition:$$condition,repetition:$$repetition}' \
			"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/raw-runner.jsonl" \
			>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl"; \
	fi; \
	cleanup_status=0; \
	for spec in hello_.py:600 hello_x.py:700; do \
		path="$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)/build/testsuite/$${spec%%:*}"; \
		if test -e "$$path"; then chmod "$${spec##*:}" "$$path" || cleanup_status=$$?; fi; \
	done; \
	if mountpoint -q "$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)"; then \
		umount -R "$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)" || cleanup_status=$$?; \
	fi; \
	mount_status=0; \
	mountpoint -q "$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)" || mount_status=$$?; \
	if test "$$mount_status" -eq 0; then cleanup_status=1; fi; \
	printf '%s\n' "$$mount_status" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/runtime-mount-after.status"; \
	printf '%s\n' "$$cleanup_status" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/guest-cleanup.status"; \
	inventory_status=0; \
	$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
		-f Makefile -f "$(lastword $(MAKEFILE_LIST))" \
		__spindle_staging_rq2_guest_inventory_after || inventory_status=$$?; \
	printf '%s\n' "$$inventory_status" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/guest-inventory-after.status"; \
	dmesg_status=0; \
	dmesg >"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/dmesg.log" || dmesg_status=$$?; \
	if test "$$dmesg_status" -eq 0; then \
		$(call NAMEI_EXT_GUEST_ASSERT_DMESG_CLEAN,$(SPINDLE_STAGING_RQ2_BOOT_DIR)/dmesg.log) || dmesg_status=$$?; \
	fi; \
	printf '%s\n' "$$dmesg_status" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/guest-dmesg.status"; \
	boot_status=completed; \
	if test "$$prepare_status" -ne 0 || test "$$inner_status" -ne 0 || \
	   test "$$cleanup_status" -ne 0 || test "$$inventory_status" -ne 0 || \
	   test "$$dmesg_status" -ne 0; then boot_status=failed; fi; \
	jq -n --arg condition "$(CONDITION)" \
		--argjson repetition "$(REPETITION)" \
		--arg kernel_commit "$(SPINDLE_STAGING_RQ2_GUEST_KERNEL_COMMIT)" \
		--arg kernel_release "$(SPINDLE_STAGING_RQ2_GUEST_KERNEL_RELEASE)" \
		--arg status "$$boot_status" \
		--argjson prepare_status "$$prepare_status" \
		--argjson inner_status "$$inner_status" \
		--argjson cleanup_status "$$cleanup_status" \
		--argjson inventory_status "$$inventory_status" \
		--argjson dmesg_status "$$dmesg_status" \
		--arg completed_at "$$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
		'{schema:"namei_ext.spindle_staging_rq2.boot.v1",condition:$$condition,repetition:$$repetition,kernel_commit:$$kernel_commit,kernel_release:$$kernel_release,status:$$status,prepare_status:$$prepare_status,inner_status:$$inner_status,cleanup_status:$$cleanup_status,inventory_status:$$inventory_status,dmesg_status:$$dmesg_status,completed_at:$$completed_at}' \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/boot.json"; \
	test "$$boot_status" = completed

__spindle_staging_rq2_guest_inventory_after:
	$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,\
		$(SPINDLE_STAGING_RQ2_BOOT_DIR),\
		$(SPINDLE_STAGING_RQ2_GUEST_BPFTOOL),after)
	jq -e 'type == "array" and length == 0' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/bpf-programs-after.json" >/dev/null
	jq -e 'type == "array" and ([.. | objects | select(has("id"))] | length) == 0' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/bpf-cgroup-after.json" >/dev/null
	test ! -s "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/fuse-mounts-after.txt"
	test "$$(cat "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/fuse-open-fds-after.status")" = 1
	test ! -s "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/fuse-open-fds-after.txt"
	test ! -e /tmp/namei-ext-spindle-cache
	test ! -e /tmp/namei-ext-spindle-comm
	test ! -e /tmp/namei-ext-spindle-tmp
	test "$$(find /sys/fs/cgroup -maxdepth 1 -type d \
		-name 'namei-ext-spindle-rq2-*' | wc -l)" = 0

__spindle_staging_rq2_guest_inner:
	case "$(CONDITION)" in namei_ext|fuse) ;; *) exit 1;; esac
	test -n "$(REPETITION)"
	test -x "$(SPINDLE_STAGING_RQ2_GUEST_RUNNER)"
	test -r "$(SPINDLE_STAGING_RQ2_GUEST_POLICY)"
	test -x "$(SPINDLE_STAGING_RQ2_GUEST_BPFTOOL)"
	test "$(SPINDLE_STAGING_RQ2_GUEST_WARMUPS)" -gt 0
	test "$(SPINDLE_STAGING_RQ2_GUEST_SAMPLES)" -gt 0
	command -v fusermount3 >/dev/null
	affinity_status=waiting; \
	for attempt in $$(seq 1 500); do \
		if jq -e '.schema == "namei_ext.vcpu_affinity.v1" and .status == "verified"' \
			"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/vcpu-affinity.json" >/dev/null 2>&1; then \
			affinity_status=verified; break; \
		fi; \
		sleep 0.05; \
	done; \
	test "$$affinity_status" = verified; \
	jq -r '.verified_at' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/vcpu-affinity.json" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/affinity-barrier.txt"
	$(call NAMEI_EXT_GUEST_CAPTURE_KERNEL_EVIDENCE,\
		$(SPINDLE_STAGING_RQ2_BOOT_DIR),\
		$(SPINDLE_STAGING_RQ2_GUEST_KERNEL_CONFIG),\
		$(SPINDLE_STAGING_RQ2_GUEST_KERNEL_COMMIT),\
		$(SPINDLE_STAGING_RQ2_GUEST_KERNEL_RELEASE))
	grep -q ' [Tt] namei_ext_lookup$$' /proc/kallsyms
	(cd "$(SPINDLE_STAGING_RQ2_GUEST_RUNTIME_ABS)" && \
		find build prefix -type l -printf '%p\t%l\n' | LC_ALL=C sort) | \
		cmp - "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/runtime-symlinks.txt"
	runtime_mount_status=0; \
	mountpoint -q "$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)" || \
		runtime_mount_status=$$?; \
	printf '%s\n' "$$runtime_mount_status" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/runtime-mount-before.status"; \
	test "$$runtime_mount_status" -ne 0
	mount -t tmpfs -o size=512m,mode=0755 tmpfs \
		"$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)"
	mountpoint -q "$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)"
	cp -a "$(SPINDLE_STAGING_RQ2_GUEST_RUNTIME_ABS)/." \
		"$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)/"
	(cd "$(SPINDLE_STAGING_RQ2_GUEST_COMPILED_ABS)" && \
		find build prefix -type l -printf '%p\t%l\n' | LC_ALL=C sort) | \
		cmp - "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/runtime-symlinks.txt"
	test -x "$(SPINDLE_STAGING_RQ2_GUEST_SPINDLE_ABS)"
	test -x "$(SPINDLE_STAGING_RQ2_GUEST_TEST_DIR_ABS)/test_driver"
	findmnt -n -o FSTYPE -T \
		"$(SPINDLE_STAGING_RQ2_GUEST_TEST_DIR_ABS)/test_driver" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/runtime-lower-fstype.txt"
	grep -Fx 'tmpfs' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/runtime-lower-fstype.txt"
	$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,\
		$(SPINDLE_STAGING_RQ2_BOOT_DIR),\
		$(SPINDLE_STAGING_RQ2_GUEST_BPFTOOL),before)
	jq -e 'type == "array" and length == 0' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/bpf-programs-before.json" >/dev/null
	test ! -s "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/fuse-mounts-before.txt"
	chmod 0200 "$(SPINDLE_STAGING_RQ2_GUEST_TEST_DIR_ABS)/hello_.py"
	chmod 0300 "$(SPINDLE_STAGING_RQ2_GUEST_TEST_DIR_ABS)/hello_x.py"
	: >"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/stdout.log"
	: >"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/stderr.log"
	: >"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/raw-runner.jsonl"
	runtime_fstype=$$(cat \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/runtime-lower-fstype.txt"); \
	jq -cn --arg runtime_fstype "$$runtime_fstype" \
		'{event:"spindle-staging-rq2-lower-filesystem",runtime_fstype:$$runtime_fstype,pass:true}' \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/raw-runner.jsonl"
	"$(SPINDLE_STAGING_RQ2_GUEST_RUNNER)" "$(CONDITION)" \
		"$(SPINDLE_STAGING_RQ2_GUEST_POLICY)" \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/raw-runner.jsonl" \
		"$(SPINDLE_STAGING_RQ2_GUEST_SPINDLE_ABS)" \
		"$(SPINDLE_STAGING_RQ2_GUEST_TEST_DIR_ABS)" \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)" /sys/fs/cgroup \
		"$(SPINDLE_STAGING_RQ2_GUEST_WARMUPS)" \
		"$(SPINDLE_STAGING_RQ2_GUEST_SAMPLES)" \
		>>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/stdout.log" \
		2>>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/stderr.log"
	jq -c --arg condition "$(CONDITION)" \
		--argjson repetition "$(REPETITION)" \
		'. + {condition:$$condition,repetition:$$repetition}' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/raw-runner.jsonl" \
		>"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl"
	! jq -e 'select(.pass != true)' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-sample" and .phase == "warmup")] | length' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl")" = \
		"$(SPINDLE_STAGING_RQ2_GUEST_WARMUPS)"
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-sample" and .phase == "measured")] | length' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl")" = \
		"$(SPINDLE_STAGING_RQ2_GUEST_SAMPLES)"
	for phase_count in warmup:$(SPINDLE_STAGING_RQ2_GUEST_WARMUPS) \
		measured:$(SPINDLE_STAGING_RQ2_GUEST_SAMPLES); do \
		phase="$${phase_count%%:*}"; expected="$${phase_count##*:}"; \
		jq -e -s --arg phase "$$phase" --argjson expected "$$expected" \
			'[.[] | select(.event == "spindle-staging-rq2-sample" and .phase == $$phase) | .iteration] | sort == [range(1; $$expected + 1)]' \
			"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null; \
	done
	for event in spindle-staging-mapping spindle-staging-rq2-target \
		spindle-staging-preservation; do \
		test "$$(jq -s --arg event "$$event" '[.[] | select(.event == $$event and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl")" = 47; \
	done
	test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-identity" and .bytes_equal == true and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl")" = 47
	jq -e 'select(.event == "spindle-staging-rq2-withdrawal" and .expected_diagnostic == true and .pass == true)' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null
	jq -e 'select(.event == "spindle-staging-rq2-permission" and .observed_errno == 13 and .restore_errno == 0 and .pass == true)' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null
	jq -e 'select(.event == "spindle-staging-rq2-withdrawal-lookup" and .operation == "fstatat" and .observed_errno == 2 and .expected_errno == 2 and .pass == true)' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null
	jq -e 'select(.event == "spindle-staging-rq2-withdrawal-window" and .before == .after and ((.condition != "namei_ext") or (.hide_after > .hide_before)) and .pass == true)' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null
	jq -e 'select(.event == "spindle-staging-rq2-summary" and .focal_objects == 47 and .failures == 0 and .pass == true)' \
		"$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null
	case "$(CONDITION)" in \
	namei_ext) \
		jq -e 'select(.event == "spindle-staging-rq2-namei-window" and .select_delta == .per_target_sum and .select_delta >= 47 and .pass == true)' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null ;; \
	fuse) \
		jq -e 'select(.event == "spindle-staging-rq2-fuse-config" and .libfuse_version == "3.18.2" and .allow_other == true and .default_permissions == true and .passthrough_negotiated == true and .pass == true)' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null; \
		test "$$(jq -s '[.[] | select(.event == "spindle-staging-rq2-fuse-invalidation" and .status == 0 and .inode_status == 0 and (.entry_status == 0 or .entry_status == -2) and .epoch_status == 0 and (.epoch_attempted == (.entry_status == -2)) and .pass == true)] | length' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl")" = 3; \
		jq -e 'select(.event == "spindle-staging-rq2-fuse-counter" and .counter == "read_fallback" and .delta == 0 and .pass == true)' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null; \
		jq -e 'select(.event == "spindle-staging-rq2-fuse-counter" and .counter == "passthrough_failure" and .delta == 0 and .pass == true)' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null; \
		jq -e 'select(.event == "spindle-staging-rq2-fuse-resource" and .cpu_runtime_ns > 0 and .pass == true)' "$(SPINDLE_STAGING_RQ2_BOOT_DIR)/observations.jsonl" >/dev/null ;; \
	esac
	test ! -e /tmp/namei-ext-spindle-cache
	test ! -e /tmp/namei-ext-spindle-comm
	test ! -e /tmp/namei-ext-spindle-tmp

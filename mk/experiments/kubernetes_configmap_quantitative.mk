KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD ?= \
	$(BUILD_ROOT)/kubernetes-configmap-quantitative
KUBERNETES_CONFIGMAP_QUANTITATIVE_CONSUMER ?= \
	$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/configmap-consumer
KUBERNETES_CONFIGMAP_QUANTITATIVE_RUNNER ?= \
	$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/namei-ext-configmap-quantitative
KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER_SOURCE ?= \
	$(ROOT_DIR)/experiments/kubernetes_configmap_quantitative/atomic_writer_quantitative_driver.go
KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER ?= \
	$(KUBERNETES_CONFIGMAP_WORKLOAD_ROOT)/atomic-writer-quantitative-driver
KUBERNETES_CONFIGMAP_QUANTITATIVE_POLICY ?= \
	$(BUILD_ROOT)/bpf/kubernetes_configmap_publication.bpf.o
KUBERNETES_CONFIGMAP_QUANTITATIVE_ANALYZER ?= \
	$(ROOT_DIR)/analysis/kubernetes_configmap_quantitative/analyze.py
KUBERNETES_CONFIGMAP_QUANTITATIVE_VALIDATOR ?= \
	$(ROOT_DIR)/analysis/kubernetes_configmap_quantitative/validate.py
KUBERNETES_CONFIGMAP_QUANTITATIVE_SOURCE_METADATA ?= \
	$(KUBERNETES_CONFIGMAP_WORKLOAD_ROOT)/quantitative-source-metadata.json

KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_FILES := \
	boot.json observations.jsonl dmesg.log \
	bpf-programs-before.json bpf-programs-after.json \
	bpf-cgroup-before.json bpf-cgroup-after.json \
	fuse-mounts-before.txt fuse-mounts-after.txt \
	fuse-open-fds-before.txt fuse-open-fds-before.status \
	fuse-open-fds-after.txt fuse-open-fds-after.status \
	guest-inner.status guest-inventory-after.status guest-dmesg.status \
	kernel.config kernel-commit.txt kernel-release.txt \
	uname.txt proc-version.txt kernel-cmdline.txt \
	vcpu-affinity-pin.json vcpu-affinity.json affinity-barrier.txt \
	host-block-backing-filesystem.txt host-block-create.json \
	host-block-cleanup.json guest-block-device.txt guest-block-cleanup.json \
	ext4-filesystem.txt ext4-statfs.txt ext4-block-signature.txt mkfs-ext4.log \
	mechanism.status launcher.stdout.log launcher.stderr.log

KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_FILES := \
	host-lscpu.txt host-lscpu-extended.txt host-cpu-pin.txt \
	host-cpu-frequency-policy.txt vng-version.txt \
	host-proc-stat-before.txt host-proc-stat-after.txt \
	host-proc-interrupts-before.txt host-proc-interrupts-after.txt

define KUBERNETES_CONFIGMAP_QUANTITATIVE_CAPTURE_HOST
lscpu >"$(1)/host-lscpu.txt"
lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE,MAXMHZ,MINMHZ \
	>"$(1)/host-lscpu-extended.txt"
printf '%s\n' "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_CPUS)" \
	>"$(1)/host-cpu-pin.txt"
printf 'intel_pstate_no_turbo=%s\n' \
	"$$(cat /sys/devices/system/cpu/intel_pstate/no_turbo)" \
	>"$(1)/host-cpu-frequency-policy.txt"; \
for cpu in $$(seq 4 7); do \
	printf 'cpu=%s governor=%s driver=%s max_khz=%s\n' "$$cpu" \
		"$$(cat /sys/devices/system/cpu/cpu$$cpu/cpufreq/scaling_governor)" \
		"$$(cat /sys/devices/system/cpu/cpu$$cpu/cpufreq/scaling_driver)" \
		"$$(cat /sys/devices/system/cpu/cpu$$cpu/cpufreq/cpuinfo_max_freq)" \
		>>"$(1)/host-cpu-frequency-policy.txt"; \
done
"$(VNG)" --version >"$(1)/vng-version.txt"
cat /proc/stat >"$(1)/host-proc-stat-before.txt"
cat /proc/interrupts >"$(1)/host-proc-interrupts-before.txt"
endef

$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER): \
		$(KUBERNETES_CONFIGMAP_SOURCE_READY) \
		$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER_SOURCE)
	cd "$(KUBERNETES_CONFIGMAP_SOURCE)" && \
		CGO_ENABLED=0 GOTOOLCHAIN=local GOFLAGS=-mod=vendor \
		go build -o "$@" \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER_SOURCE)"

$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SOURCE_METADATA): \
		$(KUBERNETES_CONFIGMAP_SOURCE_METADATA) \
		$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER) \
		$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER_SOURCE)
	jq \
		--arg adapter experiments/kubernetes_configmap_quantitative/atomic_writer_quantitative_driver.go \
		--arg adapter_command 'cd $(KUBERNETES_CONFIGMAP_SOURCE) && CGO_ENABLED=0 GOTOOLCHAIN=local GOFLAGS=-mod=vendor go build -o $(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER) $(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER_SOURCE)' \
		'.suite = "kubernetes-configmap-quantitative" | .adapter = $$adapter | .build.adapter_command = $$adapter_command' \
		"$<" >"$@.tmp"
	mv -f "$@.tmp" "$@"

define KUBERNETES_CONFIGMAP_QUANTITATIVE_CAPTURE_ARTIFACTS
install -d "$(1)/artifacts/kernel" "$(1)/artifacts/runtime" \
	"$(1)/artifacts/source"
install -m 0444 "$(KERNEL_IMAGE)" "$(1)/artifacts/kernel/bzImage"
install -m 0444 "$(KERNEL_BUILD_DIR)/.config" \
	"$(1)/artifacts/kernel/config"
install -m 0555 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_CONSUMER)" \
	"$(1)/artifacts/runtime/configmap-consumer"
install -m 0555 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUNNER)" \
	"$(1)/artifacts/runtime/namei-ext-configmap-quantitative"
install -m 0555 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER)" \
	"$(1)/artifacts/runtime/atomic-writer-quantitative-driver"
install -m 0444 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_POLICY)" \
	"$(1)/artifacts/runtime/kubernetes_configmap_publication.bpf.o"
install -m 0555 "$(KUBERNETES_CONFIGMAP_BPFTOOL)" \
	"$(1)/artifacts/runtime/bpftool"
install -m 0444 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SOURCE_METADATA)" \
	"$(1)/artifacts/source/source-metadata.json"
install -m 0444 "$(KUBERNETES_CONFIGMAP_UPSTREAM_TEST_LOG)" \
	"$(1)/artifacts/source/atomic-writer-tests.log"
install -m 0444 \
	"$(KUBERNETES_CONFIGMAP_SOURCE)/pkg/volume/util/atomic_writer.go" \
	"$(1)/artifacts/source/atomic_writer.go"
install -m 0444 \
	"$(KUBERNETES_CONFIGMAP_SOURCE)/pkg/volume/util/atomic_writer_test.go" \
	"$(1)/artifacts/source/atomic_writer_test.go"
install -m 0444 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER_SOURCE)" \
	"$(1)/artifacts/source/atomic_writer_quantitative_driver.go"
install -m 0444 \
	"$(ROOT_DIR)/experiments/kubernetes_configmap_quantitative/configmap_consumer.c" \
	"$(1)/artifacts/source/configmap_consumer.c"
install -m 0444 \
	"$(ROOT_DIR)/experiments/kubernetes_configmap_quantitative/namei_ext_configmap_quantitative.c" \
	"$(1)/artifacts/source/namei_ext_configmap_quantitative.c"
install -m 0444 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ANALYZER)" \
	"$(1)/artifacts/source/analyze.py"
install -m 0444 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_VALIDATOR)" \
	"$(1)/artifacts/source/validate.py"
jq -n \
	--arg kernel_image artifacts/kernel/bzImage \
	--arg kernel_config artifacts/kernel/config \
	--arg consumer artifacts/runtime/configmap-consumer \
	--arg runner artifacts/runtime/namei-ext-configmap-quantitative \
	--arg source_driver artifacts/runtime/atomic-writer-quantitative-driver \
	--arg policy artifacts/runtime/kubernetes_configmap_publication.bpf.o \
	--arg bpftool artifacts/runtime/bpftool \
	--arg source_metadata artifacts/source/source-metadata.json \
	'{kernel:{image:$$kernel_image,config:$$kernel_config},runtime:{consumer:$$consumer,runner:$$runner,source_driver:$$source_driver,policy:$$policy,bpftool:$$bpftool},source:{metadata:$$source_metadata,atomic_writer:"artifacts/source/atomic_writer.go",atomic_writer_test:"artifacts/source/atomic_writer_test.go",upstream_test_log:"artifacts/source/atomic-writer-tests.log",analyzer:"artifacts/source/analyze.py",validator:"artifacts/source/validate.py"}}' \
	>"$(1)/artifacts/manifest.json"
endef

define KUBERNETES_CONFIGMAP_QUANTITATIVE_START
$(call NAMEI_EXT_RESULT_ROOT_CREATE,$(1))
$(call NAMEI_EXT_MULTI_BOOT_INIT,$(1))
$(call NAMEI_EXT_RUN_START,$(1),kubernetes-configmap-quantitative,Kubernetes-AtomicWriter-vs-namei_ext,kvm_kubernetes_configmap_quantitative,$(1)/observations.jsonl,kubernetes_configmap_publication.bpf.c,configmap-consumer+atomic-writer-quantitative-driver+namei-ext-configmap-quantitative)
$(call KUBERNETES_CONFIGMAP_QUANTITATIVE_CAPTURE_HOST,$(1))
$(call KUBERNETES_CONFIGMAP_QUANTITATIVE_CAPTURE_ARTIFACTS,$(1))
jq \
	--argjson boots "$(2)" \
	--argjson pairs "$(3)" \
	--arg scales "$(4)" \
	--arg host_cpu_pin "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_CPUS)" \
	--arg ext4_size "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_EXT4_SIZE)" \
	--argjson kvm_cpus "$(KVM_CPUS)" \
	--arg kvm_memory "$(KVM_MEM)" \
		'.layout = "paired-two-known-generation-configmap-lifecycle" | .matrix = {baseline:"Kubernetes-v1.30.0-AtomicWriter",proposed:"namei_ext",states:["initial","update","no-op","rollback"],boots:$$boots,pairs_per_scale_per_boot:$$pairs,scales:($$scales | split(" ") | map(select(length > 0) | tonumber)),all_rows_must_pass:true} | .guest_launch = {kvm_cpus:$$kvm_cpus,kvm_memory:$$kvm_memory,host_cpu_pin:$$host_cpu_pin,affinity:"qmp-pinned-and-verified"} | .filesystem = {type:"ext4",layout:"fresh-virtio-block-per-boot",image_format:"raw",image_size:$$ext4_size,host_backing_filesystem:"ext4",qemu_cache:"none",mkfs_options:["-m","0","-E","lazy_itable_init=0,lazy_journal_init=0"],mount_options:["noatime","nosuid","nodev"]} | .setup_measurement = {primary_metric:"wall_span_ns",setup_aggregate:"setup_ns",proposed_components:["object_preparation_ns","target_registration_ns","map_population_ns","consumer_cgroup_move_ns"],component_sum:"exact",registered_targets:"2*(width-1)",registration_control:"single-child-repeated-control-write"}' \
	"$(1)/run.json" >"$(1)/run.json.tmp"
mv -f "$(1)/run.json.tmp" "$(1)/run.json"
printf '%s\n' "$(5)" >"$(1)/command.txt"
: >"$(1)/stdout.log"
: >"$(1)/stderr.log"
endef

.PHONY: kubernetes-configmap-quantitative \
	kubernetes-configmap-quantitative-source \
	kubernetes-configmap-quantitative-analysis-test \
	kubernetes-configmap-quantitative-host-test \
	kubernetes-configmap-quantitative-host-failure-test \
	kvm-kubernetes-configmap-quantitative-preflight \
	kvm-kubernetes-configmap-quantitative \
	kubernetes-configmap-quantitative-run \
	kubernetes-configmap-quantitative-finalize \
	kubernetes-configmap-quantitative-analyze \
	experiment-kubernetes-configmap-quantitative \
	__kubernetes_configmap_quantitative_guest \
	__kubernetes_configmap_quantitative_guest_inner \
	__kubernetes_configmap_quantitative_guest_mechanism

kubernetes-configmap-quantitative:
	$(MAKE) -C "$(ROOT_DIR)/experiments/kubernetes_configmap_quantitative" \
		ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

kubernetes-configmap-quantitative-source: \
	$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER) \
	$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SOURCE_METADATA)
	jq -e --arg commit "$(KUBERNETES_CONFIGMAP_COMMIT)" \
		'.suite == "kubernetes-configmap-quantitative" and .commit == $$commit and .adapter == "experiments/kubernetes_configmap_quantitative/atomic_writer_quantitative_driver.go" and .upstream_tests == ["TestWriteOnce","TestUpdate","TestMultipleUpdates"]' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SOURCE_METADATA)" >/dev/null

kubernetes-configmap-quantitative-analysis-test:
	cd "$(ROOT_DIR)/analysis/kubernetes_configmap_quantitative" && \
		python3 -m unittest -v

kubernetes-configmap-quantitative-host-test: \
		kubernetes-configmap-quantitative \
		kubernetes-configmap-quantitative-source
	rm -rf "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test"
	install -d "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample"
	"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER)" \
		timed \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/observations.jsonl" \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_CONSUMER)" \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample" \
		16 1 1 1
	"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER)" \
		audit \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/observations.jsonl" \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample" \
		16 1 1
	mountpoint_status=0; \
		mountpoint -q "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample" || \
		mountpoint_status=$$?; \
		test "$$mountpoint_status" -eq 32
	install -d "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/lost+found"
	test "$$(find "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/lost+found" -mindepth 1 -print -quit | wc -l)" = 0
	test "$$(find "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample" -mindepth 1 -maxdepth 1 ! -name lost+found -print -quit | wc -l)" = 0
	touch "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/lost+found/residual"
	if test "$$(find "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/lost+found" -mindepth 1 -print -quit | wc -l)" = 0; then exit 1; fi
	rm "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/lost+found/residual"
	touch "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/residual"
	if test "$$(find "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample" -mindepth 1 -maxdepth 1 ! -name lost+found -print -quit | wc -l)" = 0; then exit 1; fi
	rm "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/residual"
	rmdir "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample/lost+found"
	rmdir "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/sample"
	PYTHONPATH="$(ROOT_DIR)/analysis/kubernetes_configmap_quantitative" \
		python3 -c 'import json,sys,validate; rows=[json.loads(line) for line in open(sys.argv[1], encoding="utf-8")]; validate.validate_lifecycle(rows[0]); validate.validate_atomicwriter(rows[0], rows[1:])' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-test/observations.jsonl"

kubernetes-configmap-quantitative-host-failure-test: \
		kubernetes-configmap-quantitative-source
	rm -rf "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-failure-test"
	install -d "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-failure-test/sample"
	if "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_DRIVER)" \
		timed \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-failure-test/observations.jsonl" \
		/bin/false \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-failure-test/sample" \
		16 1 1 1; then exit 1; fi
	test "$$(find "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-failure-test/sample" -mindepth 1 -print -quit | wc -l)" = 0
	jq -s -e 'length == 1 and .[0] as $$row | $$row.pass == false and $$row.cleanup_pass == true and ($$row.error | length) > 0 and $$row.consumer_exit_status != 0 and $$row.cleanup_root_absent == true and $$row.cleanup_root_remove_error == 0 and $$row.cleanup_root_stat_error == 2 and $$row.cleanup_parent_read_error == 0 and $$row.cleanup_parent_entries == []' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BUILD)/host-failure-test/observations.jsonl" >/dev/null

kvm-kubernetes-configmap-quantitative-preflight: \
		experiment-source-clean \
		kernel kernel-provenance kernel-bpftool bpf \
		kubernetes-configmap-quantitative \
		kubernetes-configmap-quantitative-source
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_BOOTS)" = 1
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_PAIRS)" = 2
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_SCALES)" = "16 256"
	test "$(KVM_CPUS)" = 4
	test "$(KVM_MEM)" = 8G
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_CPUS)" = 4-7
	$(call NAMEI_EXT_VALIDATE_HOST_CPU_PIN,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_CPUS),$(KVM_CPUS))
	$(call KUBERNETES_CONFIGMAP_QUANTITATIVE_START,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_RESULT_DIR),1,2,16 256,make kvm-kubernetes-configmap-quantitative-preflight RUN_ID=$(RUN_ID))
	$(MAKE) -C "$(ROOT_DIR)" kubernetes-configmap-quantitative-run \
		RUN_ID="$(RUN_ID)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_RESULT_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS=1 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS=2 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES="16 256"
	$(MAKE) -C "$(ROOT_DIR)" kubernetes-configmap-quantitative-finalize \
		RUN_ID="$(RUN_ID)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_RESULT_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS=1 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS=2 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES="16 256"
	$(MAKE) -C "$(ROOT_DIR)" kubernetes-configmap-quantitative-analyze \
		RUN_ID="$(RUN_ID)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_RESULT_DIR)"
	$(call NAMEI_EXT_RUN_COMPLETE,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PREFLIGHT_RESULT_DIR))

kvm-kubernetes-configmap-quantitative: \
		experiment-source-clean \
		kernel kernel-provenance kernel-bpftool bpf \
		kubernetes-configmap-quantitative \
		kubernetes-configmap-quantitative-source
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOTS)" = 20
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_PAIRS)" = 5
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SCALES)" = "4 16 64 256"
	test "$(KVM_CPUS)" = 4
	test "$(KVM_MEM)" = 8G
	test "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_CPUS)" = 4-7
	$(call NAMEI_EXT_VALIDATE_HOST_CPU_PIN,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_CPUS),$(KVM_CPUS))
	$(call KUBERNETES_CONFIGMAP_QUANTITATIVE_START,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RESULT_DIR),20,5,4 16 64 256,make experiment-kubernetes-configmap-quantitative RUN_ID=$(RUN_ID))
	$(MAKE) -C "$(ROOT_DIR)" kubernetes-configmap-quantitative-run \
		RUN_ID="$(RUN_ID)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RESULT_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS=20 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS=5 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES="4 16 64 256"
	$(MAKE) -C "$(ROOT_DIR)" kubernetes-configmap-quantitative-finalize \
		RUN_ID="$(RUN_ID)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RESULT_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS=20 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS=5 \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES="4 16 64 256"
	$(MAKE) -C "$(ROOT_DIR)" kubernetes-configmap-quantitative-analyze \
		RUN_ID="$(RUN_ID)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RESULT_DIR)"
	$(call NAMEI_EXT_RUN_COMPLETE,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RESULT_DIR))

experiment-kubernetes-configmap-quantitative: \
	kvm-kubernetes-configmap-quantitative

kubernetes-configmap-quantitative-run:
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS)"
	jq -e '.status == "running" and (.completed_at | not)' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/run.json" >/dev/null
	test ! -e "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/execution-started"; \
		test ! -s "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/expected-boots.txt"; \
		test "$$(find "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/boots" -mindepth 1 -print -quit | wc -l)" = 0; \
		: >"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/execution-started"
	for boot_index in $$(seq 1 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS)"); do \
		printf '%s\n' "$$boot_index" \
			>>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/expected-boots.txt"; \
		boot="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/boots/repetition-$$(printf '%02d' "$$boot_index")"; \
		mkdir "$$boot"; \
		install -d "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_ROOT)"; \
		block_image="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_ROOT)/$(RUN_ID)-repetition-$$(printf '%02d' "$$boot_index").raw"; \
		if test -e "$$block_image"; then \
			failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
			$(call NAMEI_EXT_MARK_RUN_FAILED,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR),$$failed_at,host-block-image-already-exists) || exit 1; \
			exit 1; \
		fi; \
		block_create_status=0; \
		truncate -s "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_EXT4_SIZE)" "$$block_image" || block_create_status=$$?; \
		block_apparent_bytes=0; \
		if test -e "$$block_image"; then block_apparent_bytes=$$(stat -c %s "$$block_image"); fi; \
		jq -n --argjson status "$$block_create_status" \
			--arg path "$$block_image" \
			--arg size "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_EXT4_SIZE)" \
			--argjson apparent_bytes "$$block_apparent_bytes" \
			'{status:$$status,path:$$path,format:"raw",requested_size:$$size,apparent_bytes:$$apparent_bytes}' \
			>"$$boot/host-block-create.json"; \
		if test "$$block_create_status" -ne 0 || test "$$block_apparent_bytes" -ne 1073741824; then \
			block_remove_status=0; rm -f "$$block_image" || block_remove_status=$$?; \
			block_absent=false; test ! -e "$$block_image" && block_absent=true; \
			block_cleanup_status=0; \
			if test "$$block_remove_status" -ne 0 || test "$$block_absent" != true; then \
				block_cleanup_status=1; \
			fi; \
			jq -n --argjson capture_status null --argjson remove_status "$$block_remove_status" \
				--argjson image_absent "$$block_absent" --argjson cleanup_status "$$block_cleanup_status" \
				'{capture_status:$$capture_status,remove_status:$$remove_status,image_absent:$$image_absent,cleanup_status:$$cleanup_status}' \
				>"$$boot/host-block-cleanup.json"; \
			failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
			$(call NAMEI_EXT_MARK_RUN_FAILED,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR),$$failed_at,host-block-create) || exit 1; \
			exit 1; \
		fi; \
		host_backing_status=0; \
		findmnt -rn -T "$$block_image" -o SOURCE,FSTYPE,OPTIONS,TARGET \
			>"$$boot/host-block-backing-filesystem.txt" || host_backing_status=$$?; \
		if test "$$host_backing_status" -eq 0; then \
			awk '$$2 == "ext4" { found = 1 } END { exit !found }' \
				"$$boot/host-block-backing-filesystem.txt" || host_backing_status=$$?; \
		fi; \
		if test "$$host_backing_status" -ne 0; then \
			block_remove_status=0; rm -f "$$block_image" || block_remove_status=$$?; \
			block_absent=false; test ! -e "$$block_image" && block_absent=true; \
			block_cleanup_status=0; \
			if test "$$block_remove_status" -ne 0 || test "$$block_absent" != true; then \
				block_cleanup_status=1; \
			fi; \
			jq -n --argjson capture_status null --argjson remove_status "$$block_remove_status" \
				--argjson image_absent "$$block_absent" --argjson cleanup_status "$$block_cleanup_status" \
				'{capture_status:$$capture_status,remove_status:$$remove_status,image_absent:$$image_absent,cleanup_status:$$cleanup_status}' \
				>"$$boot/host-block-cleanup.json"; \
			failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
			$(call NAMEI_EXT_MARK_RUN_FAILED,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR),$$failed_at,host-block-backing-filesystem) || exit 1; \
			exit 1; \
		fi; \
		capture_status=0; \
		$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
			__namei_ext_kvm_capture \
			RUN_ID="$(RUN_ID)" \
			NAMEI_EXT_KVM_CAPTURE_IMAGE="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/artifacts/kernel/bzImage" \
			NAMEI_EXT_KVM_CAPTURE_GUEST_TARGET="__kubernetes_configmap_quantitative_guest KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_INDEX=$$boot_index KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR=$${boot#$(ROOT_DIR)/} KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR=$${KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR#$(ROOT_DIR)/} KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS=$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS) KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES='$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES)'" \
			NAMEI_EXT_KVM_CAPTURE_BOOT_DIR="$$boot" \
			NAMEI_EXT_KVM_CAPTURE_RUN_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)" \
			NAMEI_EXT_KVM_CAPTURE_HOST_CPUS="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_CPUS)" \
			NAMEI_EXT_KVM_CAPTURE_BLOCK_IMAGE="$$block_image" \
			NAMEI_EXT_KVM_CAPTURE_DEFER_FAILURE_MARK=1 \
			NAMEI_EXT_KVM_CAPTURE_TIMEOUT="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_KVM_TIMEOUT)" || \
			capture_status=$$?; \
		block_remove_status=0; rm -f "$$block_image" || block_remove_status=$$?; \
		block_absent=false; test ! -e "$$block_image" && block_absent=true; \
		block_cleanup_status=0; \
		if test "$$block_remove_status" -ne 0 || test "$$block_absent" != true; then \
			block_cleanup_status=1; \
		fi; \
		jq -n --argjson capture_status "$$capture_status" \
			--argjson remove_status "$$block_remove_status" \
			--argjson image_absent "$$block_absent" \
			--argjson cleanup_status "$$block_cleanup_status" \
			'{capture_status:$$capture_status,remove_status:$$remove_status,image_absent:$$image_absent,cleanup_status:$$cleanup_status}' \
			>"$$boot/host-block-cleanup.json"; \
		failure=; \
		if test "$$capture_status" -ne 0; then \
			if test -s "$$boot/kvm-capture-failure.txt"; then \
				failure=$$(cat "$$boot/kvm-capture-failure.txt"); \
			else failure=kvm-capture-wrapper; fi; \
		elif test "$$block_cleanup_status" -ne 0; then failure=host-block-cleanup; fi; \
		if test -n "$$failure"; then \
			failed_at=$$(date -u +%Y-%m-%dT%H:%M:%SZ); \
			$(call NAMEI_EXT_MARK_RUN_FAILED,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR),$$failed_at,$$failure) || exit 1; \
			exit 1; \
		fi; \
	done
	cat /proc/stat >"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/host-proc-stat-after.txt"
	cat /proc/interrupts >"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/host-proc-interrupts-after.txt"

kubernetes-configmap-quantitative-finalize:
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES)"
	jq -e '.status == "running" and (.completed_at | not)' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/run.json" >/dev/null
	test -f "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/execution-started"; \
		test ! -e "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/finalization-started"; \
		: >"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/finalization-started"
	$(call NAMEI_EXT_MULTI_BOOT_COLLECT_OBSERVATIONS,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR),$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS))
	! jq -e 'select(.pass != true)' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/observations.jsonl" >/dev/null
	python3 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/artifacts/source/validate.py" \
		--run-dir "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)"
	$(call NAMEI_EXT_MULTI_BOOT_VALIDATE_BOOT_FILES,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR),$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_BOOTS),$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_FILES))
	for file in $(KUBERNETES_CONFIGMAP_QUANTITATIVE_HOST_FILES); do \
		test -s "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/$$file"; \
	done
	while IFS= read -r -d '' boot; do \
		jq -e '.status == "completed" and .inner_status == 0 and .inventory_after_status == 0 and .dmesg_status == 0' "$$boot/boot.json" >/dev/null; \
		jq -e '.status == "verified" and .expected_host_cpus == [4,5,6,7] and [.vcpus[].cpus_allowed] == [[4],[5],[6],[7]]' "$$boot/vcpu-affinity.json" >/dev/null; \
		test "$$(cat "$$boot/affinity-barrier.txt")" = "$$(jq -r '.verified_at' "$$boot/vcpu-affinity.json")"; \
		test "$$(cat "$$boot/mechanism.status")" = 0; \
		jq -e '.status == 0 and .format == "raw" and .requested_size == "1G" and .apparent_bytes == 1073741824' "$$boot/host-block-create.json" >/dev/null; \
		jq -e '.capture_status == 0 and .remove_status == 0 and .image_absent == true and .cleanup_status == 0' "$$boot/host-block-cleanup.json" >/dev/null; \
		jq -e '.unmount_status == 0 and .mount_lookup_status == 1 and .mountpoint_status == 32 and .root_remove_status == 0 and .root_absent == true and .cleanup_status == 0' "$$boot/guest-block-cleanup.json" >/dev/null; \
		awk '$$2 == "ext4" { found = 1 } END { exit !found }' "$$boot/host-block-backing-filesystem.txt"; \
		awk '$$3 == "disk" && $$4 == 1073741824 && $$7 == "namei_ext_w4" { found = 1 } END { exit !found }' "$$boot/guest-block-device.txt"; \
		awk '$$1 ~ /^\/dev\/vd/ && $$2 == "ext4" { options = "," $$3 ","; if (index(options, ",noatime,") && index(options, ",nosuid,") && index(options, ",nodev,")) found = 1 } END { exit !found }' "$$boot/ext4-filesystem.txt"; \
		grep -E 'type=ext2/ext3 magic=ef53|type=ext4 magic=ef53' "$$boot/ext4-statfs.txt" >/dev/null; \
		grep -E 'TYPE="ext4"' "$$boot/ext4-block-signature.txt" >/dev/null; \
		$(call KUBERNETES_CONFIGMAP_VALIDATE_EXTERNAL,$$boot,before); \
		$(call KUBERNETES_CONFIGMAP_VALIDATE_EXTERNAL,$$boot,after); \
		test "$$(find "$$boot/logs" -type f -name '*.stderr.log' -size +0c -print -quit | wc -l)" = 0; \
		test ! -s "$$boot/launcher.stderr.log"; \
	done < <(find "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/boots" \
		-mindepth 1 -maxdepth 1 -type d -print0 | LC_ALL=C sort -z)

kubernetes-configmap-quantitative-analyze:
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)"
	jq -e '.status == "running" and (.completed_at | not)' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/run.json" >/dev/null
	test -f "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/finalization-started"; \
		test ! -e "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/analysis-started"; \
		test ! -e "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/analysis"; \
		: >"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/analysis-started"
	python3 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)/artifacts/source/analyze.py" \
		--run-dir "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_DIR)"

__kubernetes_configmap_quantitative_guest: __namei_ext_guest_prepare
	inner_status=0; \
	$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
		__kubernetes_configmap_quantitative_guest_inner \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_INDEX="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_INDEX)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES)" || \
		inner_status=$$?; \
	printf '%s\n' "$$inner_status" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/guest-inner.status"; \
	inventory_after_status=0; \
	$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR),$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/bpftool,after) || \
		inventory_after_status=$$?; \
	printf '%s\n' "$$inventory_after_status" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/guest-inventory-after.status"; \
	dmesg_status=0; \
	dmesg >"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/dmesg.log" || dmesg_status=$$?; \
	if test "$$dmesg_status" -eq 0; then \
		$(call NAMEI_EXT_GUEST_ASSERT_DMESG_CLEAN,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/dmesg.log) || dmesg_status=$$?; \
	fi; \
	printf '%s\n' "$$dmesg_status" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/guest-dmesg.status"; \
	status=completed; \
	if test "$$inner_status" -ne 0 || test "$$inventory_after_status" -ne 0 || test "$$dmesg_status" -ne 0; then status=failed; fi; \
	jq -n --arg status "$$status" --argjson inner_status "$$inner_status" \
		--argjson inventory_after_status "$$inventory_after_status" \
		--argjson dmesg_status "$$dmesg_status" \
		--arg completed_at "$$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
		'{status:$$status,inner_status:$$inner_status,inventory_after_status:$$inventory_after_status,dmesg_status:$$dmesg_status,completed_at:$$completed_at}' \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/boot.json"; \
	test "$$status" = completed

__kubernetes_configmap_quantitative_guest_inner:
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_INDEX)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES)"
	test -x "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/configmap-consumer"
	test -x "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/atomic-writer-quantitative-driver"
	test -x "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/namei-ext-configmap-quantitative"
	test -r "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/kubernetes_configmap_publication.bpf.o"
	affinity_status=waiting; deadline=$$((SECONDS + 60)); \
	while test "$$SECONDS" -lt "$$deadline"; do \
		if jq -e '.schema == "namei_ext.vcpu_affinity.v1" and .status == "verified"' \
			"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/vcpu-affinity.json" >/dev/null 2>&1; then \
			affinity_status=verified; break; \
		fi; \
		sleep 0.1; \
	done; \
	test "$$affinity_status" = verified; \
	jq -r '.verified_at' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/vcpu-affinity.json" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/affinity-barrier.txt"
	$(call NAMEI_EXT_GUEST_CAPTURE_KERNEL_EVIDENCE,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR),$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/kernel/config,$(shell cat $(KERNEL_COMMIT_FILE)),$(shell sed -n 's/^#define UTS_RELEASE "\(.*\)"/\1/p' $(KERNEL_RELEASE_HEADER)))
	$(call NAMEI_EXT_GUEST_CAPTURE_EXTERNAL_INVENTORY,$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR),$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/bpftool,before)
	: >"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/observations.jsonl"
	install -d "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/logs"
	sample_root="/tmp/namei-ext-kubernetes-configmap-quantitative"; \
	mapfile -t block_devices < <(lsblk -b -dn -o PATH,TYPE,SIZE,SERIAL | \
		awk '$$2 == "disk" && $$3 == 1073741824 && $$4 == "namei_ext_w4" { print $$1 }'); \
	test "$${#block_devices[@]}" -eq 1; block_device="$${block_devices[0]}"; \
	lsblk -b -dn -o NAME,PATH,TYPE,SIZE,ROTA,RO,SERIAL,MODEL "$$block_device" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/guest-block-device.txt"; \
	mechanism_status=0; \
	$(MAKE) --no-print-directory -C "$(ROOT_DIR)" \
		__kubernetes_configmap_quantitative_guest_mechanism \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_INDEX="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_INDEX)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES)" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_DEVICE="$$block_device" \
		KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT="$$sample_root" || \
		mechanism_status=$$?; \
	printf '%s\n' "$$mechanism_status" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/mechanism.status"; \
	unmount_status=0; \
	if mountpoint -q "$$sample_root"; then \
		sync; umount "$$sample_root" || unmount_status=$$?; \
	fi; \
	mount_lookup_status=0; findmnt -rn "$$sample_root" >/dev/null || mount_lookup_status=$$?; \
	mountpoint_status=0; mountpoint -q "$$sample_root" || mountpoint_status=$$?; \
	root_remove_status=0; \
	if test -d "$$sample_root"; then \
		rmdir "$$sample_root" || root_remove_status=$$?; \
	fi; \
	root_absent=false; test ! -e "$$sample_root" && root_absent=true; \
	cleanup_status=0; \
	if test "$$unmount_status" -ne 0 || test "$$mount_lookup_status" -ne 1 || \
			test "$$mountpoint_status" -ne 32 || test "$$root_remove_status" -ne 0 || \
			test "$$root_absent" != true; then cleanup_status=1; fi; \
	jq -n --argjson unmount_status "$$unmount_status" \
		--argjson mount_lookup_status "$$mount_lookup_status" \
		--argjson mountpoint_status "$$mountpoint_status" \
		--argjson root_remove_status "$$root_remove_status" \
		--argjson root_absent "$$root_absent" \
		--argjson cleanup_status "$$cleanup_status" \
		'{unmount_status:$$unmount_status,mount_lookup_status:$$mount_lookup_status,mountpoint_status:$$mountpoint_status,root_remove_status:$$root_remove_status,root_absent:$$root_absent,cleanup_status:$$cleanup_status}' \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/guest-block-cleanup.json"; \
	if test "$$mechanism_status" -ne 0; then exit "$$mechanism_status"; fi; \
	test "$$cleanup_status" -eq 0

__kubernetes_configmap_quantitative_guest_mechanism:
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_DEVICE)"
	test -n "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)"
	test -b "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_DEVICE)"
	test ! -e "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)"
	mkfs.ext4 -q -F -m 0 -E lazy_itable_init=0,lazy_journal_init=0 \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_DEVICE)" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/mkfs-ext4.log" 2>&1
	blkid -p "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_DEVICE)" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/ext4-block-signature.txt"
	install -d "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)"
	mount -t ext4 -o noatime,nosuid,nodev \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BLOCK_DEVICE)" \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)"
	sync -f "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)"
	test "$$(findmnt -rn -T "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)" -o FSTYPE)" = ext4
	findmnt -rn -T "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)" \
		-o SOURCE,FSTYPE,OPTIONS,TARGET \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/ext4-filesystem.txt"
	stat -f -c 'type=%T magic=%t block_size=%S blocks=%b available=%a' \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)" \
		>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/ext4-statfs.txt"
	boot_index="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_INDEX)"; \
	runtime_uid="$$(stat -c %u "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)")"; \
	runtime_gid="$$(stat -c %g "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)")"; \
	test "$$runtime_uid" -gt 0; test "$$runtime_gid" -gt 0; \
	sample_root="$(KUBERNETES_CONFIGMAP_QUANTITATIVE_SAMPLE_ROOT)"; \
	scales=( $(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_SCALES) ); \
	scale_count="$${#scales[@]}"; \
	start_index=$$(( (boot_index - 1) % scale_count )); \
	run_condition() { \
		mechanism="$$1"; scale="$$2"; pair="$$3"; order="$$4"; \
		label="w$${scale}-p$${pair}-o$${order}-$${mechanism}"; \
		parent="$$sample_root/$$label"; \
		install -d "$$parent"; \
		chown "$$runtime_uid:$$runtime_gid" "$$parent"; \
		if test "$$mechanism" = atomicwriter; then \
			"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/atomic-writer-quantitative-driver" \
				timed \
				"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/observations.jsonl" \
				"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/configmap-consumer" \
				"$$parent" "$$scale" "$$boot_index" "$$pair" "$$order" \
				>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/logs/$$label.stdout.log" \
				2>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/logs/$$label.stderr.log"; \
		else \
			"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/namei-ext-configmap-quantitative" \
				"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/kubernetes_configmap_publication.bpf.o" \
				"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/configmap-consumer" \
				"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/observations.jsonl" \
				"$$parent" /sys/fs/cgroup "$$scale" "$$boot_index" "$$pair" "$$order" \
				>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/logs/$$label.stdout.log" \
				2>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/logs/$$label.stderr.log"; \
		fi; \
		rmdir "$$parent"; \
	}; \
	run_audit() { \
		scale="$$1"; pair="$$2"; label="w$${scale}-p$${pair}-atomicwriter-audit"; \
		parent="$$sample_root/$$label"; \
		install -d "$$parent"; chown "$$runtime_uid:$$runtime_gid" "$$parent"; \
		"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_RUN_DIR)/artifacts/runtime/atomic-writer-quantitative-driver" \
			audit "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/observations.jsonl" \
			"$$parent" "$$scale" "$$boot_index" "$$pair" \
			>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/logs/$$label.stdout.log" \
			2>"$(KUBERNETES_CONFIGMAP_QUANTITATIVE_BOOT_DIR)/logs/$$label.stderr.log"; \
		rmdir "$$parent"; \
	}; \
	for ((offset=0; offset<scale_count; offset++)); do \
		scale="$${scales[$$(( (start_index + offset) % scale_count ))]}"; \
		for pair in $$(seq 1 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS)"); do \
			if test $$(( (boot_index + pair) % 2 )) -eq 0; then first=atomicwriter; second=namei_ext; else first=namei_ext; second=atomicwriter; fi; \
			run_condition "$$first" "$$scale" "$$pair" 1; \
			run_condition "$$second" "$$scale" "$$pair" 2; \
			done; \
		done; \
	for scale in "$${scales[@]}"; do \
		for pair in $$(seq 1 "$(KUBERNETES_CONFIGMAP_QUANTITATIVE_ACTIVE_PAIRS)"); do \
			run_audit "$$scale" "$$pair"; \
		done; \
	done; \
	test -d "$$sample_root/lost+found"; \
		test "$$(find "$$sample_root/lost+found" -mindepth 1 -print -quit | wc -l)" = 0; \
		test "$$(find "$$sample_root" -mindepth 1 -maxdepth 1 ! -name lost+found -print -quit | wc -l)" = 0

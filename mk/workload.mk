include $(ROOT_DIR)/configs/benchmarks/workloads.mk
include $(ROOT_DIR)/configs/benchmarks/workload-sources.mk

WORKLOAD_CACHE_ROOT ?= $(CACHE_ROOT)/workloads
WORKLOAD_BUILD_ROOT ?= $(BUILD_ROOT)/workloads
WORKLOAD_PROVENANCE_ROOT ?= $(RESULT_ROOT)/workloads/provenance
WORKLOAD_RUNS_ROOT ?= $(RESULT_ROOT)/workloads/runs
WORKLOAD_RESULT_ROOT ?= $(WORKLOAD_PROVENANCE_ROOT)
WORKLOAD_RUN_ROOT ?= $(WORKLOAD_RUNS_ROOT)/$(RUN_ID)

REDIS_ARCHIVE := $(WORKLOAD_CACHE_ROOT)/$(REDIS_ARCHIVE_NAME)
REDIS_SRC := $(WORKLOAD_BUILD_ROOT)/$(REDIS_SOURCE_DIR_NAME)
REDIS_STAMP := $(REDIS_SRC)/.source.ok
REDIS_PROVENANCE := $(WORKLOAD_RESULT_ROOT)/redis-source.json

NGINX_ARCHIVE := $(WORKLOAD_CACHE_ROOT)/$(NGINX_ARCHIVE_NAME)
NGINX_SRC := $(WORKLOAD_BUILD_ROOT)/$(NGINX_SOURCE_DIR_NAME)
NGINX_STAMP := $(NGINX_SRC)/.source.ok
NGINX_PROVENANCE := $(WORKLOAD_RESULT_ROOT)/nginx-source.json

BAZEL_BINARY := $(WORKLOAD_CACHE_ROOT)/$(BAZEL_BINARY_NAME)

SANDBOXFS_ARCHIVE := $(WORKLOAD_CACHE_ROOT)/$(SANDBOXFS_ARCHIVE_NAME)
SANDBOXFS_WORK_ROOT := \
	$(WORKLOAD_BUILD_ROOT)/sandboxfs-$(SANDBOXFS_COMMIT_SHORT)
SANDBOXFS_SRC := \
	$(SANDBOXFS_WORK_ROOT)/$(SANDBOXFS_SOURCE_DIR_NAME)
SANDBOXFS_TARGET_DIR := $(SANDBOXFS_WORK_ROOT)/target
SANDBOXFS_BINARY := $(SANDBOXFS_TARGET_DIR)/release/sandboxfs
SANDBOXFS_EXTRACT_STAMP := $(SANDBOXFS_WORK_ROOT)/.extract.ok
SANDBOXFS_BUILD_STAMP := $(SANDBOXFS_WORK_ROOT)/.build.ok
SANDBOXFS_BUILD_RECORD_DIR := \
	$(WORKLOAD_PROVENANCE_ROOT)/build/sandboxfs-$(SANDBOXFS_COMMIT_SHORT)
SANDBOXFS_BUILD_LOG := $(SANDBOXFS_BUILD_RECORD_DIR)/build.log
SANDBOXFS_LDD := $(SANDBOXFS_BUILD_RECORD_DIR)/ldd.txt
SANDBOXFS_BUILD_PROVENANCE := $(SANDBOXFS_BUILD_RECORD_DIR)/build.json

DMTCP_ARCHIVE := $(WORKLOAD_CACHE_ROOT)/$(DMTCP_ARCHIVE_NAME)
DMTCP_WORK_ROOT := $(WORKLOAD_BUILD_ROOT)/dmtcp-$(DMTCP_COMMIT_SHORT)
DMTCP_SRC := $(DMTCP_WORK_ROOT)/$(DMTCP_SOURCE_DIR_NAME)
DMTCP_INSTALL_ROOT := $(DMTCP_WORK_ROOT)/install
DMTCP_EXTRACT_STAMP := $(DMTCP_WORK_ROOT)/.extract.ok
DMTCP_CONFIGURE_STAMP := $(DMTCP_WORK_ROOT)/.configure.ok
DMTCP_COMPILE_STAMP := $(DMTCP_WORK_ROOT)/.compile.ok
DMTCP_INSTALL_STAMP := $(DMTCP_WORK_ROOT)/.install.ok
DMTCP_BUILD_RECORD_DIR := $(WORKLOAD_PROVENANCE_ROOT)/build/dmtcp-$(DMTCP_COMMIT_SHORT)
DMTCP_CONFIGURE_LOG := $(DMTCP_BUILD_RECORD_DIR)/configure.log
DMTCP_BUILD_LOG := $(DMTCP_BUILD_RECORD_DIR)/build.log
DMTCP_INSTALL_LOG := $(DMTCP_BUILD_RECORD_DIR)/install.log
DMTCP_INSTALL_MANIFEST := $(DMTCP_BUILD_RECORD_DIR)/install-tree.sha256
DMTCP_BUILD_PROVENANCE := $(DMTCP_BUILD_RECORD_DIR)/build.json

SPINDLE_ARCHIVE := $(WORKLOAD_CACHE_ROOT)/$(SPINDLE_ARCHIVE_NAME)
SPINDLE_WORK_ROOT := \
	$(WORKLOAD_BUILD_ROOT)/spindle-$(SPINDLE_COMMIT_SHORT)
SPINDLE_SRC := $(SPINDLE_WORK_ROOT)/source
SPINDLE_BUILD_ROOT := $(SPINDLE_WORK_ROOT)/build
SPINDLE_INSTALL_ROOT := $(SPINDLE_WORK_ROOT)/prefix
SPINDLE_EXTRACT_STAMP := $(SPINDLE_WORK_ROOT)/.extract.ok
SPINDLE_CONFIGURE_STAMP := $(SPINDLE_WORK_ROOT)/.configure.ok
SPINDLE_COMPILE_STAMP := $(SPINDLE_WORK_ROOT)/.compile.ok
SPINDLE_INSTALL_STAMP := $(SPINDLE_WORK_ROOT)/.install.ok
SPINDLE_BUILD_RECORD_DIR := \
	$(WORKLOAD_PROVENANCE_ROOT)/build/spindle-$(SPINDLE_COMMIT_SHORT)
SPINDLE_CONFIGURE_LOG := $(SPINDLE_BUILD_RECORD_DIR)/configure.log
SPINDLE_BUILD_LOG := $(SPINDLE_BUILD_RECORD_DIR)/build.log
SPINDLE_INSTALL_LOG := $(SPINDLE_BUILD_RECORD_DIR)/install.log
SPINDLE_BUILD_PROVENANCE := $(SPINDLE_BUILD_RECORD_DIR)/build.json
SPINDLE_TEST_DIR := $(SPINDLE_BUILD_ROOT)/testsuite
SPINDLE_BINARY := $(SPINDLE_INSTALL_ROOT)/bin/spindle
SPINDLE_SOURCE_CHECK_DIR := \
	$(WORKLOAD_RUN_ROOT)/spindle-source-check

REDIS_BUILD_WORK_ROOT := $(WORKLOAD_BUILD_ROOT)/runs/$(RUN_ID)/w1-redis-build
REDIS_BUILD_SRC := $(REDIS_BUILD_WORK_ROOT)/src
REDIS_BUILD_STAMP := $(REDIS_BUILD_SRC)/.workload-source.ok
REDIS_BUILD_RESULT_DIR := $(WORKLOAD_RUN_ROOT)/w1-redis-build
REDIS_BUILD_LOG := $(REDIS_BUILD_RESULT_DIR)/build.log
REDIS_BUILD_JSON := $(REDIS_BUILD_RESULT_DIR)/build.json

NGINX_BUILD_WORK_ROOT := $(WORKLOAD_BUILD_ROOT)/runs/$(RUN_ID)/w1-nginx-build
NGINX_BUILD_SRC := $(NGINX_BUILD_WORK_ROOT)/src
NGINX_BUILD_PREFIX := $(NGINX_BUILD_WORK_ROOT)/install
NGINX_BUILD_STAMP := $(NGINX_BUILD_SRC)/.workload-source.ok
NGINX_BUILD_RESULT_DIR := $(WORKLOAD_RUN_ROOT)/w1-nginx-build
NGINX_CONFIGURE_LOG := $(NGINX_BUILD_RESULT_DIR)/configure.log
NGINX_BUILD_LOG := $(NGINX_BUILD_RESULT_DIR)/build.log
NGINX_BUILD_JSON := $(NGINX_BUILD_RESULT_DIR)/build.json

.PHONY: workload-redis-build workload-nginx-build workload-bazel \
	workload-sandboxfs-acquire workload-sandboxfs-build \
	workload-dmtcp-acquire workload-dmtcp-verify workload-dmtcp-extract \
	workload-dmtcp-configure workload-dmtcp-compile workload-dmtcp-install \
	workload-dmtcp-provenance workload-dmtcp-build \
	workload-spindle-acquire workload-spindle-verify workload-spindle-extract \
	workload-spindle-configure workload-spindle-compile \
	workload-spindle-install workload-spindle-provenance \
	workload-spindle-build workload-spindle-source-check

workload-redis-build: $(REDIS_BUILD_JSON)

workload-nginx-build: $(NGINX_BUILD_JSON)

workload-bazel: $(BAZEL_BINARY)
	test -x "$(BAZEL_BINARY)"
	test "$$("$(BAZEL_BINARY)" --version)" = "bazel $(BAZEL_VERSION)"

workload-sandboxfs-acquire: $(SANDBOXFS_ARCHIVE)
	test -s "$(SANDBOXFS_ARCHIVE)"

workload-sandboxfs-build: $(SANDBOXFS_BUILD_PROVENANCE)
	test -x "$(SANDBOXFS_BINARY)"
	test "$$("$(SANDBOXFS_BINARY)" --version)" = \
		"sandboxfs $(SANDBOXFS_VERSION)"
	test -s "$(SANDBOXFS_BUILD_LOG)"
	test -s "$(SANDBOXFS_LDD)"
	test "$$(pkg-config --modversion fuse)" = \
		"$(SANDBOXFS_LIBFUSE_VERSION)"
	jq -e \
		--arg commit "$(SANDBOXFS_COMMIT)" \
		--arg version "$(SANDBOXFS_VERSION)" \
		--arg rustc "$(SANDBOXFS_RUSTC_VERSION)" \
		--arg cargo "$(SANDBOXFS_CARGO_VERSION)" \
		--arg libfuse "$(SANDBOXFS_LIBFUSE_VERSION)" \
		'.schema == "namei_ext.workload_build_provenance.v2" and .project == "sandboxfs" and .version == $$version and .commit == $$commit and .build.toolchain.rustc == $$rustc and .build.toolchain.cargo == $$cargo and .build.toolchain.libfuse == $$libfuse and .build.binary.version == ("sandboxfs " + $$version)' \
		"$(SANDBOXFS_BUILD_PROVENANCE)" >/dev/null

workload-dmtcp-acquire: $(DMTCP_ARCHIVE)
	test -s "$(DMTCP_ARCHIVE)"

workload-dmtcp-verify: $(DMTCP_ARCHIVE)
	printf '%s  %s\n' "$(DMTCP_ARCHIVE_SHA256)" "$(DMTCP_ARCHIVE)" | sha256sum -c -

workload-dmtcp-extract: workload-dmtcp-verify $(DMTCP_EXTRACT_STAMP)
	test -x "$(DMTCP_SRC)/configure"
	test -f "$(DMTCP_SRC)/$(DMTCP_LICENSE_PATH)"
	test "$$(grep -F -c 'while (start_ptr - env_buf < count)' \
		"$(DMTCP_SRC)/src/dmtcpplugin.cpp")" = 1
	! grep -F 'while (start_ptr - env_buf < (int)sizeof(env_buf))' \
		"$(DMTCP_SRC)/src/dmtcpplugin.cpp"

workload-dmtcp-configure: $(DMTCP_CONFIGURE_STAMP)
	test -s "$(DMTCP_CONFIGURE_LOG)"
	test -f "$(DMTCP_SRC)/Makefile"

workload-dmtcp-compile: $(DMTCP_COMPILE_STAMP)
	test -s "$(DMTCP_BUILD_LOG)"
	test -x "$(DMTCP_SRC)/bin/dmtcp_launch"
	test -x "$(DMTCP_SRC)/bin/dmtcp_coordinator"
	test -x "$(DMTCP_SRC)/bin/dmtcp_command"
	test -x "$(DMTCP_SRC)/bin/dmtcp_restart"
	test -f "$(DMTCP_SRC)/lib/dmtcp/libdmtcp.so"

workload-dmtcp-install: $(DMTCP_INSTALL_STAMP)
	test -s "$(DMTCP_INSTALL_LOG)"
	test -s "$(DMTCP_INSTALL_MANIFEST)"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_launch"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_coordinator"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_command"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_restart"
	test -f "$(DMTCP_INSTALL_ROOT)/lib/dmtcp/libdmtcp.so"

workload-dmtcp-provenance: $(DMTCP_BUILD_PROVENANCE)
	printf '%s  %s\n' "$(DMTCP_RESTART_ENV_PATCH_SHA256)" \
		"$(DMTCP_RESTART_ENV_PATCH)" | sha256sum -c -
	jq -e \
		--arg commit "$(DMTCP_COMMIT)" \
		--arg archive_sha256 "$(DMTCP_ARCHIVE_SHA256)" \
		--arg patch_path "$(DMTCP_RESTART_ENV_PATCH)" \
		--arg patch_sha256 "$(DMTCP_RESTART_ENV_PATCH_SHA256)" \
		'.schema == "namei_ext.workload_build_provenance.v1" and .project == "dmtcp" and .commit == $$commit and .source.archive_sha256 == $$archive_sha256 and .source.patches == [{path:$$patch_path, expected_sha256:$$patch_sha256, sha256:$$patch_sha256, purpose:"fix dmtcp_get_restart_env flattened-environment scan bound"}] and (.source.pinned_files["src/dmtcpplugin.cpp"] | length == 64) and .install.file_count > 0' \
		"$(DMTCP_BUILD_PROVENANCE)" >/dev/null

workload-dmtcp-build: workload-dmtcp-verify workload-dmtcp-provenance
	test -s "$(DMTCP_CONFIGURE_LOG)"
	test -s "$(DMTCP_BUILD_LOG)"
	test -s "$(DMTCP_INSTALL_LOG)"
	test -s "$(DMTCP_INSTALL_MANIFEST)"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_launch"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_coordinator"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_command"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_restart"
	test -f "$(DMTCP_INSTALL_ROOT)/lib/dmtcp/libdmtcp.so"
	(cd "$(DMTCP_INSTALL_ROOT)" && sha256sum -c "$(DMTCP_INSTALL_MANIFEST)")

workload-spindle-acquire: $(SPINDLE_ARCHIVE)
	test -s "$(SPINDLE_ARCHIVE)"

workload-spindle-verify: $(SPINDLE_ARCHIVE)
	test "$$(tar -tzf "$(SPINDLE_ARCHIVE)" | \
		grep -Fc '/testsuite/test_driver.c')" = 1
	test "$$(tar -tzf "$(SPINDLE_ARCHIVE)" | \
		grep -Fc '/src/server/cache/global_name.c')" = 1

workload-spindle-extract: workload-spindle-verify $(SPINDLE_EXTRACT_STAMP)
	test -x "$(SPINDLE_SRC)/configure"
	test -f "$(SPINDLE_SRC)/$(SPINDLE_LICENSE_PATH)"
	grep -F 'libtls20.so' "$(SPINDLE_SRC)/testsuite/Makefile.am"
	grep -F './run_driver --dlopen --pull' \
		"$(SPINDLE_SRC)/testsuite/runTests_template"

workload-spindle-configure: $(SPINDLE_CONFIGURE_STAMP)
	test -s "$(SPINDLE_CONFIGURE_LOG)"
	test -f "$(SPINDLE_BUILD_ROOT)/Makefile"
	test -f "$(SPINDLE_TEST_DIR)/Makefile"

workload-spindle-compile: $(SPINDLE_COMPILE_STAMP)
	test -s "$(SPINDLE_BUILD_LOG)"
	test -x "$(SPINDLE_TEST_DIR)/run_driver"
	test -x "$(SPINDLE_TEST_DIR)/test_driver"
	test -f "$(SPINDLE_TEST_DIR)/libtest10.so"
	test -f "$(SPINDLE_TEST_DIR)/libtls20.so"
	test -f "$(SPINDLE_TEST_DIR)/origin_dir/origin_subdir/liborigintarget.so"

workload-spindle-install: $(SPINDLE_INSTALL_STAMP)
	test -s "$(SPINDLE_INSTALL_LOG)"
	test -x "$(SPINDLE_BINARY)"

workload-spindle-provenance: $(SPINDLE_BUILD_PROVENANCE)
	jq -e \
		--arg commit "$(SPINDLE_COMMIT)" \
		'.schema == "namei_ext.workload_build_provenance.v1" and .project == "Spindle" and .commit == $$commit and .build.security == "none" and .build.resource_manager == "serial" and .install.file_count > 0' \
		"$(SPINDLE_BUILD_PROVENANCE)" >/dev/null

workload-spindle-build: workload-spindle-provenance
	test -x "$(SPINDLE_BINARY)"
	test -x "$(SPINDLE_TEST_DIR)/test_driver"
	test -x "$(SPINDLE_INSTALL_ROOT)/libexec/spindle/spindle_be"
	test -x "$(SPINDLE_INSTALL_ROOT)/libexec/spindle/spindle_bootstrap"

workload-spindle-source-check: workload-spindle-build
	$(call NAMEI_EXT_RESULT_ROOT_CREATE,$(SPINDLE_SOURCE_CHECK_DIR))
	rm -rf /tmp/namei-ext-spindle-cache \
		/tmp/namei-ext-spindle-comm /tmp/namei-ext-spindle-tmp
	install -d /tmp/namei-ext-spindle-cache \
		/tmp/namei-ext-spindle-comm /tmp/namei-ext-spindle-tmp
	rm -f "$(SPINDLE_TEST_DIR)"/spindle_output.* \
		"$(SPINDLE_TEST_DIR)"/spindle_test*
	printf '%s\n' \
		'$(SPINDLE_BINARY) --level=high --launcher=serial --pull --noclean=yes --strip=no $(SPINDLE_TEST_DIR)/test_driver --dlopen --pull --nompi' \
		>"$(SPINDLE_SOURCE_CHECK_DIR)/command.txt"
	(cd "$(SPINDLE_TEST_DIR)" && \
		timeout 180 env -i \
			HOME="$(HOME)" USER="$$(id -un)" LOGNAME="$$(id -un)" \
			SHELL=/bin/sh \
			PATH="/usr/bin:/bin:$(SPINDLE_TEST_DIR)" \
			LD_LIBRARY_PATH="$(SPINDLE_TEST_DIR)" \
			SPINDLE_TEST=1 SPINDLE_DEBUG=3 \
			TMPDIR=/tmp/namei-ext-spindle-tmp \
			LC_ALL=C LANG=C TZ=UTC \
			"$(SPINDLE_BINARY)" --level=high \
				--launcher=serial --pull --noclean=yes --strip=no \
				"$(SPINDLE_TEST_DIR)/test_driver" \
				--dlopen --pull --nompi) \
		>"$(SPINDLE_SOURCE_CHECK_DIR)/stdout.log" \
		2>"$(SPINDLE_SOURCE_CHECK_DIR)/stderr.log"
	test ! -s "$(SPINDLE_SOURCE_CHECK_DIR)/stderr.log"
	find "$(SPINDLE_TEST_DIR)" -maxdepth 1 -type f \
		\( -name 'spindle_output.*' -o -name 'spindle_test*' \) \
		-print -exec cp -a {} "$(SPINDLE_SOURCE_CHECK_DIR)/" \;
	find /tmp/namei-ext-spindle-cache -xdev -printf '%y %p %s\n' \
		| LC_ALL=C sort >"$(SPINDLE_SOURCE_CHECK_DIR)/cache-tree.txt"
	test -s "$(SPINDLE_SOURCE_CHECK_DIR)/cache-tree.txt"
	rg -l 'Adding .*spindlens-file' \
		"$(SPINDLE_SOURCE_CHECK_DIR)"/spindle_output.* \
		>"$(SPINDLE_SOURCE_CHECK_DIR)/mapping-logs.txt"
	test -s "$(SPINDLE_SOURCE_CHECK_DIR)/mapping-logs.txt"

$(WORKLOAD_CACHE_ROOT) $(WORKLOAD_BUILD_ROOT) $(WORKLOAD_RESULT_ROOT):
	install -d "$@"

$(WORKLOAD_RUN_ROOT) $(REDIS_BUILD_RESULT_DIR) $(NGINX_BUILD_RESULT_DIR) \
$(DMTCP_BUILD_RECORD_DIR) $(SANDBOXFS_BUILD_RECORD_DIR) \
$(SPINDLE_BUILD_RECORD_DIR) $(SPINDLE_SOURCE_CHECK_DIR):
	install -d "$@"

$(REDIS_ARCHIVE): | $(WORKLOAD_CACHE_ROOT)
	curl -fL --retry 3 --connect-timeout 30 -o "$@.tmp" "$(REDIS_URL)"
	mv -f "$@.tmp" "$@"
	printf '%s  %s\n' "$(REDIS_ARCHIVE_SHA256)" "$@" | sha256sum -c -

$(NGINX_ARCHIVE): | $(WORKLOAD_CACHE_ROOT)
	curl -fL --retry 3 --connect-timeout 30 -o "$@.tmp" "$(NGINX_URL)"
	mv -f "$@.tmp" "$@"
	printf '%s  %s\n' "$(NGINX_ARCHIVE_SHA256)" "$@" | sha256sum -c -

$(BAZEL_BINARY): | $(WORKLOAD_CACHE_ROOT)
	curl -fL --retry 3 --connect-timeout 30 -o "$@.tmp" "$(BAZEL_URL)"
	chmod 0755 "$@.tmp"
	test "$$("$@.tmp" --version)" = "bazel $(BAZEL_VERSION)"
	mv -f "$@.tmp" "$@"

$(SANDBOXFS_ARCHIVE): | $(WORKLOAD_CACHE_ROOT)
	curl -fL --retry 3 --connect-timeout 30 -o "$@.tmp" "$(SANDBOXFS_URL)"
	test "$$(tar -tzf "$@.tmp" | grep -Fc '/Cargo.toml')" = 1
	test "$$(tar -tzf "$@.tmp" | grep -Fc '/LICENSE')" = 1
	mv -f "$@.tmp" "$@"

$(SPINDLE_ARCHIVE): | $(WORKLOAD_CACHE_ROOT)
	curl -fL --retry 3 --connect-timeout 30 -o "$@.tmp" "$(SPINDLE_URL)"
	test -s "$@.tmp"
	test "$$(tar -tzf "$@.tmp" | \
		grep -Fc '/testsuite/test_driver.c')" = 1
	mv -f "$@.tmp" "$@"

$(SPINDLE_EXTRACT_STAMP): $(SPINDLE_ARCHIVE) \
$(ROOT_DIR)/configs/benchmarks/workload-sources.mk | $(WORKLOAD_BUILD_ROOT)
	rm -rf "$(SPINDLE_WORK_ROOT)"
	install -d "$(SPINDLE_WORK_ROOT)/source"
	tar -xzf "$(SPINDLE_ARCHIVE)" -C "$(SPINDLE_SRC)" \
		--strip-components=1
	test -x "$(SPINDLE_SRC)/configure"
	test -f "$(SPINDLE_SRC)/$(SPINDLE_LICENSE_PATH)"
	touch "$@"

$(SPINDLE_CONFIGURE_STAMP): $(SPINDLE_EXTRACT_STAMP) | \
$(SPINDLE_BUILD_RECORD_DIR)
	rm -rf "$(SPINDLE_BUILD_ROOT)" "$(SPINDLE_INSTALL_ROOT)"
	install -d "$(SPINDLE_BUILD_ROOT)" "$(SPINDLE_INSTALL_ROOT)"
	(cd "$(SPINDLE_BUILD_ROOT)" && \
		"$(SPINDLE_SRC)/configure" \
			--prefix="$(SPINDLE_INSTALL_ROOT)" \
			--enable-sec-none \
			--with-rm=serial \
			--with-testrm=serial \
			--with-cachepaths=/tmp/namei-ext-spindle-cache \
			--with-commpath=/tmp/namei-ext-spindle-comm \
			CFLAGS="-O2 -g" CXXFLAGS="-O2 -g") \
		>"$(SPINDLE_CONFIGURE_LOG).tmp" 2>&1
	test -s "$(SPINDLE_CONFIGURE_LOG).tmp"
	mv -f "$(SPINDLE_CONFIGURE_LOG).tmp" "$(SPINDLE_CONFIGURE_LOG)"
	test -f "$(SPINDLE_BUILD_ROOT)/Makefile"
	touch "$@"

$(SPINDLE_COMPILE_STAMP): $(SPINDLE_CONFIGURE_STAMP)
	$(MAKE) -C "$(SPINDLE_BUILD_ROOT)" -j"$(JOBS)" \
		>"$(SPINDLE_BUILD_LOG).tmp" 2>&1
	test -s "$(SPINDLE_BUILD_LOG).tmp"
	mv -f "$(SPINDLE_BUILD_LOG).tmp" "$(SPINDLE_BUILD_LOG)"
	test -x "$(SPINDLE_TEST_DIR)/test_driver"
	test "$$(file -b "$(SPINDLE_TEST_DIR)/test_driver" | \
		grep -c '^ELF 64-bit')" = 1
	test -f "$(SPINDLE_TEST_DIR)/libtls20.so"
	touch "$@"

$(SPINDLE_INSTALL_STAMP): $(SPINDLE_COMPILE_STAMP)
	$(MAKE) -C "$(SPINDLE_BUILD_ROOT)" install \
		>"$(SPINDLE_INSTALL_LOG).tmp" 2>&1
	test -s "$(SPINDLE_INSTALL_LOG).tmp"
	mv -f "$(SPINDLE_INSTALL_LOG).tmp" "$(SPINDLE_INSTALL_LOG)"
	test -x "$(SPINDLE_BINARY)"
	touch "$@"

$(SPINDLE_BUILD_PROVENANCE): $(SPINDLE_INSTALL_STAMP) \
$(ROOT_DIR)/mk/workload.mk | \
$(SPINDLE_BUILD_RECORD_DIR)
	jq -n \
		--arg schema "namei_ext.workload_build_provenance.v1" \
		--arg project "Spindle" \
		--arg commit "$(SPINDLE_COMMIT)" \
		--arg url "$(SPINDLE_URL)" \
		--arg archive "$(SPINDLE_ARCHIVE)" \
		--arg source_dir "$(SPINDLE_SRC)" \
		--arg license "$(SPINDLE_LICENSE)" \
		--arg license_path "$(SPINDLE_LICENSE_PATH)" \
		--arg build_dir "$(SPINDLE_BUILD_ROOT)" \
		--arg prefix "$(SPINDLE_INSTALL_ROOT)" \
		--arg configure_log "$(SPINDLE_CONFIGURE_LOG)" \
		--arg build_log "$(SPINDLE_BUILD_LOG)" \
		--arg install_log "$(SPINDLE_INSTALL_LOG)" \
		--arg spindle "$(SPINDLE_BINARY)" \
		--arg test_elf "$(SPINDLE_TEST_DIR)/test_driver" \
		--argjson install_file_count "$$(find "$(SPINDLE_INSTALL_ROOT)" -type f | wc -l)" \
		'{schema:$$schema,project:$$project,commit:$$commit,source:{url:$$url,archive:$$archive,source_dir:$$source_dir,license:{spdx:$$license,path:$$license_path}},build:{build_dir:$$build_dir,prefix:$$prefix,security:"none",resource_manager:"serial",cachepaths:"/tmp/namei-ext-spindle-cache",commpath:"/tmp/namei-ext-spindle-comm",configure_log:$$configure_log,build_log:$$build_log,install_log:$$install_log},install:{root:$$prefix,file_count:$$install_file_count,artifacts:{spindle:$$spindle,test_driver_elf:$$test_elf}}}' \
		>"$@.tmp"
	jq -e \
		--arg commit "$(SPINDLE_COMMIT)" \
		'.commit == $$commit and .install.file_count > 0' \
		"$@.tmp" >/dev/null
	mv -f "$@.tmp" "$@"

$(SANDBOXFS_EXTRACT_STAMP): $(SANDBOXFS_ARCHIVE) \
$(SANDBOXFS_CARGO_LOCK) | $(WORKLOAD_BUILD_ROOT)
	rm -rf "$(SANDBOXFS_WORK_ROOT)"
	install -d "$(SANDBOXFS_WORK_ROOT)"
	tar -xzf "$(SANDBOXFS_ARCHIVE)" -C "$(SANDBOXFS_WORK_ROOT)"
	test -f "$(SANDBOXFS_SRC)/Cargo.toml"
	test -f "$(SANDBOXFS_SRC)/LICENSE"
	test ! -e "$(SANDBOXFS_SRC)/Cargo.lock"
	install -m 0444 "$(SANDBOXFS_CARGO_LOCK)" \
		"$(SANDBOXFS_SRC)/Cargo.lock"
	grep -Fx 'version = "$(SANDBOXFS_VERSION)"' \
		"$(SANDBOXFS_SRC)/Cargo.toml"
	touch "$@"

$(SANDBOXFS_BUILD_STAMP): $(SANDBOXFS_EXTRACT_STAMP) \
$(ROOT_DIR)/configs/benchmarks/workload-sources.mk \
$(ROOT_DIR)/mk/workload.mk | $(SANDBOXFS_BUILD_RECORD_DIR)
	command -v cargo >/dev/null
	command -v rustc >/dev/null
	command -v pkg-config >/dev/null
	test "$$(cargo --version)" = "$(SANDBOXFS_CARGO_VERSION)"
	test "$$(rustc --version)" = "$(SANDBOXFS_RUSTC_VERSION)"
	test "$$(pkg-config --modversion fuse)" = \
		"$(SANDBOXFS_LIBFUSE_VERSION)"
	CARGO_TARGET_DIR="$(SANDBOXFS_TARGET_DIR)" \
		cargo build --release --locked \
		--manifest-path "$(SANDBOXFS_SRC)/Cargo.toml" \
		>"$(SANDBOXFS_BUILD_LOG).tmp" 2>&1
	test -s "$(SANDBOXFS_BUILD_LOG).tmp"
	mv -f "$(SANDBOXFS_BUILD_LOG).tmp" "$(SANDBOXFS_BUILD_LOG)"
	test -x "$(SANDBOXFS_BINARY)"
	test "$$("$(SANDBOXFS_BINARY)" --version)" = \
		"sandboxfs $(SANDBOXFS_VERSION)"
	ldd "$(SANDBOXFS_BINARY)" >"$(SANDBOXFS_LDD).tmp"
	grep -F 'libfuse.so.2' "$(SANDBOXFS_LDD).tmp"
	mv -f "$(SANDBOXFS_LDD).tmp" "$(SANDBOXFS_LDD)"
	touch "$@"

$(SANDBOXFS_BUILD_PROVENANCE): $(SANDBOXFS_BUILD_STAMP) | \
$(SANDBOXFS_BUILD_RECORD_DIR)
	jq -n \
		--arg schema "namei_ext.workload_build_provenance.v2" \
		--arg project "sandboxfs" \
		--arg version "$(SANDBOXFS_VERSION)" \
		--arg commit "$(SANDBOXFS_COMMIT)" \
		--arg url "$(SANDBOXFS_URL)" \
		--arg archive "$(SANDBOXFS_ARCHIVE)" \
		--arg cargo_lock "$(SANDBOXFS_CARGO_LOCK)" \
		--arg cargo_version "$$(cargo --version)" \
		--arg rustc_version "$$(rustc --version)" \
		--arg libfuse_version "$$(pkg-config --modversion fuse)" \
		--arg libfuse_runtime "$(SANDBOXFS_LIBFUSE_RUNTIME)" \
		--arg binary "$(SANDBOXFS_BINARY)" \
		--arg binary_version "$$("$(SANDBOXFS_BINARY)" --version)" \
		--arg build_log "$(SANDBOXFS_BUILD_LOG)" \
		--arg ldd "$(SANDBOXFS_LDD)" \
		'{schema:$$schema,project:$$project,version:$$version,commit:$$commit,source:{url:$$url,archive:$$archive,cargo_lock:$$cargo_lock},build:{command:["cargo","build","--release","--locked"],toolchain:{cargo:$$cargo_version,rustc:$$rustc_version,libfuse:$$libfuse_version,libfuse_runtime:$$libfuse_runtime},log:$$build_log,ldd:$$ldd,binary:{path:$$binary,version:$$binary_version}}}' \
		>"$@.tmp"
	jq -e \
		--arg commit "$(SANDBOXFS_COMMIT)" \
		--arg version "$(SANDBOXFS_VERSION)" \
		--arg rustc "$(SANDBOXFS_RUSTC_VERSION)" \
		--arg cargo "$(SANDBOXFS_CARGO_VERSION)" \
		--arg libfuse "$(SANDBOXFS_LIBFUSE_VERSION)" \
		'.schema == "namei_ext.workload_build_provenance.v2" and .project == "sandboxfs" and .version == $$version and .commit == $$commit and .build.toolchain.rustc == $$rustc and .build.toolchain.cargo == $$cargo and .build.toolchain.libfuse == $$libfuse and .build.binary.version == ("sandboxfs " + $$version)' \
		"$@.tmp" >/dev/null
	mv -f "$@.tmp" "$@"

$(DMTCP_ARCHIVE): | $(WORKLOAD_CACHE_ROOT)
	curl -fL --retry 3 --connect-timeout 30 -o "$@.tmp" "$(DMTCP_URL)"
	printf '%s  %s\n' "$(DMTCP_ARCHIVE_SHA256)" "$@.tmp" | sha256sum -c -
	mv -f "$@.tmp" "$@"

$(DMTCP_EXTRACT_STAMP): $(DMTCP_ARCHIVE) $(DMTCP_RESTART_ENV_PATCH) | $(WORKLOAD_BUILD_ROOT)
	rm -rf "$(DMTCP_WORK_ROOT)"
	install -d "$(DMTCP_WORK_ROOT)"
	printf '%s  %s\n' "$(DMTCP_ARCHIVE_SHA256)" "$(DMTCP_ARCHIVE)" | sha256sum -c -
	printf '%s  %s\n' "$(DMTCP_RESTART_ENV_PATCH_SHA256)" \
		"$(DMTCP_RESTART_ENV_PATCH)" | sha256sum -c -
	tar -xzf "$(DMTCP_ARCHIVE)" -C "$(DMTCP_WORK_ROOT)"
	test -d "$(DMTCP_SRC)"
	test -x "$(DMTCP_SRC)/configure"
	test -f "$(DMTCP_SRC)/$(DMTCP_LICENSE_PATH)"
	test -f "$(DMTCP_SRC)/src/dmtcpplugin.cpp"
	test -f "$(DMTCP_SRC)/src/plugin_pathtranslator.cpp"
	test -f "$(DMTCP_SRC)/test/pathvirt1.c"
	test -f "$(DMTCP_SRC)/test/autotest.py"
	patch --batch --forward --fuzz=0 -d "$(DMTCP_SRC)" -p1 \
		<"$(DMTCP_RESTART_ENV_PATCH)"
	test "$$(grep -F -c 'while (start_ptr - env_buf < count)' \
		"$(DMTCP_SRC)/src/dmtcpplugin.cpp")" = 1
	! grep -F 'while (start_ptr - env_buf < (int)sizeof(env_buf))' \
		"$(DMTCP_SRC)/src/dmtcpplugin.cpp"
	touch "$@"

$(DMTCP_CONFIGURE_STAMP): $(DMTCP_EXTRACT_STAMP) | $(DMTCP_BUILD_RECORD_DIR)
	command -v cc >/dev/null
	command -v c++ >/dev/null
	command -v make >/dev/null
	cd "$(DMTCP_SRC)" && \
		./configure --prefix="$(DMTCP_INSTALL_ROOT)" >"$(DMTCP_CONFIGURE_LOG)" 2>&1
	test -s "$(DMTCP_CONFIGURE_LOG)"
	test -f "$(DMTCP_SRC)/Makefile"
	touch "$@"

$(DMTCP_COMPILE_STAMP): $(DMTCP_CONFIGURE_STAMP) | $(DMTCP_BUILD_RECORD_DIR)
	$(MAKE) -C "$(DMTCP_SRC)" -j"$(JOBS)" >"$(DMTCP_BUILD_LOG)" 2>&1
	test -s "$(DMTCP_BUILD_LOG)"
	test -x "$(DMTCP_SRC)/bin/dmtcp_launch"
	test -x "$(DMTCP_SRC)/bin/dmtcp_coordinator"
	test -x "$(DMTCP_SRC)/bin/dmtcp_command"
	test -x "$(DMTCP_SRC)/bin/dmtcp_restart"
	test -f "$(DMTCP_SRC)/lib/dmtcp/libdmtcp.so"
	touch "$@"

$(DMTCP_INSTALL_STAMP): $(DMTCP_COMPILE_STAMP) | $(DMTCP_BUILD_RECORD_DIR)
	rm -rf "$(DMTCP_INSTALL_ROOT)"
	install -d "$(DMTCP_INSTALL_ROOT)"
	$(MAKE) -C "$(DMTCP_SRC)" install >"$(DMTCP_INSTALL_LOG)" 2>&1
	test -s "$(DMTCP_INSTALL_LOG)"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_launch"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_coordinator"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_command"
	test -x "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_restart"
	test -f "$(DMTCP_INSTALL_ROOT)/lib/dmtcp/libdmtcp.so"
	cd "$(DMTCP_INSTALL_ROOT)" && \
		find . -type f -print0 | sort -z | xargs -0 sha256sum >"$(DMTCP_INSTALL_MANIFEST).tmp"
	test -s "$(DMTCP_INSTALL_MANIFEST).tmp"
	mv -f "$(DMTCP_INSTALL_MANIFEST).tmp" "$(DMTCP_INSTALL_MANIFEST)"
	touch "$@"

$(DMTCP_BUILD_PROVENANCE): $(DMTCP_INSTALL_STAMP) | $(DMTCP_BUILD_RECORD_DIR)
	printf '%s  %s\n' "$(DMTCP_ARCHIVE_SHA256)" "$(DMTCP_ARCHIVE)" | sha256sum -c -
	jq -n \
		--arg schema "namei_ext.workload_build_provenance.v1" \
		--arg project "dmtcp" \
		--arg commit "$(DMTCP_COMMIT)" \
		--arg commit_short "$(DMTCP_COMMIT_SHORT)" \
		--arg url "$(DMTCP_URL)" \
		--arg archive "$(DMTCP_ARCHIVE)" \
		--arg expected_archive_sha256 "$(DMTCP_ARCHIVE_SHA256)" \
		--arg archive_sha256 "$$(sha256sum "$(DMTCP_ARCHIVE)" | awk '{print $$1}')" \
		--arg source_dir "$(DMTCP_SRC)" \
		--arg install_dir "$(DMTCP_INSTALL_ROOT)" \
		--arg license "$(DMTCP_LICENSE)" \
		--arg license_path "$(DMTCP_LICENSE_PATH)" \
		--arg license_sha256 "$$(sha256sum "$(DMTCP_SRC)/$(DMTCP_LICENSE_PATH)" | awk '{print $$1}')" \
		--arg restart_env_patch "$(DMTCP_RESTART_ENV_PATCH)" \
		--arg expected_restart_env_patch_sha256 "$(DMTCP_RESTART_ENV_PATCH_SHA256)" \
		--arg restart_env_patch_sha256 "$$(sha256sum "$(DMTCP_RESTART_ENV_PATCH)" | awk '{print $$1}')" \
		--arg dmtcpplugin_sha256 "$$(sha256sum "$(DMTCP_SRC)/src/dmtcpplugin.cpp" | awk '{print $$1}')" \
		--arg pathtranslator_sha256 "$$(sha256sum "$(DMTCP_SRC)/src/plugin_pathtranslator.cpp" | awk '{print $$1}')" \
		--arg pathvirt1_sha256 "$$(sha256sum "$(DMTCP_SRC)/test/pathvirt1.c" | awk '{print $$1}')" \
		--arg autotest_sha256 "$$(sha256sum "$(DMTCP_SRC)/test/autotest.py" | awk '{print $$1}')" \
		--arg configure_log "$(DMTCP_CONFIGURE_LOG)" \
		--arg configure_log_sha256 "$$(sha256sum "$(DMTCP_CONFIGURE_LOG)" | awk '{print $$1}')" \
		--arg build_log "$(DMTCP_BUILD_LOG)" \
		--arg build_log_sha256 "$$(sha256sum "$(DMTCP_BUILD_LOG)" | awk '{print $$1}')" \
		--arg install_log "$(DMTCP_INSTALL_LOG)" \
		--arg install_log_sha256 "$$(sha256sum "$(DMTCP_INSTALL_LOG)" | awk '{print $$1}')" \
		--arg install_manifest "$(DMTCP_INSTALL_MANIFEST)" \
		--arg install_manifest_sha256 "$$(sha256sum "$(DMTCP_INSTALL_MANIFEST)" | awk '{print $$1}')" \
		--arg cc_version "$$(cc --version | sed -n '1p')" \
		--arg cxx_version "$$(c++ --version | sed -n '1p')" \
		--arg make_version "$$(make --version | sed -n '1p')" \
		--arg jobs "$(JOBS)" \
		--arg launch_sha256 "$$(sha256sum "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_launch" | awk '{print $$1}')" \
		--arg coordinator_sha256 "$$(sha256sum "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_coordinator" | awk '{print $$1}')" \
		--arg command_sha256 "$$(sha256sum "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_command" | awk '{print $$1}')" \
		--arg restart_sha256 "$$(sha256sum "$(DMTCP_INSTALL_ROOT)/bin/dmtcp_restart" | awk '{print $$1}')" \
		--arg libdmtcp_sha256 "$$(sha256sum "$(DMTCP_INSTALL_ROOT)/lib/dmtcp/libdmtcp.so" | awk '{print $$1}')" \
		--argjson install_file_count "$$(find "$(DMTCP_INSTALL_ROOT)" -type f | wc -l)" \
		'{schema:$$schema, project:$$project, commit:$$commit, commit_short:$$commit_short, source:{url:$$url, archive:$$archive, expected_archive_sha256:$$expected_archive_sha256, archive_sha256:$$archive_sha256, source_dir:$$source_dir, license:{spdx:$$license, path:$$license_path, sha256:$$license_sha256}, patches:[{path:$$restart_env_patch, expected_sha256:$$expected_restart_env_patch_sha256, sha256:$$restart_env_patch_sha256, purpose:"fix dmtcp_get_restart_env flattened-environment scan bound"}], pinned_files:{"src/dmtcpplugin.cpp":$$dmtcpplugin_sha256, "src/plugin_pathtranslator.cpp":$$pathtranslator_sha256, "test/pathvirt1.c":$$pathvirt1_sha256, "test/autotest.py":$$autotest_sha256}}, build:{configure_command:["./configure", ("--prefix=" + $$install_dir)], make_jobs:$$jobs, toolchain:{cc:$$cc_version, cxx:$$cxx_version, make:$$make_version}, logs:{configure:{path:$$configure_log, sha256:$$configure_log_sha256}, build:{path:$$build_log, sha256:$$build_log_sha256}, install:{path:$$install_log, sha256:$$install_log_sha256}}}, install:{root:$$install_dir, file_count:$$install_file_count, manifest:{path:$$install_manifest, sha256:$$install_manifest_sha256}, artifacts:{dmtcp_launch:$$launch_sha256, dmtcp_coordinator:$$coordinator_sha256, dmtcp_command:$$command_sha256, dmtcp_restart:$$restart_sha256, "lib/dmtcp/libdmtcp.so":$$libdmtcp_sha256}}}' \
		>"$@.tmp"
	jq -e \
		--arg commit "$(DMTCP_COMMIT)" \
		--arg archive_sha256 "$(DMTCP_ARCHIVE_SHA256)" \
		--arg patch_path "$(DMTCP_RESTART_ENV_PATCH)" \
		--arg patch_sha256 "$(DMTCP_RESTART_ENV_PATCH_SHA256)" \
		'.schema == "namei_ext.workload_build_provenance.v1" and .commit == $$commit and .source.archive_sha256 == $$archive_sha256 and .source.patches == [{path:$$patch_path, expected_sha256:$$patch_sha256, sha256:$$patch_sha256, purpose:"fix dmtcp_get_restart_env flattened-environment scan bound"}] and (.source.pinned_files["src/dmtcpplugin.cpp"] | length == 64) and .install.file_count > 0' \
		"$@.tmp" >/dev/null
	mv -f "$@.tmp" "$@"

$(REDIS_STAMP): $(REDIS_ARCHIVE) | $(WORKLOAD_BUILD_ROOT)
	rm -rf "$(REDIS_SRC)"
	printf '%s  %s\n' "$(REDIS_ARCHIVE_SHA256)" "$(REDIS_ARCHIVE)" | sha256sum -c -
	tar -xzf "$(REDIS_ARCHIVE)" -C "$(WORKLOAD_BUILD_ROOT)"
	test -f "$(REDIS_SRC)/Makefile"
	test -f "$(REDIS_SRC)/src/Makefile"
	test -f "$(REDIS_SRC)/$(REDIS_LICENSE_PATH)"
	touch "$@"

$(NGINX_STAMP): $(NGINX_ARCHIVE) | $(WORKLOAD_BUILD_ROOT)
	rm -rf "$(NGINX_SRC)"
	printf '%s  %s\n' "$(NGINX_ARCHIVE_SHA256)" "$(NGINX_ARCHIVE)" | sha256sum -c -
	tar -xzf "$(NGINX_ARCHIVE)" -C "$(WORKLOAD_BUILD_ROOT)"
	test -f "$(NGINX_SRC)/configure"
	test -f "$(NGINX_SRC)/conf/nginx.conf"
	test -f "$(NGINX_SRC)/$(NGINX_LICENSE_PATH)"
	touch "$@"

$(REDIS_BUILD_STAMP): $(REDIS_ARCHIVE) | $(WORKLOAD_BUILD_ROOT)
	rm -rf "$(REDIS_BUILD_WORK_ROOT)"
	install -d "$(REDIS_BUILD_SRC)"
	printf '%s  %s\n' "$(REDIS_ARCHIVE_SHA256)" "$(REDIS_ARCHIVE)" | sha256sum -c -
	tar -xzf "$(REDIS_ARCHIVE)" -C "$(REDIS_BUILD_SRC)" --strip-components=1
	test -f "$(REDIS_BUILD_SRC)/Makefile"
	test -f "$(REDIS_BUILD_SRC)/src/Makefile"
	touch "$@"

$(NGINX_BUILD_STAMP): $(NGINX_ARCHIVE) | $(WORKLOAD_BUILD_ROOT)
	rm -rf "$(NGINX_BUILD_WORK_ROOT)"
	install -d "$(NGINX_BUILD_SRC)" "$(NGINX_BUILD_PREFIX)"
	printf '%s  %s\n' "$(NGINX_ARCHIVE_SHA256)" "$(NGINX_ARCHIVE)" | sha256sum -c -
	tar -xzf "$(NGINX_ARCHIVE)" -C "$(NGINX_BUILD_SRC)" --strip-components=1
	test -f "$(NGINX_BUILD_SRC)/configure"
	test -f "$(NGINX_BUILD_SRC)/conf/nginx.conf"
	touch "$@"

$(REDIS_PROVENANCE): $(REDIS_STAMP) | $(WORKLOAD_RESULT_ROOT)
	jq -n \
		--arg workload_ids "w1-redis-build,w3-redis-podman-criu,w4-ccache-redis-nginx" \
		--arg project "redis" \
		--arg version "$(REDIS_VERSION)" \
		--arg commit "$(REDIS_COMMIT)" \
		--arg url "$(REDIS_URL)" \
		--arg archive "$(REDIS_ARCHIVE)" \
		--arg expected_archive_sha256 "$(REDIS_ARCHIVE_SHA256)" \
		--arg archive_sha256 "$$(sha256sum "$(REDIS_ARCHIVE)" | awk '{print $$1}')" \
		--arg source_dir "$(REDIS_SRC)" \
		--arg license "$(REDIS_LICENSE_PATH)" \
		--arg license_sha256 "$$(sha256sum "$(REDIS_SRC)/$(REDIS_LICENSE_PATH)" | awk '{print $$1}')" \
		--arg makefile_sha256 "$$(sha256sum "$(REDIS_SRC)/Makefile" | awk '{print $$1}')" \
		--arg src_makefile_sha256 "$$(sha256sum "$(REDIS_SRC)/src/Makefile" | awk '{print $$1}')" \
		'{schema:"namei_ext.workload_source.v1", project:$$project, workload_ids:($$workload_ids | split(",")), version:$$version, commit:$$commit, url:$$url, archive:$$archive, expected_archive_sha256:$$expected_archive_sha256, archive_sha256:$$archive_sha256, source_dir:$$source_dir, license:{path:$$license, sha256:$$license_sha256}, evidence:{makefile_sha256:$$makefile_sha256, src_makefile_sha256:$$src_makefile_sha256}}' \
		>"$@"

$(NGINX_PROVENANCE): $(NGINX_STAMP) | $(WORKLOAD_RESULT_ROOT)
	jq -n \
		--arg workload_ids "w1-nginx-build,w2-nginx-fixture,w3-nginx-podman-criu,w4-ccache-redis-nginx" \
		--arg project "nginx" \
		--arg version "$(NGINX_VERSION)" \
		--arg url "$(NGINX_URL)" \
		--arg archive "$(NGINX_ARCHIVE)" \
		--arg expected_archive_sha256 "$(NGINX_ARCHIVE_SHA256)" \
		--arg archive_sha256 "$$(sha256sum "$(NGINX_ARCHIVE)" | awk '{print $$1}')" \
		--arg source_dir "$(NGINX_SRC)" \
		--arg license "$(NGINX_LICENSE_PATH)" \
		--arg license_sha256 "$$(sha256sum "$(NGINX_SRC)/$(NGINX_LICENSE_PATH)" | awk '{print $$1}')" \
		--arg configure_sha256 "$$(sha256sum "$(NGINX_SRC)/configure" | awk '{print $$1}')" \
		--arg conf_sha256 "$$(sha256sum "$(NGINX_SRC)/conf/nginx.conf" | awk '{print $$1}')" \
		'{schema:"namei_ext.workload_source.v1", project:$$project, workload_ids:($$workload_ids | split(",")), version:$$version, url:$$url, archive:$$archive, expected_archive_sha256:$$expected_archive_sha256, archive_sha256:$$archive_sha256, source_dir:$$source_dir, license:{path:$$license, sha256:$$license_sha256}, evidence:{configure_sha256:$$configure_sha256, sample_config_sha256:$$conf_sha256}}' \
		>"$@"

$(REDIS_BUILD_JSON): $(REDIS_BUILD_STAMP) $(REDIS_PROVENANCE) | $(REDIS_BUILD_RESULT_DIR)
	command -v cc >/dev/null
	start_ns=$$(date +%s%N); \
	GIT_CEILING_DIRECTORIES="$(REDIS_BUILD_SRC)" \
		$(MAKE) -C "$(REDIS_BUILD_SRC)" -j"$(JOBS)" BUILD_TLS=no MALLOC=libc redis-server >"$(REDIS_BUILD_LOG)" 2>&1; \
	end_ns=$$(date +%s%N); \
	test -x "$(REDIS_BUILD_SRC)/src/redis-server"; \
	jq -n \
		--arg schema "namei_ext.real_workload_build.v1" \
		--arg run_id "$(RUN_ID)" \
		--arg workload_id "w1-redis-build" \
		--arg policy_family "build_graph_view.bpf.c" \
		--arg project "redis" \
		--arg version "$(REDIS_VERSION)" \
		--arg jobs "$(JOBS)" \
		--arg git_ceiling_directories "$(REDIS_BUILD_SRC)" \
		--arg cc_version "$$(cc --version | sed -n '1p')" \
		--arg source_dir "$(REDIS_BUILD_SRC)" \
		--arg log "$(REDIS_BUILD_LOG)" \
		--arg binary "$(REDIS_BUILD_SRC)/src/redis-server" \
		--arg binary_sha256 "$$(sha256sum "$(REDIS_BUILD_SRC)/src/redis-server" | awk '{print $$1}')" \
		--arg stdout_sha256 "$$(sha256sum "$(REDIS_BUILD_LOG)" | awk '{print $$1}')" \
		--argjson duration_ns "$$((end_ns - start_ns))" \
		'{schema:$$schema, run_id:$$run_id, workload_id:$$workload_id, policy_family:$$policy_family, result_level:"host_source_build_trace", run_environment:"host", policy_executed:false, kvm_validated:false, output_hash_oracle:false, project:$$project, version:$$version, source_dir:$$source_dir, command:["make","redis-server"], make_variables:{BUILD_TLS:"no", MALLOC:"libc", JOBS:$$jobs}, environment:{GIT_CEILING_DIRECTORIES:$$git_ceiling_directories}, toolchain:{cc_version:$$cc_version}, duration_ns:$$duration_ns, artifacts:{log:$$log, log_sha256:$$stdout_sha256, binary:$$binary, binary_sha256:$$binary_sha256}}' \
		>"$@"

$(NGINX_BUILD_JSON): $(NGINX_BUILD_STAMP) $(NGINX_PROVENANCE) | $(NGINX_BUILD_RESULT_DIR)
	command -v cc >/dev/null
	start_ns=$$(date +%s%N); \
	cd "$(NGINX_BUILD_SRC)" && ./configure --prefix="$(NGINX_BUILD_PREFIX)" --with-cc=cc --without-http_rewrite_module --without-http_gzip_module >"$(NGINX_CONFIGURE_LOG)" 2>&1; \
	$(MAKE) -C "$(NGINX_BUILD_SRC)" -j"$(JOBS)" >"$(NGINX_BUILD_LOG)" 2>&1; \
	end_ns=$$(date +%s%N); \
	test -x "$(NGINX_BUILD_SRC)/objs/nginx"; \
	jq -n \
		--arg schema "namei_ext.real_workload_build.v1" \
		--arg run_id "$(RUN_ID)" \
		--arg workload_id "w1-nginx-build" \
		--arg policy_family "build_graph_view.bpf.c" \
		--arg project "nginx" \
		--arg version "$(NGINX_VERSION)" \
		--arg jobs "$(JOBS)" \
		--arg cc_version "$$(cc --version | sed -n '1p')" \
		--arg source_dir "$(NGINX_BUILD_SRC)" \
		--arg configure_log "$(NGINX_CONFIGURE_LOG)" \
		--arg configure_log_sha256 "$$(sha256sum "$(NGINX_CONFIGURE_LOG)" | awk '{print $$1}')" \
		--arg build_log "$(NGINX_BUILD_LOG)" \
		--arg build_log_sha256 "$$(sha256sum "$(NGINX_BUILD_LOG)" | awk '{print $$1}')" \
		--arg binary "$(NGINX_BUILD_SRC)/objs/nginx" \
		--arg binary_sha256 "$$(sha256sum "$(NGINX_BUILD_SRC)/objs/nginx" | awk '{print $$1}')" \
		--argjson duration_ns "$$((end_ns - start_ns))" \
		'{schema:$$schema, run_id:$$run_id, workload_id:$$workload_id, policy_family:$$policy_family, result_level:"host_source_build_trace", run_environment:"host", policy_executed:false, kvm_validated:false, output_hash_oracle:false, project:$$project, version:$$version, source_dir:$$source_dir, command:["./configure","make"], configure_flags:["--without-http_rewrite_module","--without-http_gzip_module"], make_variables:{JOBS:$$jobs}, toolchain:{cc_version:$$cc_version}, duration_ns:$$duration_ns, artifacts:{configure_log:$$configure_log, configure_log_sha256:$$configure_log_sha256, build_log:$$build_log, build_log_sha256:$$build_log_sha256, binary:$$binary, binary_sha256:$$binary_sha256}}' \
		>"$@"

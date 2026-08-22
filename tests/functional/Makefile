ROOT_DIR ?= $(abspath ../..)
BUILD_ROOT ?= $(ROOT_DIR)/.build
OUTPUT ?= $(BUILD_ROOT)/functional
KERNEL_DIR ?= $(ROOT_DIR)/kernel
LIBBPF_OUTPUT ?= $(BUILD_ROOT)/libbpf
LIBBPF_A ?= $(LIBBPF_OUTPUT)/libbpf.a
LIBBPF_SOURCES := $(wildcard $(KERNEL_DIR)/tools/lib/bpf/*.[ch] $(KERNEL_DIR)/tools/lib/bpf/Makefile)
HARNESS_LIBRARY ?= $(BUILD_ROOT)/runner/libnamei_ext_harness.a
HARNESS_SOURCES := $(ROOT_DIR)/runner/src/namei_ext_harness.c \
	$(ROOT_DIR)/runner/include/namei_ext_harness.h
CC ?= cc

.PHONY: all clean

all: $(OUTPUT)/namei_ext_functional

$(OUTPUT):
	install -d "$@"

$(LIBBPF_OUTPUT):
	install -d "$@"

$(LIBBPF_A): $(LIBBPF_SOURCES) | $(LIBBPF_OUTPUT)
	$(MAKE) -C "$(KERNEL_DIR)/tools/lib/bpf" OUTPUT="$(LIBBPF_OUTPUT)/" "$(LIBBPF_A)"

$(HARNESS_LIBRARY): $(HARNESS_SOURCES)
	$(MAKE) -C "$(ROOT_DIR)/runner" \
		ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

$(OUTPUT)/namei_ext_functional: namei_ext_functional.c \
		$(HARNESS_LIBRARY) $(LIBBPF_A) | $(OUTPUT)
	$(CC) -g -O2 -Wall -Wextra \
		-I"$(ROOT_DIR)/runner/include" \
		-I"$(KERNEL_DIR)/tools/lib" \
		-I"$(KERNEL_DIR)/tools/include" \
		-I"$(KERNEL_DIR)/tools/include/uapi" \
		-I"$(LIBBPF_OUTPUT)" \
		-o "$@" "$<" "$(HARNESS_LIBRARY)" "$(LIBBPF_A)" -lelf -lz

clean:
	rm -rf "$(OUTPUT)"

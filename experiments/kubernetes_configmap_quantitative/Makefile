ROOT_DIR ?= $(abspath ../..)
BUILD_ROOT ?= $(ROOT_DIR)/.build
OUTPUT ?= $(BUILD_ROOT)/kubernetes-configmap-quantitative
KERNEL_DIR ?= $(ROOT_DIR)/kernel
LIBBPF_OUTPUT ?= $(BUILD_ROOT)/libbpf
LIBBPF_A ?= $(LIBBPF_OUTPUT)/libbpf.a
LIBBPF_SOURCES := $(wildcard $(KERNEL_DIR)/tools/lib/bpf/*.[ch] \
	$(KERNEL_DIR)/tools/lib/bpf/Makefile)
HARNESS_LIBRARY ?= $(BUILD_ROOT)/runner/libnamei_ext_harness.a
HARNESS_SOURCES := $(ROOT_DIR)/runner/src/namei_ext_harness.c \
	$(ROOT_DIR)/runner/include/namei_ext_harness.h
CC ?= cc
CFLAGS ?= -g -O2 -Wall -Wextra -Werror

.PHONY: all clean

all: $(OUTPUT)/configmap-consumer \
	$(OUTPUT)/namei-ext-configmap-quantitative

$(OUTPUT):
	install -d "$@"

$(LIBBPF_OUTPUT):
	install -d "$@"

$(LIBBPF_A): $(LIBBPF_SOURCES) | $(LIBBPF_OUTPUT)
	$(MAKE) -C "$(KERNEL_DIR)/tools/lib/bpf" \
		OUTPUT="$(LIBBPF_OUTPUT)/" "$(LIBBPF_A)"

$(HARNESS_LIBRARY): $(HARNESS_SOURCES)
	$(MAKE) -C "$(ROOT_DIR)/runner" \
		ROOT_DIR="$(ROOT_DIR)" BUILD_ROOT="$(BUILD_ROOT)" all

$(OUTPUT)/configmap-consumer: configmap_consumer.c | $(OUTPUT)
	$(CC) $(CFLAGS) -o "$@" "$<"

$(OUTPUT)/namei-ext-configmap-quantitative: \
		namei_ext_configmap_quantitative.c \
		$(HARNESS_LIBRARY) $(LIBBPF_A) | $(OUTPUT)
	$(CC) $(CFLAGS) \
		-I"$(ROOT_DIR)/runner/include" \
		-I"$(KERNEL_DIR)/tools/lib" \
		-I"$(KERNEL_DIR)/tools/include" \
		-I"$(KERNEL_DIR)/tools/include/uapi" \
		-I"$(LIBBPF_OUTPUT)" \
		-o "$@" "$<" "$(HARNESS_LIBRARY)" "$(LIBBPF_A)" -lelf -lz

clean:
	rm -rf "$(OUTPUT)"

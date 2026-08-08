CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror -std=gnu11
BUILD_DIR ?= build

.PHONY: all clean

all: $(BUILD_DIR)/view_holder

$(BUILD_DIR)/view_holder: view_holder.c
	install -d $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)

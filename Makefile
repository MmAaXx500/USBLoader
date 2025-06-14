NASM    = nasm
OBJCOPY = objcopy

TEST_CC = $(CC)
TEST_LD = $(CC)

BASE_NASMFLAGS = -f elf
NASMDEPFLAGS   = -MT $@ -MD -MP -MF $(@:.o=.d)

BASE_CFLAGS = -m32 -march=i486 -mtune=i686 -ffreestanding -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -Wconversion
TEST_BASE_CFLAGS = -m32 -g  -Wall -Wextra -Werror -Wconversion
CDEPFLAGS   = -MT $@ -MMD -MP -MF $(@:.o=.d)

BASE_LDFLAGS = -m32 -nostdlib -lgcc -no-pie -T linker.ld -L$(BUILD) -Wl,-Map=$(BUILD)/usbloader.map,--no-warn-rwx-segments,--build-id=none
TEST_BASE_LDFLAGS = -m32 -g -L$(BUILD)

BUILD  := build
SRCS   := stage1.asm stage2_entry.asm stage2.c print.c pci21.c pit.c mem.c
OBJS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).o)
DEPS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).d)
TARGET := $(BUILD)/usbloader.bin
TEST_TARGETS :=

# Create new test target
# Parameters
#   1: target name
#   2: souce file list
define test_target
TEST_SRCS_$(1) := $(2)
TEST_OBJS_$(1) := $$(foreach f,$$(TEST_SRCS_$(1)),$(BUILD)/$$(basename $$(notdir $(1).$$(f))).o)
TEST_DEPS_$(1) := $$(foreach f,$$(TEST_SRCS_$(1)),$(BUILD)/$$(basename $$(notdir $(1).$$(f))).d)

TEST_TARGETS += $(1)

$(1): $$(BUILD)/$(1)

$$(BUILD)/$(1): $$(TEST_OBJS_$(1))
	$$(TEST_LD) $$(TEST_BASE_LDFLAGS) $$(TEST_LDFLAGS) -o $$@ $$(TEST_OBJS_$(1))

$$(BUILD)/$(1).%.o: %.c $$(BUILD)/$(1).%.d | $$(BUILD)
	$$(TEST_CC) $$(TEST_BASE_CFLAGS) $$(TEST_CFLAGS) $$(CDEPFLAGS) -c $$< -o $$@

$$(TEST_DEPS_$(1)):

include $$(wildcard $$(TEST_DEPS_$(1)))
endef

all: usbloader tests

usbloader: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(BASE_LDFLAGS) $(LDFLAGS) -o $(BUILD)/usbloader.elf
	$(OBJCOPY) -O binary $(BUILD)/usbloader.elf $@

$(BUILD)/%.o: %.asm $(BUILD)/%.d | $(BUILD)
# NASM produces a dep (.d) file that is newer than the .o
# and that causes unnecessary reassembly every time.
# `touch`-ing the result after assembly to update the modification time
	$(NASM) $(BASE_NASMFLAGS) $(NASMFLAGS) $(NASMDEPFLAGS) $< -o $@
	touch $@

$(BUILD)/%.o: %.c $(BUILD)/%.d | $(BUILD)
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(CDEPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

$(DEPS):

include $(wildcard $(DEPS))

$(eval $(call test_target,test_mem,unity.c mem_test.c mem.c))

tests: $(TEST_TARGETS)

.PHONY: all clean

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

SANITIZER_FLAGS := -fsanitize=address,pointer-compare,pointer-subtract,leak,undefined -fno-sanitize=alignment

BUILD  := build
TARGET := $(BUILD)/usbloader.bin
TARGET_IMG := $(BUILD)/usbloader.img
TEST_TARGETS :=

ifeq ($(TEST_SAN), true)
	TEST_BASE_CFLAGS += $(SANITIZER_FLAGS)
	TEST_BASE_LDFLAGS += $(SANITIZER_FLAGS)
endif

# Create new test target
# Parameters
#   1: target name
#   2: souce file list
define test_target
TEST_BUILD_$(1) := $$(BUILD)/$(1).build
TEST_SRCS_$(1) := $(2)
TEST_OBJS_$(1) := $$(foreach f,$$(TEST_SRCS_$(1)),$$(TEST_BUILD_$(1))/$$(basename $$(f)).o)
TEST_DEPS_$(1) := $$(foreach f,$$(TEST_SRCS_$(1)),$$(TEST_BUILD_$(1))/$$(basename $$(f)).d)

TEST_TARGETS += $(1)

$(1): $$(BUILD)/$(1)

$$(BUILD)/$(1): $$(TEST_OBJS_$(1))
	$$(TEST_LD) $$(TEST_BASE_LDFLAGS) $$(TEST_LDFLAGS) -o $$@ $$(TEST_OBJS_$(1))

$$(TEST_BUILD_$(1))/%.o: %.c $$(TEST_BUILD_$(1))/%.d
	@mkdir -p $$(@D)
	$$(TEST_CC) $$(TEST_BASE_CFLAGS) -I. $$(TEST_CFLAGS) $$(CDEPFLAGS) -c $$< -o $$@

$$(TEST_DEPS_$(1)):

include $$(wildcard $$(TEST_DEPS_$(1)))
endef

all: usbloader usbloader.img tests

usbloader: $(TARGET)

usbloader.img: $(TARGET_IMG)

$(TARGET_IMG): $(TARGET)
	dd if=/dev/zero of=$(TARGET_IMG) bs=512 count=2880
	dd if=$(TARGET) of=$(TARGET_IMG) bs=512 conv=notrunc

$(BUILD)/%.o: %.asm $(BUILD)/%.d 
# NASM produces a dep (.d) file that is newer than the .o
# and that causes unnecessary reassembly every time.
# `touch`-ing the result after assembly to update the modification time
	@mkdir -p $(@D)
	$(NASM) $(BASE_NASMFLAGS) -I$(<D) $(NASMFLAGS) $(NASMDEPFLAGS) $< -o $@
	touch $@

$(BUILD)/%.o: %.c $(BUILD)/%.d
	@mkdir -p $(@D)
	$(CC) $(BASE_CFLAGS) -I. $(CFLAGS) $(CDEPFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

include arch/module.mk
include boot/module.mk
include drivers/module.mk
include mem/module.mk
include utils/module.mk

OBJS := $(foreach f,$(SRCS), $(BUILD)/$(basename $(f)).o)
DEPS := $(foreach f,$(SRCS), $(BUILD)/$(basename $(f)).d)

$(TARGET): $(OBJS)
	$(CC) $^ $(BASE_LDFLAGS) $(LDFLAGS) -o $(BUILD)/usbloader.elf
	$(OBJCOPY) -O binary $(BUILD)/usbloader.elf $@

$(DEPS):
include $(wildcard $(DEPS))

tests: $(TEST_TARGETS)

.PHONY: all clean usbloader usbloader.img tests

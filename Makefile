NASM    = nasm
OBJCOPY = objcopy

BASE_NASMFLAGS = -f elf
NASMDEPFLAGS   = -MT $@ -MD -MP -MF $(BUILD)/$*.d

BASE_CFLAGS = -m32 -march=i486 -mtune=i686 -ffreestanding -nostdlib -fno-pic -fno-stack-protector -Wall -Wextra -Werror -Wconversion
CDEPFLAGS   = -MT $@ -MMD -MP -MF $(BUILD)/$*.d

BASE_LDFLAGS = -m elf_i386 -T linker.ld --no-warn-rwx-segments -Map=$(BUILD)/usbloader.map -L$(BUILD)

BUILD  := build
SRCS   := stage1.asm stage2_entry.asm stage2.c print.c pci21.c pit.c uhci.c
OBJS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).o)
DEPS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).d)
TARGET := $(BUILD)/usbloader.bin

# === Rules ===
all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(BASE_LDFLAGS) $(LDFLAGS) -o $(BUILD)/usbloader.elf $(OBJS)
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

.PHONY: all clean

$(DEPS):

include $(wildcard $(DEPS))

NASM    = nasm
OBJCOPY = objcopy

NASMFLAGS    = -f elf
NASMDEPFLAGS = -MT $@ -MD -MP -MF $(BUILD)/$*.d

CFLAGS    = -m32 -ffreestanding -nostdlib -fno-pic -Wall -Wextra
CDEPFLAGS = -MT $@ -MMD -MP -MF $(BUILD)/$*.d

LDFLAGS = -m elf_i386 -T linker.ld --no-warn-rwx-segments -Map=$(BUILD)/usbloader.map -L$(BUILD)

BUILD  := build
SRCS   := stage1.asm stage2_entry.asm stage2.c
OBJS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).o)
DEPS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).d)
TARGET := $(BUILD)/usbloader.bin

# === Rules ===
all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $(BUILD)/usbloader.elf
	$(OBJCOPY) -O binary $(BUILD)/usbloader.elf $@

$(BUILD)/%.o: %.asm $(BUILD)/%.d | $(BUILD)
# NASM produces a dep (.d) file that is newer than the .o
# and that causes unnecessary reassembly every time.
# `touch`-ing the result after assembly to update the modification time
	$(NASM) $(NASMFLAGS) $(NASMDEPFLAGS) $< -o $@
	touch $@

$(BUILD)/%.o: %.c $(BUILD)/%.d | $(BUILD)
	$(CC) $(CFLAGS) $(CDEPFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean

$(DEPS):

include $(wildcard $(DEPS))

NASM    = nasm
OBJCOPY = objcopy

BASE_NASMFLAGS = -f elf
NASMDEPFLAGS   = -MT $@ -MD -MP -MF $(BUILD)/$*.d

BASE_CFLAGS = -m32 -march=i486 -mtune=i686 -ffreestanding -fno-pic -fno-stack-protector -Wall -Wextra -Werror -Wconversion
CDEPFLAGS   = -MT $@ -MMD -MP -MF $(BUILD)/$*.d

BASE_LDFLAGS = -m32 -nostdlib -lgcc -no-pie -T linker.ld -L$(BUILD) -Wl,-Map=$(BUILD)/usbloader.map,--no-warn-rwx-segments,--build-id=none

BUILD  := build
SRCS   := stage1.asm stage2_entry.asm stage2.c print.c pci21.c pit.c mem.c
OBJS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).o)
DEPS   := $(foreach f,$(SRCS), $(BUILD)/$(basename $(notdir $(f))).d)
TARGET := $(BUILD)/usbloader.bin

all: $(TARGET)

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

.PHONY: all clean

$(DEPS):

include $(wildcard $(DEPS))

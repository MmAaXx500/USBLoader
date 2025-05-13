all: usbloader.bin

usbloader.bin: stage1.o stage2_entry.o stage2.o linker.ld
	ld -m elf_i386 -T linker.ld --no-warn-rwx-segments -Map=usbloader.map stage1.o stage2_entry.o stage2.o -o usbloader.elf
	objcopy -O binary usbloader.elf usbloader.bin

stage1.o: stage1.asm
	nasm -f elf stage1.asm -o stage1.o

stage2_entry.o: stage2_entry.asm stage2.c
	nasm -f elf stage2_entry.asm -o stage2_entry.o

stage2.o: stage2.c
	gcc -m32 -ffreestanding -nostdlib -fno-pic -c stage2.c -o stage2.o

clean:
	rm -f *.o *.bin *.elf *.map

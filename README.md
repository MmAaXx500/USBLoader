# USBLoader

USBLoader is an experimental bootloader that allows booting from USB on PCs that do not support it natively (in theory, see [Status](#Status)). This nicely coincides with PCs that have a floppy drive, so USBLoader is designed to be bootable from a floppy.

## Goal

Successfully boot from a USB device on an Asus CUSL2-C motherboard.

## Status

Currently, most of the functionality is not implemented, so booting from USB is not possible.

So far:

 - Boots into 32 bit protected mode.
 - Prints PCI device information
 - Hangs

## Usage

What you need:
 - NASM
 - GCC
 - Make

If you have all of it, then build the image with

```
make
```

This will create the `build/usbloader.bin` file that you can write to a floppy and boot it.

```
dd if=build/usbloader.bin of=/dev/fdX
```

## Emulator (Bochs)

For a minimal Bochs config you need the following in your `bochsrc` file.

```
floppya: 1_44=build/usbloader.bin, status=inserted
boot: floppy
```

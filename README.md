# USBLoader

USBLoader is an experimental bootloader that allows booting from USB on PCs that do not support it natively (in theory, see [Status](#Status)). This nicely coincides with PCs that have a floppy drive, so USBLoader is designed to be bootable from a floppy.

## Goal

Successfully boot from a USB device on an Asus CUSL2-C motherboard.

## Status

Currently, most of the functionality is not implemented, so booting from USB is not possible.

So far:

 - Boots into 32 bit protected mode.
 - Prints nice messages both on-screen and serial port
 - Hangs

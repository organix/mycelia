# Mycelia

A bare-metal actor operating system for Raspberry Pi.

## Build and run instructions

_**NOTE**: HEAD is in active development, for a stable version the latest release is recommended._

If you are building on the RPi, just type:

    $ make clean all

If you can't compile (or cross-compile) from source,
you can use the pre-built `kernel.img` file.

Next, copy the firmware and kernel to a blank SD card, for example:

    $ cp firmware/* /media/<SD-card>/
    $ cp kernel.img /media/<SD-card>/

The end state for the SD card is to have a FAT32 filesystem on it with the following files:

    bootcode.bin
    start.elf
    kernel.img

Put the prepared SD card into the RPi,
connect the USB-to-Serial cable
(see [RPi Serial Connection](http://elinux.org/RPi_Serial_Connection) for more details),
and power-up to the console.

To get to the console, you'll need to connect. Here are two ways to try:

    $ minicom -b 115200 -o -D <device>

Where `<device>` is something like `/dev/ttyUSB0` or similar
(wherever you plugged in your USB-to-Serial cable).

Alternatively, if `minicom` is not working for you, try using `screen`:

    $ screen <device> 115200

Where `<device>` is, again, something like `/dev/ttyUSB0`.

The console will be waiting for an input, press `<ENTER>`. You should then see:

    mycelia <version> sp=0x00008000

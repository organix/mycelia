#
# Makefile for mycelia
#
# Copyright 2014 Dale Schumacher, Tristan Slominski
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

AS=	as
CC=	gcc -g -Wall -O2 -nostdlib -nostartfiles -ffreestanding
LD=	ld

KOBJS=	start.o mycelia.o raspberry.o timer.o serial.o xmodem.o

all: kernel.img

start.o: start.s
	$(AS) start.s -o start.o

mycelia.o: mycelia.s
	$(AS) mycelia.s -o mycelia.o

kernel.img: loadmap $(KOBJS)
	$(LD) $(KOBJS) -T loadmap -o mycelia.elf
	objdump -D mycelia.elf > mycelia.list
	objcopy mycelia.elf -O ihex mycelia.hex
	objcopy --only-keep-debug mycelia.elf kernel.sym
	objcopy mycelia.elf -O binary kernel.img

.c.o:
	$(CC) -c $<

clean:
	rm -f *.o
	rm -f *.bin
	rm -f *.hex
	rm -f *.elf
	rm -f *.list
	rm -f *.img
	rm -f *~ core


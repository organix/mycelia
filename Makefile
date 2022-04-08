#
# Makefile for mycelia
#
# Copyright 2014-2022 Dale Schumacher, Tristan Slominski
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

COPTS=	-nostdlib -nostartfiles -ffreestanding -ffixed-sl -ffixed-fp -ffixed-ip
CFLAGS=	$(COPTS) -g -Wall -O2
#CFLAGS=	$(COPTS) -g -Wall

AS=	as
CC=	gcc $(CFLAGS)
LD=	ld

KOBJS=	start.o \
	mycelia.o \
	idiom.o \
	console.o \
	shutt.o \
	pred.o \
	number.o \
	tools.o \
	sexpr.o \
	cal.o \
	bose.o \
	timer.o \
	serial.o \
	raspberry.o \
	xmodem.o

all: kernel.img

kernel.img: loadmap $(KOBJS)
	$(LD) $(KOBJS) -T loadmap -o mycelia.elf
	objdump -D mycelia.elf > mycelia.list
	objcopy mycelia.elf -O ihex mycelia.hex
	objcopy --only-keep-debug mycelia.elf kernel.sym
	objcopy mycelia.elf -O binary kernel.img

.s.o:
	$(AS) -o $@ $<

.c.o:
	$(CC) -c -o $@ $<

.c.i:
	$(CC) -E -o $@ $<

quartet: quartet.c
	cc -o $@ $<

wart: wart.c
	cc -o $@ $<

wart.i: wart.c
	cc -E -o $@ $<

ufork: ufork.c
	cc -Os -o $@ $<

ufork.i: ufork.c
	cc -E -o $@ $<

clean:
	rm -f *.o
	rm -f *.i
	rm -f *.bin
	rm -f *.hex
	rm -f *.elf
	rm -f *.list
	rm -f *.img
	rm -f *~ core
	rm -f quartet
	rm -f wart
	rm -f ufork

# Kernel Debugging

## qemu
To run normally:
~~~
$ qemu-system-arm -cpu arm1176 -M raspi -kernel kernel.img -initrd kernel.img -nographic
~~~
To trace instruction execution:
~~~
$ qemu-system-arm -cpu arm1176 -M raspi -kernel kernel.img -initrd kernel.img -nographic -d in_asm 2>qemu.log
~~~
To halt and allow connection from gdb:
~~~
$ qemu-system-arm -cpu arm1176 -M raspi -kernel kernel.img -initrd kernel.img -nographic -s -S
~~~

## gdb
~~~
$ gdb kernel.sym
~~~
~~~
(gdb) target remote localhost:1234
(gdb) display/i $pc
(gdb) break mycelia
(gdb) c
(gdb) info reg
(gdb) si
(gdb) ni
(gdb) q
~~~
~~~
$ kill %1
~~~

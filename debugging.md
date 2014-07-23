# Kernel Debugging

## qemu
~~~
$ qemu-system-arm -cpu arm1176 -M raspi -kernel kernel.img -initrd kernel.img -nographic -s -S <loadmap &
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

# Actor ABI

## Registers

| Register | Kernel Usage    | Actor Usage     |
|----------|-----------------|-----------------|
| r0       | arg_0 / return  | block address   |
| r1       | arg_1 / tmp_1   | target / answer |
| r2       | arg_2 / tmp_2   | base address    |
| r3       | arg_3 / tmp_3   | working data    |
| r4       | --preserved--   | state_0         |
| r5       | --preserved--   | state_1         |
| r6       | --preserved--   | state_2         |
| r7       | --preserved--   | state_3         |
| r8       | --preserved--   | --available--   |
| r9       | --preserved--   | --available--   |
| r10 (sl) | sponsor link    | sponsor link    |
| r11 (fp) | event frame ptr | event frame ptr |
| r12 (ip) | actor index ptr | actor index ptr |
| r13 (sp) | stack pointer   | stack pointer   |
| r13 (sp) | link register   | link register   |
| r15 (pc) | program counter | program counter |

## Event Structure
~~~
        +-------+-------+-------+-------+
  0x00  | address of target actor       |
        +-------------------------------+
  0x04  | customer / ok                 |  m
        + . . . . . . . . . . . . . . . +  e
  0x08  | parameter / fail              |  s
        + . . . . . . . . . . . . . . . +  s
  0x0c  |                               |  a
        +       .       .       .       +  g
  0x10  |                               |  e
        +       .       .       .       +
  0x14  |                               |  d
        +       .       .       .       +  a
  0x18  |                               |  t
        +       .       .       .       +  a
  0x1c  |                               |
        +-------+-------+-------+-------+
~~~

## Actor Structure

### Example 0
~~~
        +-------+-------+-------+-------+
  0x00  |       bl      reserve         |  m
        +-------------------------------+  a
  0x04  |       ldr     r1, [ip, #0x1c] |  c
        +-------------------------------+  h
  0x08  |       str     r1, [r0, #0x04] |  i  (_a_answer)
        +-------------------------------+  n
  0x0c  |       ldr     r1, [fp, #0x04] |  e  (_a_reply)
        +-------------------------------+
  0x10  |       str     r1, [r0]        |  c  (_a_send)
        +-------------------------------+  o
  0x14  |       bl      enqueue         |  d  (_a_end)
        +-------------------------------+  e
  0x18  |       b       complete        |
        +-------------------------------+
  0x1c  | data field containing answer  |
        +-------+-------+-------+-------+
~~~

### Example 1
~~~
        +-------+-------+-------+-------+
  0x00  |       ldr     pc, [pc, -#4]   |
        +-------------------------------+
  0x04  | address of actor behavior     |
        +-------------------------------+
  0x08  | actor state                   |
        +       .       .       .       +
  0x0c  |                               |
        +       .       .       .       +
  0x10  |                               |
        +       .       .       .       +
  0x14  |                               |
        +       .       .       .       +
  0x18  |                               |
        +       .       .       .       +
  0x1c  |                               |
        +-------+-------+-------+-------+
~~~

### Example 2
~~~
        +-------+-------+-------+-------+
  0x00  |       ldr     lr, [pc]        |
        +-------------------------------+
  0x04  |       blx     lr              |
        +-------------------------------+
  0x08  | address of actor behavior     |
        +-------------------------------+
  0x0c  | actor state                   |
        +       .       .       .       +
  0x10  |                               |
        +       .       .       .       +
  0x14  |                               |
        +       .       .       .       +
  0x18  |                               |
        +       .       .       .       +
  0x1c  |                               |
        +-------+-------+-------+-------+
~~~

### Example 3
~~~
        +-------+-------+-------+-------+
  0x00  |       mov     ip, pc          |
        +-------------------------------+
  0x04  |       ldmia   ip,{r4-r7,lr,pc}|
        +-------------------------------+
  0x08  | value for r4                  |
        +-------------------------------+
  0x0c  | value for r5                  |
        +-------------------------------+
  0x10  | value for r6                  |
        +-------------------------------+
  0x14  | value for r7                  |
        +-------------------------------+
  0x18  | value for lr                  |
        +-------------------------------+
  0x1c  | address of actor behavior     |
        +-------+-------+-------+-------+
~~~

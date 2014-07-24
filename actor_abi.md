# Actor ABI

## Memory

Memory is managed in cache-line-aligned blocks of 32 bytes (8x32-bit words).


## Registers

On entry to an actor behavior,
we want as many registers available as we can get.
The kernel's event dispatch loop does not use r4-r9 at all.
Actor behaviors may use r4-r9 freely, without preserving them.
Actor behaviors may also use r0-r3,
although they should not expect them to survive a kernel call.
Kernel procedures called from actor behaviors
are required to preserve r4-r9, but may freely use r0-r3.
Consistent special meanings are given to r10-r15.

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
| r14 (lr) | link register   | link register   |
| r15 (pc) | program counter | program counter |

All computation is driven by dispatching events.
An event designates a target actor and
up 7 words of additional message content.
When the target actor's behavior begins executing,
r10 (sl) will indicate the sponsor of the computation,
r11 (fp) will point to the base of the event block, and
r12 (ip) will point to the base of the actor block.
Kernel procedures called from an actor behavior
will expect r10 (sl) to remain stable,
pointing to the sponsor.
Note that actors should consider the sponsor opaque.

## Event Structure

An event block begins with the address of the actor
to whom the event will be dispatched.
The rest of the block may contain 0 to 7 additional words of message content.
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
By convention, the first word of the message (at offset 0x04)
is often the customer (an actor) to whom a reply may be directed.
The second word of the message (at offset 0x08) is usually the first parameter.
In the case that an actor may report success or failure,
two customers are provided.
The ok customer (at offset 0x04) for success/true results,
and the fail customer (at offet 0x08) for failure/false results.

## Actor Structure

The target actor may be accessed through the event (at offset 0x00),
or more directly via register r12 (ip).
An actor block must begin with directly executable code.
The block may also contain data fields
accessed by the behavior code.

### Example 0

There are several reasonable strategies
for organizing an actor's behavior.
The most basic is to embed the behavior
directly in the actor block.
Example 0 illustrates the typical elements
of a directly-coded actor behavior.
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
This actor behavior begins by calling `reserve`
to allocate a new block for a reply-message event.
The kernel procedure `reserve` is allowed to change r0-r3,
but must preserve r4-r9, and returns the block address in r0.
Next, r1 is loaded from the actor block offset 0x1c (the answer).
The answer in r1 is stored in the event block at offset 0x04.
Now, r1 is reloaded with the customer
from the request-event block offset 0x04.
The customer is r1 is stored as the target
of the reply-message event.
The reply-message event is added to the event queue
by calling the `enqueue` kernel procedure (which may change r0-r3).
Finally, the actor behavior jumps to `complete`,
which ends the processing of the request-event
and dispatches the next event.

Many directly-coded actor behaviors end with similar steps,
so they have been extracted into a series of jump labels
that can be used to end an actor behavior.
They can be considered abbreviations
for common sequences of completion steps.
Working backwards, we will consider the circumstances
in with each jump label is useful.

All actor behaviors must end with a jump to `complete`.
If they also want to `enqueue` an event,
they can jump to `_a_end` instead,
with the new event in r0.

If the target for the new event is in r1,
the actor can jump to `_a_send`,
which stores the target in the event
before calling `enqueue`.

If the target for the new event is
the customer from the current event,
the actor can jump to `_a_reply`,
which load the target address
from the current event block (offset 0x04).

If the answer is in r1,
the target is the current-event customer,
and the new event is in r0,
the actor can jump to `_a_answer`,
which stores the answer in the new event
and performs all the previously described steps.

### Example 1
~~~
        +-------+-------+-------+-------+
  0x00  |       ldr     pc, [pc, #-4]   |
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

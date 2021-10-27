# Actor ABI

## Memory

Memory is managed in cache-line-aligned blocks of 32 bytes (8x32-bit words).
An event-block holds a message and the target actor
to whom it will be dispatched.
An actor-block holds actor behavior and state.
Blocks may be chained
in order to extend the available storage
as needed.

## Registers

On entry to an actor behavior,
we want as many registers available as we can get.
The kernel's event dispatch loop does not make any assumptions
about the contents of r0-r9.
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
up to 7 words of additional message content.
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
The rest of the block may contain 0 to 7 additional words of message data.
~~~
        +--------+--------+--------+--------+
  0x00  | address of target actor           |
        +-----------------------------------+
  0x04  | customer / ok                     |  m
        + . . . . . . . . . . . . . . . . . +  e
  0x08  | parameter / fail                  |  s
        + . . . . . . . . . . . . . . . . . +  s
  0x0c  |                                   |  a
        +        .        .        .        +  g
  0x10  |                                   |  e
        +        .        .        .        +
  0x14  |                                   |  d
        +        .        .        .        +  a
  0x18  |                                   |  t
        +        .        .        .        +  a
  0x1c  |                                   |
        +--------+--------+--------+--------+
~~~
By convention, the first word of the message (at offset 0x04)
is often the customer (an actor) to whom a reply may be directed.
The second word of the message (at offset 0x08) is usually the first parameter.
In the case that an actor may report success or failure,
two customers are provided.
The _ok_ customer (at offset 0x04) for success/true results,
and the _fail_ customer (at offet 0x08) for failure/false results.
Additional parameters may follow starting at offset 0x0c.

## Actor Structure

The target actor may be accessed through the event block (offset 0x00),
or more directly via register r12 (ip).
An actor block must begin with directly executable code.
The block may also contain data fields
accessed by the code of the behavior.

### Example 0

There are several reasonable strategies
for organizing an actor's behavior.
The most basic is to embed the behavior
directly in the actor block.
Example 0 illustrates the typical elements
of a directly-coded actor behavior.
~~~
        +--------+--------+--------+--------+
  0x00  |       bl      reserve             |  m
        +-----------------------------------+  a
  0x04  |       ldr     r1, [ip, #0x1c]     |  c
        +-----------------------------------+  h
  0x08  |       str     r1, [r0, #0x04]     |  i  (_a_answer: r0=event, r1=answer)
        +-----------------------------------+  n
  0x0c  |       ldr     r1, [fp, #0x04]     |  e  (_a_reply: r0=event)
        +-----------------------------------+
  0x10  |       str     r1, [r0]            |  c  (_a_send: r0=event, r1=target)
        +-----------------------------------+  o
  0x14  |       bl      enqueue             |  d  (_a_end: r0=event)
        +-----------------------------------+  e
  0x18  |       b       complete            |
        +-----------------------------------+
  0x1c  | data field containing answer      |
        +--------+--------+--------+--------+
~~~
This actor behavior begins by calling `reserve`
to allocate a new block for a reply-message event.
The kernel procedure `reserve` is allowed to change r0-r3,
but must preserve r4-r9, and returns the block address in r0.
Next, r1 is loaded from the actor block offset 0x1c (the answer).
The answer in r1 is stored in the reply-event block at offset 0x04.
Now, r1 is reloaded with the customer
from the request-event block offset 0x04.
The customer in r1 is stored as the target
of the reply-message event.
The reply-message event is added to the event queue
by calling the `enqueue` kernel procedure (which may change r0-r3).
Finally, the actor behavior jumps to `complete`,
which ends the processing of the request-event
and dispatches the next event.

Many directly-coded actor behaviors end with similar steps.
These steps have been extracted into a series of jump labels
that can be used to complete an actor behavior.
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
which loads the target address
from the current event block (offset 0x04).

If the answer is in r1,
the target is the current-event customer,
and the new event is in r0,
the actor can jump to `_a_answer`,
which stores the answer in the new event (offset 0x04)
and performs all the previously described steps.

### Example 1

It is often useful for an actor's behavior
to be separated from it's state,
especially when the actor wants to switch
among several behaviors.
This can be accomplished by maintaining a pointer
to the desired behavior,
and jumping through that pointer to handle an event.
~~~
        +--------+--------+--------+--------+
  0x00  |       ldr     pc, [ip, #0x04]     |
        +-----------------------------------+
  0x04  | address of actor behavior         |
        +-----------------------------------+
  0x08  | actor state                       |
        +        .        .        .        +
  0x0c  |                                   |
        +        .        .        .        +
  0x10  |                                   |
        +        .        .        .        +
  0x14  |                                   |
        +        .        .        .        +
  0x18  |                                   |
        +        .        .        .        +
  0x1c  |                                   |
        +--------+--------+--------+--------+
~~~
As always, the kernel invokes an actor's behavior
by jumping to the beginning of the actor block.
The first instruction in the actor block
simply reloads the program counter
with the address of the current behavior.
Note that the value of ip is
the address of the actor block itself
thus [ip, #4] loads the new pc
from offset 0x04 in the actor block.
The actor behavior will have access to
the actor state (if any) through the ip register.

### Example 2

Here is another way to implement an indirect call to the actor behavior.
~~~
        +--------+--------+--------+--------+
  0x00  |       ldr     lr, [pc]            |
        +-----------------------------------+
  0x04  |       blx     lr                  |
        +-----------------------------------+
  0x08  | address of actor behavior         |
        +-----------------------------------+
  0x0c  | actor state                       |
        +        .        .        .        +
  0x10  |                                   |
        +        .        .        .        +
  0x14  |                                   |
        +        .        .        .        +
  0x18  |                                   |
        +        .        .        .        +
  0x1c  |                                   |
        +--------+--------+--------+--------+
~~~
The first instruction loads the actor behavior address
(from offset 0x08 in the actor block)
into the link register.
The second instruction jumps to that address,
while setting the link register
to the address following the instruction,
which is the beginning of the actor's state
(and the pointer to the current behavior).

### Example 3

A more powerful indirect-behavior implementation
can pre-load a group of registers from the actor state
while jumping to the actual behavior code.
~~~
        +--------+--------+--------+--------+
  0x00  |       mov     ip, pc              |
        +-----------------------------------+
  0x04  |       ldmia   ip, {r4-r7,lr,pc}   |
        +-----------------------------------+
  0x08  | value for r4                      |
        +-----------------------------------+
  0x0c  | value for r5                      |
        +-----------------------------------+
  0x10  | value for r6                      |
        +-----------------------------------+
  0x14  | value for r7                      |
        +-----------------------------------+
  0x18  | value for lr                      |
        +-----------------------------------+
  0x1c  | address of actor behavior         |
        +--------+--------+--------+--------+
~~~
The first instruction loads the actor state address
(offset 0x08 into the actor block)
into the index pointer (ip).
The second instruction loads up to 6 registers,
including the program counter (pc),
from the actor block.
Loading the program counter causes a jump
to the address loaded,
which is the address of the actual behavior code.

The behavior then begins executing
with several actor-state fields
already loaded into convenient registers.
Any combination of registers may be loaded,
as long as the final register is the pc
(to jump to the behavior).
Note that the register values must be in numerical order
based on the register numbers to be loaded.
The actor behavior may use the pre-loaded state in registers,
but must write back to the actor block
(through ip, which is offset +0x08)
to update the persistent actor state.

### Example 4

Pre-loading actor state (and jumping to the behavior)
can be accomplished with a single instruction.
~~~
        +--------+--------+--------+--------+
  0x00  |       ldmib   ip, {r4-r9,pc}      |
        +-----------------------------------+
  0x04  | value for r4                      |
        +-----------------------------------+
  0x08  | value for r5                      |
        +-----------------------------------+
  0x0c  | value for r6                      |
        +-----------------------------------+
  0x10  | value for r7                      |
        +-----------------------------------+
  0x14  | value for r8                      |
        +-----------------------------------+
  0x18  | value for r9                      |
        +-----------------------------------+
  0x1c  | address of actor behavior         |
        +--------+--------+--------+--------+
~~~
The actor's persistent state can be updated
through the ip register.

### Example 5

If we don't want to pre-load registers,
we can simply jump directly to the behavior.
The actor state can still be read/written,
as needed, through the ip register.
~~~
        +--------+--------+--------+--------+
  0x00  |       ldr     pc, [ip, #0x1c]     |
        +-----------------------------------+
  0x04  | actor state                       |
        +        .        .        .        +
  0x08  |                                   |
        +        .        .        .        +
  0x0c  |                                   |
        +        .        .        .        +
  0x10  |                                   |
        +        .        .        .        +
  0x14  |                                   |
        +        .        .        .        +
  0x18  |                                   |
        +-----------------------------------+
  0x1c  | address of actor behavior         |
        +--------+--------+--------+--------+
~~~
Note that this strategy locates the _behavior_
consistently at the end of the block,
which can be used to characterize
the "type" of the actor.

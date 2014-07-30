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
The rest of the block may contain 0 to 7 additional words of message data.
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
        +-------+-------+-------+-------+
  0x00  |       bl      reserve         |  m
        +-------------------------------+  a
  0x04  |       ldr     r1, [ip, #0x1c] |  c
        +-------------------------------+  h
  0x08  |       str     r1, [r0, #0x04] |  i  (_a_answer: r0=event, r1=answer)
        +-------------------------------+  n
  0x0c  |       ldr     r1, [fp, #0x04] |  e  (_a_reply: r0=event)
        +-------------------------------+
  0x10  |       str     r1, [r0]        |  c  (_a_send: r0=event, r1=target)
        +-------------------------------+  o
  0x14  |       bl      enqueue         |  d  (_a_end: r0=event)
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
As always, the kernel invokes an actor's behavior
by jumping to the beginning of the actor block.
The first instruction in the actor block
simply reloads the program counter
with the address of the current behavior.
Note that the value of pc is offset +8
from the currently executing instruction,
thus [pc, #-4] loads the new pc
from offset 0x04 in the actor block.
The actor behavior will have access to
the actor state
through the ip register,
which points to the beginning of the actor block.

### Example 2

Here is another way to implement an indirect call to the actor behavior.
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
        +-------+-------+-------+-------+
  0x00  |       mov     ip, pc          |
        +-------------------------------+
  0x04  |       ldmia   ip,{r4-r8,pc}   |
        +-------------------------------+
  0x08  | value for r4                  |
        +-------------------------------+
  0x0c  | value for r5                  |
        +-------------------------------+
  0x10  | value for r6                  |
        +-------------------------------+
  0x14  | value for r7                  |
        +-------------------------------+
  0x18  | value for r8                  |
        +-------------------------------+
  0x1c  | address of actor behavior     |
        +-------+-------+-------+-------+
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


## Kernel Procedures

### Control

#### complete

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | event | actor |
| Out | xx | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | event' | actor' |

Signal actor behavior completion.
Release completed `event` block,
and call dispatcher.
Note: This is often used as a no-op actor and/or behavior.

#### reserve

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | block | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Reserve (allocate) a `block` (32 bytes) of memory.

#### release

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | block | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | xx | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Release (free) a `block` for allocation by `reserve`.

### Events

#### enqueue

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | event | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | event | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Add an `event` to the dispatch queue.
Note that an event must have the target actor at \[r0\] (offset 0x00).
The `event` address is returned in r0.

#### send

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | target | 0x04 | 0x08 | 0x0c | 0x10 | 0x14 | 0x18 | 0x1c | -- | -- | -- | sponsor | -- | -- |
| Out | event | xx | xx | xx | 0x10 | 0x14 | 0x18 | 0x1c | -- | -- | -- | sponsor | -- | -- |

Send a message.
Registers r0-r7 are arranged
exactly like the event structure,
starting with the `target` actor in r0.
The message data is in r1-r7.
An event block is created, initialized, and enqueued.

#### send_0

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | target | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | event | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Send an empty message to `target`.
An event block is created, initialized, and enqueued.

#### send_1

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | target | 0x04 | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | event | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Send a one-word message to `target`.
The message data is in r1.
An event block is created, initialized, and enqueued.

#### send_2

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | target | 0x04 | 0x08 | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | event | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Send a two-word message to `target`.
The message data is in r1 and r2.
An event block is created, initialized, and enqueued.

#### send_3x

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | -- | -- | -- | -- | target | 0x04 | 0x08 | 0x10 | -- | -- | -- | sponsor | -- | -- |
| Out | event | xx | xx | xx | target | 0x04 | 0x08 | 0x10 | -- | -- | -- | sponsor | -- | -- |

Send a three-word message to `target`.
The target actor is in r4.
The message data is in r5-r7.
An event block is created, initialized, and enqueued.

### Actors

#### create

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | behavior | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | actor | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Create an actor from `example_3`.
The actor behavior (offset 0x1c) is set from r0.
The default actor state is all zeros.
On entry to the actor behavior,
fp points to the event,
\[fp\] points to the actor,
and ip points to the actor + 0x08.
Actor state is loaded in registers as follows:
0x08:r4, 0x0c:r5, 0x10:r6, 0x14:r7, 0x18:r8.

#### create_0

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | behavior | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | actor | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Create an actor from `example_1`.
The actor behavior (offset 0x04) is set from r0.
On entry to the actor behavior,
fp points to the event
and ip points to the actor.

#### create_1

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | behavior | r4 | -- | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | actor | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Create a single-parameter actor.
The actor behavior (offset 0x0c) is set from r0.
Intial value for r4 (offset 0x08) is set from r1.
On entry to the actor behavior,
fp points to the event,
\[fp\] points to the actor,
and ip points to the actor + 0x08.
Actor state is loaded in registers as follows:
0x08:r4.

#### create_2

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | behavior | r4 | r5 | -- | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |
| Out | actor | xx | xx | xx | -- | -- | -- | -- | -- | -- | -- | sponsor | -- | -- |

Create a two-parameter actor.
The actor behavior (offset 0x10) is set from r0.
Intial value for r4 (offset 0x08) is set from r1.
Intial value for r5 (offset 0x0c) is set from r2.
On entry to the actor behavior,
fp points to the event,
\[fp\] points to the actor,
and ip points to the actor + 0x08.
Actor state is loaded in registers as follows:
0x08:r4, 0x0c:r5.

#### create_3x

| Reg | r0 | r1 | r2 | r3 | r4 | r5 | r6 | r7 | r8 | r9 | r10 | sl | fp | ip |
|-----|----|----|----|----|----|----|----|----|----|----|-----|----|----|----|
| In  | -- | -- | -- | -- | r4 | r5 | r6 | behavior | -- | -- | -- | sponsor | -- | -- |
| Out | actor | xx | xx | xx | r4 | r5 | r6 | behavior | -- | -- | -- | sponsor | -- | -- |

Create a three-parameter actor.
The actor behavior (offset 0x14) is set from r7.
Intial value for r4 (offset 0x08) is set from r4.
Intial value for r5 (offset 0x0c) is set from r5.
Intial value for r6 (offset 0x10) is set from r6.
On entry to the actor behavior,
fp points to the event,
\[fp\] points to the actor,
and ip points to the actor + 0x08.
Actor state is loaded in registers as follows:
0x08:r4, 0x0c:r5, 0x10:r6.

## Actor/Behavior Library

### Built-In Actors

#### a_ignore

Ignore all messages.

| Offset | Actor Field |
|--------|-------|
| 0x00 | code |
| 0x04 | -- |
| 0x08 | -- |
| 0x0c | -- |
| 0x10 | -- |
| 0x14 | -- |
| 0x18 | -- |
| 0x1c | -- |

| Offset | Event Field |
|--------|-------|
| 0x00 | target | -> a_ignore
| 0x04 | msg_1 |
| 0x08 | msg_2 |
| 0x0c | msg_3 |
| 0x10 | msg_4 |
| 0x14 | msg_5 |
| 0x18 | msg_6 |
| 0x1c | msg_7 |

Note: This actor is not usually needed,
since `complete` can be referenced directly
(as if it were an actor).

#### a_forward

Foward message to `delegate`.

| Offset | Actor Field |
|--------|-------|
| 0x00 | code |
| 0x04 | code |
| 0x08 | code |
| 0x0c | code |
| 0x10 | code |
| 0x14 | -- |
| 0x18 | -- |
| 0x1c | delegate |

| Offset | Event Field |
|--------|-------|
| 0x00 | target = a_forward |
| 0x04 | msg_1 |
| 0x08 | msg_2 |
| 0x0c | msg_3 |
| 0x10 | msg_4 |
| 0x14 | msg_5 |
| 0x18 | msg_6 |
| 0x1c | msg_7 |

Note: This actor is used in the implementation of `a_oneshot`.

#### a_oneshot

#### a_fork

### Built-In Behaviors

#### b_label

#### b_tag

#### b_join

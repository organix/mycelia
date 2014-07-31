# Memory Management

Automatic memory management greatly simplifies the programmer's model.
Memory is managed in cache-line-aligned blocks of 32 bytes (8x32-bit words).
Any block that cannot be reached by following references from a "root"
is considered garbage and released back to the available-memory pool.
This process is called "garbage collection".

## Garbage Collection

In describing the operation of the garbage collector,
we use the following color codes:

  * White = Available Memory
  * Black = Old Reserved Memory
  * Red = New Reserved Memory

As a block is reserved,
it is marked Red.
As a block is released,
it is marked White.
At the beginning of a garbage-collection pass,
all Red blocks are marked Black.
During a garbage-collection pass,
we need to verify that the Black blocks
are still in use.

A garbage-pass starts by adding
all "root references" to the _scan list_.
For each block on the scan list,
if it is Black, we mark it Red
and add all references in the block to the scan list.
When the scan list is empty,
all blocks still marked Black
are released (and marked White).

# Memory Management

Automatic memory management greatly simplifies the programmer's model.
Memory is managed in cache-line-aligned blocks of 32 bytes (8x32-bit words).
Any block that cannot be reached by following references from a "root"
is considered garbage and released back to the available-memory pool.
This process is called "garbage collection".

## Garbage Collection

To begin garbage collection,
clear marks for all blocks.
Mark each each block in the free list.
Free blocks are unreachable,
but we don't want to release them.
Mark the root block and add it to the scan list.
The root block points to 
dynamically-allocated structures
we want to preserve,
even if they are not reachable through events.
For each block in the event queue,
if the event block is not marked,
mark it and add it to the scan list.
Events represent the work to be done,
thus nearly all in-use memory can be reached
through some pending event.

For each block on the scan list,
iterate over references in the block.
For each reference,
if the referenced block is not marked,
mark it and add it to the scan list.
When the scan list is empty,
release all the unmarked blocks,
linking them into the free list.

### GC Algorithm Pseudo-Code
~~~
BLOCK queue[];
int q_head;
int q_tail;
int q_size;
BLOCK heap[];
BOOLEAN mark[];
int i_free = END;
int n_blocks;
int i_scan[];
int n_scans;
int i_root;

void gc() {
    int i;
    int j;

    n_scans = 0;                            // start with empty scan list
    for (i = 0; i < n_blocks; ++i) {
        mark[i] = 0;                        // clear all marks
    }
    for (i = i_free; i != END; i = heap[i].ref[0]) {
        mark[i] = 1;                        // mark all "free" blocks
    }
    scan(i_root);                           // mark root block
    i = q_head;
    while (i != q_tail) {                   // mark event blocks
        scan(queue[i]);
        i = ((i + 1) % q_size);
    }
    for (i = 0; i < n_scans; ++i) {         // iterate over scan list
        for (j = 0; j < n_refs; ++j) {      // iterate over block refs
            scan(heap[i_scan[i]].ref[j]);   // mark block references
        }
    }
    for (i = 0; i < n_blocks; ++i) {
        if (mark[i] == 0) {
            release(heap[i]);               // release "garbage" blocks
        }
    }
}

void scan(int n) {
    if ((n >= 0) && (n < n_blocks)          // in range
     && (mark[n] == 0)) {                   // not already marked
        mark[n] = 1;                        // mark block
        scan_list[n_scans++] = n;           // add to scan list
    }
}

void release(int n) {
    heap[n].ref[0] = i_free;                // thread free blocks through ref_0
    i_free = n;                             // link block at head of free chain
}
~~~

### Implementation Notes

The actual implementation is coded directly in assembly-language.
The pseudo-code uses integer index values (into the heap array).
The assembly-code uses actual memory addresses,
with 0 playing the role of `END`.

The heap lives between `heap_start` and the current contents of `block_end`.
The contents of `block_free` are a pointer to the first available block,
or 0 if there are no blocks available.
When additional blocks are needed,
`block_end` is advanced
and the new block is put on the available list
(by _release_),
then immediately allocated (by _reserve_).

The heap is initially aligned to a 1k boundary.
At the beginning of garbage-collection,
`block_end` is rounded up to a 1k boundary.
The `mark` array is implemented as a tightly-packed bit-string,
positioned after `block_end`.
Each 32-bit word in the `mark` array represents 32 blocks of 32 bytes.
The `i_scan` array (the scan list)
begins immediately after the `mark` array.
The `mark` array and `i_scan` array area re-created for each GC run.

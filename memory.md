# Memory Management

Automatic memory management greatly simplifies the programmer's model.
Memory is managed in cache-line-aligned blocks of 32 bytes (8x32-bit words).
Any block that cannot be reached by following references from a "root"
is considered garbage and released back to the available-memory pool.
This process is called "garbage collection".

## Garbage Collection

To begin garbage collection,
clear marks for all blocks.
For each root reference,
mark the referenced block and add it to the scan list.
For each block on the scan list,
iterate over references in the block.
For each reference,
if the referenced block is not marked,
mark it and add it to the scan list.
When the scan list is empty,
re-thread all the unmarked blocks
into the available-block list.

~~~
BOOLEAN mark[];
BLOCK heap[];
int i_free = END;
int n_blocks;
int i_scan[];
int n_scans;

void gc(int root[], int n_roots) {
    int i;
    int j;

    n_scans = 0;                            // start with empty scan list
    for (i = 0; i < n_blocks; ++i) {
        mark[i] = 0;                        // clear all marks
    }
    for (i = i_free; i != END; i = heap[i].ref[0]) {
        mark[i] = 1;                        // mark all "free" blocks
    }
    for (i = 0; i < n_roots; ++i) {
        scan(root[i]);                      // mark root references
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

The actual implementation is directly in assembly-language.
Where integer index values (into the heap array) are used in the pseudo-code,
the assembly-code uses actual memory addresses,
with 0 playing the role of `END`.

The heap lives between `heap_start` and the current contents of `block_end`.
The contents of `block_free` are a pointer to the first available block,
or 0 if there are no blocks available.
When additional blocks are needed,
`block_end` is advanced
and the new block is put on the available list
(by _release_),
then immediately allocated (by _reserve_).

The `mark` array is implemented as a tightly-packed bit-string,
positioned after `block_end`.
The `i_scan` array (the scan list)
is positioned on the next 32-bit word-boundry
after the `mark` array.

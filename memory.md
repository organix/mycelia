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
into an available-block list.

~~~
BOOLEAN mark[];
BLOCK heap[];
int n_blocks;
int scan_list[];
int n_scans;

void gc(BLOCK root[], int n_roots) {
    BLOCK* p;
    int i;
    int j;
    int k;

    n_scans = 0;
    for (i = 0; i < n_blocks; ++i) {        // clear all marks
        mark[i] = 0;
    }
    for (p = block_free; p; p = p.ref[0]) { // mark all "free" blocks
        i = (int)(p - block_free);
        mark[i] = 1;
    }
    for (i = 0; i < n_roots; ++i) {         // mark root references
        scan(roots[i]);
    }
    for (i = 0; i < n_scans; ++i) {         // iterate over scan list
        BLOCK b = heap[scan_list[i]];
        for (j = 0; j < n_refs; ++j) {      // iterate over block refs
            scan(b.ref[j]);
        }
    }
    for (i = 0; i < n_blocks; ++i) {        // release "garbage" blocks
        if (mark[i] == 0) {
            release(heap[i]);
        }
    }
}

void scan(int n) {                          // mark, if not already marked
    if (mark[n] == 0) {
        mark[n] = 1;
        scan_list[n_scans++] = n;
    }
}
~~~

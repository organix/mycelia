# WART (WebAssembly Actor Runtime)

WASM only has four primitive types:
  * i32 -- 32-bit Integer
  * i64 -- 64-bit Integer
  * f32 -- 32-bit IEEE Float
  * f64 -- 64-bit IEEE Float

```
____  3322 2222 2222 1111  1111 1100 0000 0000
i32:  1098 7654 3210 9876  5432 1098 7654 3210

____  6666 5555 5555 5544  4444 4444 3333 3333  3322 2222 2222 1111  1111 1100 0000 0000
i64:  3210 9876 5432 1098  7654 3210 9876 5432  1098 7654 3210 9876  5432 1098 7654 3210
```

WASM has no pointers, only 0-based indexing into collections.
Linear memory (Heap) is addressed by offset from zero.

## Garbage Collected Heap

The GC Heap is composed of i64 cells.
Since all allocations are the same size (one cell),
there is never any fragmentation in the heap.
Each cell contains a Pair of i32 values (car and cdr).
The root cell is at offset 0.
The car of the root cell holds the current allocation limit.
The cdr of the root cell points to the free-cell chain.
The cdr of each free cell is the offset of the next free cell.
The cdr of last cell in the chain is 0.

```
[0] = 1024,1  root cell (1K cells available)
[1] = 0,0     end of free-list (next available linear offset)
```

After a few allocations and frees the heap could look like this...

```
[0] = 1024,5  root cell (1K cells available)
[1] = _,_       allocated cell
[2] = _,_       allocated cell
[3] = 0,7     free cell [1]
[4] = _,_       allocated cell
[5] = 0,3     free cell [0]
[6] = _,_       allocated cell
[7] = 0,0     end of free-list (next available linear offset)
```

## Tagged Value (Boxed) Encoding

Encoding     | Components    | Dispatch
-------------|---------------|----------
Immediate    | Tag, Value    | Implicit
Indirect     | Tag, ^Data    | Implicit
Polymorphic  | Tag, ^Code    | Explicit
Virtual      | Tag, ^Object  | Explicit
Asynchronous | Tag, ^Actor   | Delayed

Values also have a storage class:
  * ROM   -- immutable pre-existing
  * RAM   -- mutable pre-existing
  * Heap  -- mutable/immutable dynamic (GC?)
  * Stack -- mutable/immutable transient

```
Pointers to i64 values are 8-byte aligned, making the 3 LSBs zero.
Pointers to i32 values are 4-byte aligned, making the 2 LSBs zero.

____  3322 2222 2222 1111  1111 1100 0000 0000
i32:  1098 7654 3210 9876  5432 1098 7654 3210
      xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xg00
                                           ^^^-- Type
                                           |+--- Indirect
                                           +---- GC Trace

2#00 = Immediate 30-bit signed Integer value=(x>>2)
      snnn nnnn nnnn nnnn  nnnn nnnn nnnn nn00
2#01 = Immediate 24-bit value=(x>>8) type=((n>>2)&~0x1F)
      vvvv vvvv vvvv vvvv  vvvv vvvv tttt tt01  (24-bit value, 6-bit type)
      000c cccc cccc cccc  cccc cccc 0000 0001 = 21-bit Unicode code-point
      vvvv vvvv vvvv vvvv  tttt tttt 1111 1101  (16-bit value, 8-bit type)
      ssss ssss ssss ssss  0000 0000 1111 1101 = 16-bit interned symbol
      pppp pppp pppp pppp  0000 0001 1111 1101 = 16-bit procedure number
      vvvv vvvv tttt tttt  1111 1111 1111 1101  (8-bit value, 8-bit type)
      0000 0000 0000 0000  1111 1111 1111 1101 = #f
      0000 0001 0000 0000  1111 1111 1111 1101 = #t
      0000 0002 0000 0000  1111 1111 1111 1101 = '()
      0000 000F 0000 0000  1111 1111 1111 1101 = ""
      1111 1111 0000 0000  1111 1111 1111 1101 = #undefined
2#10 = Pointer to Pair of boxed values addr=(x&~0x7) gc=(x&0x4)
  +-- aaaa aaaa aaaa aaaa  aaaa aaaa aaaa ag10
  +-> xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xgtt = car
      xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xgtt = cdr
2#11 = Pointer to Object (code+data) value addr=(x&~0x7) gc=(x&0x4)
  +-- aaaa aaaa aaaa aaaa  aaaa aaaa aaaa ag11
  +-> xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xgtt = code
      xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xgtt = data
```

The GC Trace bit is set (1) if the corresponding object
should be included in GC liveness tracing.
Immediate values, and pointers with GC Trace clear (0),
are not candidates for GC reclaimation.

## Object Method Invocations

```
object.method(arg*) ==> result
```

```
object operator arg ==> result
```

```
(procedure object . arg*) ==> result
procedure(object, arg*) ==> result
```

```
SEND (cust, selector, arg*) TO object ==> SEND result TO cust
```

```
target.dispatch(target, (selector, arg*)) ==> result
  |      ^        ^        ^        ^
  |      |        |        |        |
  +---->[*|*]    [*|*]--->[*|*]-----+
           |
           +---> ...
```

## Actor Behaviors

Actor behaviors return a collection of _effects_.

```
Success: --->[*|*]--->[*|*]---> beh
              |        |
              v        v
             actors   msgs
```

On succcess, this is:
  * a set of newly created actors
  * a set of message sent
  * an optional new behavior (or `'()`)

```
Failure: --->[*|*]---> error
              |
              v
             UNDEF
```

On failure, a value describing the _error_ is returned.

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

### Disjoint Base Types

Tag  | Type   | Description
-----|--------|------------
2#00 | Fixnum | (x>>2) = 2's-complement integer
2#01 | Pair   | (x&~3) = machine address of cell
2#10 | Symbol | (x>>2) = index into symbol table
2#11 | Actor  | (x&~3) = machine address of cell

_Procedures_ are encoded as _Actors_
(since they are opaque values),
using the raw code address.
Some code performs an additional check
to disambiguate when needed.

## Ground Environment

The following procedures are defined in the ground environment:
  * `(quote `_expression_`)`
  * `(list . `_objects_`)`
  * `(cons `_head_` `_tail_`)`
  * `(car `_pair_`)`
  * `(cdr `_pair_`)`
  * `(if `_predicate_` `_consequent_` `_alternative_`)`
  * `(and . `_expressions_`)`
  * `(or . `_expressions_`)`
  * `(eq? . `_objects_`)`
  * `(equal? . `_objects_`)`
  * `(lambda `_pattern_` . `_objects_`)`
  * `(eval `_expression_`)`
  * `(macro `_pattern_` . `_objects_`)`
  * `(define `_pattern_` `_expression_`)`
  * `(boolean? . `_objects_`)`
  * `(null? . `_objects_`)`
  * `(pair? . `_objects_`)`
  * `(symbol? . `_objects_`)`
  * `(number? . `_objects_`)`
  * `(+ . `_numbers_`)`
  * `(- . `_numbers_`)`
  * `(* . `_numbers_`)`
  * `(< . `_numbers_`)`
  * `(<= . `_numbers_`)`
  * `(= . `_numbers_`)`
  * `(>= . `_numbers_`)`
  * `(> . `_numbers_`)`

## Garbage Collected Heap

The GC Heap is composed of i64 cells.
Since all allocations are the same size (one cell),
there is never any fragmentation in the heap.
Each cell contains a Pair of i32 values (car and cdr).
The root cell is at offset 0.
The car of the root cell holds the next available linear offset.
The cdr of the root cell is the offset of the free-cell chain.
The cdr of each free cell is the offset of the next free cell.
The cdr of last cell in the chain is 0.

```
[0] = 1,1     root cell (limit,free)
[1] = 0,0     end of free-list (next available linear offset)
```

After a few allocations and frees the heap could look like this...

```
[0] = 7,5     root cell (limit,free)
[1] = _,_       allocated cell
[2] = _,_       allocated cell
[3] = 0,7     free cell [1]
[4] = _,_       allocated cell
[5] = 0,3     free cell [0]
[6] = _,_       allocated cell
[7] = 0,0     end of free-list (next available linear offset)
```

Note: There are no pointers in the free-list, only offsets.

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
      xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xxtt
                                            ^^-- Pointer
                                            +--- Subtype

2#00 = Immediate 30-bit signed Integer value=(x>>2)
      snnn nnnn nnnn nnnn  nnnn nnnn nnnn nn00
2#01 = Pointer to Pair of boxed values addr=(x&~3)
  +-- aaaa aaaa aaaa aaaa  aaaa aaaa aaaa aa01
  +-> xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xxtt = car
      xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xxtt = cdr
2#10 = Immediate 30-bit unsigned Index value=(x>>2)
      iiii iiii iiii iiii  iiii iiii iiii ii10
2#11 = Pointer to Object (code+data) value addr=(x&~3)
  +-- aaaa aaaa aaaa aaaa  aaaa aaaa aaaa aa11
  +-> xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xxtt = proc
      xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xxtt = var
```

### Object Method Invocations

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

## Actor Structures

Each Actor/Object defines its behavior
with a code-address in the `proc` field
and a data-address in the `var` field.
The `proc` is invoked with Actor as `self` and the Message as `arg`.

```
actor: --->[*|*]---> var
            |
            v
           proc(self, arg)
```

An Actor/Object returns a collection of _effects_.

```
Failure: --->[*|*]---> error
              |
              v
             FAIL
```

On failure, a value describing the _error_ is returned.
As a special-case, `UNDEF` is returned for internal errors.

```
Success: --->[*|*]--->[*|*]---> beh
              |        |
              v        v
             actors   events
```

On succcess, this is:
  * a set of newly created actors
  * a set of new message events
  * an optional new behavior (or `()`)
As a special-case, `()` is returned for no effect, to avoid allocation.

```
events: --->[*|*]---> ... --->[*|/]
             |                 |
             v                 v
            [*|*]---> msg_1   [*|*]---> msg_n
             |                 |
             v                 v
            target_1          target_n
```

Each event consists of:
  * a target actor
  * message contents

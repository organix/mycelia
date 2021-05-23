# Kernel Language Support

[Kernel](https://web.cs.wpi.edu/~jshutt/kernel.html) is a language in the LISP/Scheme family. 
This implementation, while faithful to the novel semantics of Kernel 
and consistent with John's specification, 
is **not** a conforming Kernel 
because it does not provide all of the required features 
defined in the Kernel standard (R<sup>-1</sup>RK). 
However, we believe it is a useful subset 
which illustrates our own novel actor-based approach to the implementation. 
This implementation is designed for machines with 32-bit words.

## Bootstrap Combiners

### exit

`(exit)`

The _`exit`_ applicative terminates the Kernel runtime system.

### list

`(list . `_objects_`)`

The _`list`_ applicative returns _objects_.

The underlying operative of _`list`_ 
returns its undifferentiated operand tree, 
regardless of whether that tree is or is not a list. 
The behavior of the applicative is therefore determined 
by the way the Kernel evaluator algorithm evaluates arguments.

### boolean?

`(boolean? . `_objects_`)`

The _`boolean?`_ applicative returns `#t` 
if _objects_ all have _boolean_ type, 
otherwise `#f`.

### eq?

`(eq? . `_objects_`)`

The _`eq?`_ applicative returns `#t` 
unless some two of its arguments are 
different objects,
otherwise `#f`.

### hexdump

`(hexdump `_address_` `_count_`)`

The _`hexdump`_ applicative prints a data dump to the console,
starting at _address_ and continuing for _count_ bytes.

#### Example:
```
> (hexdump #xc000 64)
0000c000  1c f0 9c e5 6c 69 73 74  00 00 00 00 00 00 00 00  |....list........|
0000c010  00 00 00 00 00 00 00 00  00 00 00 00 a0 8d 00 00  |................|
0000c020  1c f0 9c e5 65 78 69 74  00 00 00 00 00 00 00 00  |....exit........|
0000c030  00 00 00 00 00 00 00 00  00 00 00 00 a0 8d 00 00  |................|
```

### load-bytes

`(load-bytes `_address_` `_count_`)`

The _`load-bytes`_ applicative constructs and returns a list of exact integers 
representing the data bytes found in system memory
starting at _address_ and continuing for _length_ bytes.

### store-bytes

`(store-bytes `_address_` `_list_`)`

The _`store-bytes`_ applicative takes a _list_ of exact integers 
representing the data bytes to be stored in system memory
starting at _address_.
The result returned by _`store-bytes`_ is `#inert`.

### load-words

`(load-words `_address_` `_count_`)`

The _`load-words`_ applicative constructs and returns a list of exact integers 
representing the 32-bit words found in system memory
starting at _address_ and continuing for _count_ words.
The _address_ must be aligned on a 4-byte boundary.

### store-words

`(store-words `_address_` `_list_`)`

The _`store-words`_ applicative takes a _list_ of exact integers 
representing the 32-bit words to be stored in system memory
starting at _address_.
The result returned by _`store-words`_ is `#inert`.

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

### boolean?

`(boolean? . `_objects_`)`

The _`boolean?`_ applicative returns `#t`
if _objects_ all have _boolean_ type,
otherwise `#f`.

### symbol?

`(symbol? . `_objects_`)`

The _`symbol?`_ applicative returns `#t`
if _objects_ all have _symbol_ type,
otherwise `#f`.

### inert?

`(inert? . `_objects_`)`

The _`inert?`_ applicative returns `#t`
if _objects_ are all `#inert`,
otherwise `#f`.

### pair?

`(pair? . `_objects_`)`

The _`pair?`_ applicative returns `#t`
if _objects_ all have _pair_ type,
otherwise `#f`.

### null?

`(null? . `_objects_`)`

The _`null?`_ applicative returns `#t`
if _objects_ are all `()`,
otherwise `#f`.

### eq?

`(eq? . `_objects_`)`

The _`eq?`_ applicative returns `#t`
unless some two of its arguments are
different objects,
otherwise `#f`.

### list

`(list . `_objects_`)`

The _`list`_ applicative returns _objects_.

The underlying operative of _`list`_
returns its undifferentiated operand tree,
regardless of whether that tree is or is not a list.
The behavior of the applicative is therefore determined
by the way the Kernel evaluator algorithm evaluates arguments.

### $define!

`($define! `⟨definiend⟩` `⟨expression⟩`)`

The _`$define!`_ operative evaluates ⟨expression⟩ in the dynamic environment
(that is, the environment in which the `($define! `...`)` combination is evaluated),
and matches ⟨definiend⟩ to the result in the dynamic environment,
binding each symbol in ⟨definiend⟩ in the dynamic environment
to the corresponding part of the result.
The result returned by _`$define!`_ is `#inert`.

### $vau

`($vau `⟨formals⟩` `⟨eformal⟩` . `⟨objects⟩`)`

⟨formals⟩ should be a formal parameter tree,
as described for the ⟨definiend⟩ of the _`$define!`_ operative.
⟨eformal⟩ should be either a symbol or `#ignore`.
A _`$vau`_ expression evaluates to an operative.
The environment in which the _`$vau`_ expression was evaluated
is remembered as the compound operative’s _static environment_.
When the compound operative is later called with an object and an environment,
here called respectively the _operand tree_ and the _dynamic environment_,
  1. A new, initially empty _local environment_ is created,
  with the _static environment_ as its parent.
  2. The formal parameter tree ⟨formals⟩ is matched
  in the _local environment_ to the _operand tree_,
  binding the symbols of ⟨formals⟩
  to the corresponding parts of the operand tree.
  3. ⟨eformal⟩ is matched to the _dynamic environment_;
  that is, if ⟨eformal⟩ is a symbol then that symbol is bound to the _dynamic environment_,
  or ignored if ⟨eformal⟩ is `#ignore`.
  4. The expressions in ⟨objects⟩ are evaluated sequentially,
  from left to right, in the _local environment_.
  The final expression is evaluated in a _tail context_.
  If ⟨objects⟩ is `()`, the result is `#inert`.

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

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


## Core Features

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

### environment?

`(environment? . `_objects_`)`

The _`environment?`_ applicative returns `#t`
if _objects_ all have _environment_ type,
otherwise `#f`.

### ignore?

`(ignore? . `_objects_`)`

The _`ignore?`_ applicative returns `#t`
if _objects_ are all `#ignore`,
otherwise `#f`.

### eq?

`(eq? . `_objects_`)`

The _`eq?`_ applicative returns `#t`
unless some two of its arguments are
different objects,
otherwise `#f`.

### $if

`($if `⟨test⟩` `⟨consequent⟩` `⟨alternative⟩`)`

The _`$if`_ operative first evaluates ⟨test⟩ in the dynamic environment.
If the result is not of type boolean, an error is signaled.
If the result is `#t`, ⟨consequent⟩ is then evaluated
in the dynamic environment as a _tail context_.
Otherwise, ⟨alternative⟩ is evaluated
in the dynamic environment as a _tail context_.

### cons

`(cons `_object1_` `_object2_`)`

A new pair object is constructed and returned,
whose car and cdr referents are respectively _object1_ and _object2_.
The objects returned by two different calls to _`cons`_ are not _`eq?`_.

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
(that is, the environment in which the <tt>(<i>$define!</i> </tt>...<tt>)</tt> combination is evaluated),
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

### wrap

`(wrap `_combiner_`)`

The _`wrap`_ applicative returns an applicative whose underlying combiner is _combiner_.

### unwrap

`(unwrap `_applicative_`)`

The _`unwrap`_ applicative returns the underlying combiner of _applicative_.
If _applicative_ is not an applicative, an error is signaled.

### $sequence

`($sequence . `⟨objects⟩`)`

The _`$sequence`_ operative evaluates the elements of the list ⟨objects⟩
in the dynamic environment, one at a time from left to right.
If ⟨objects⟩ is a nonempty finite list,
its last element is evaluated as a tail context.
If ⟨objects⟩ is `()`, the result is `#inert`.

### $lambda

`($lambda `⟨formals⟩` . `⟨objects⟩`)`

⟨formals⟩ should be a formal parameter tree as described for operative _`$define!`_.
The expression
<pre>
(<i>$lambda</i> ⟨formals⟩ . ⟨objects⟩)
</pre>
is equivalent to
<pre>
(<i>wrap</i> (<i>$vau</i> ⟨formals⟩ #ignore . ⟨objects⟩))
</pre>


## Library Features

### car

`(car `_pair_`)`

### cdr

`(cdr `_pair_`)`

### get-current-env

`(get-current-env)`


## Machine Tools

### dump-bytes

`(dump-bytes `_address_` `_count_`)`

The _`dump-bytes`_ applicative prints a byte dump to the console,
starting at _address_ and continuing for _count_ bytes.

#### Example:
```
> (dump-bytes #xc000 64)
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

### dump-words

`(dump-words `_address_` `_count_`)`

The _`dump-words`_ applicative prints a 32-bit word dump to the console,
starting at _address_ and continuing for _count_ words.
The _address_ must be aligned on a 4-byte boundary.

#### Example:
```
> (dump-words #xd000 32)
0000_
_d000: e59cf01c 706d7564 7479622d 00007365 00000000 00000000 00000000 00008ee0
_d020: e59cf01c 74697865 00000000 00000000 00000000 00000000 00000000 00008ee0
_d040: e59cf01c 0000d000 0000d080 bbbbbbbb cccccccc dddddddd eeeeeeee 00008f60
_d060: e59cf01c 0000d000 aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee 00008d10
```

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
The _address_ must be aligned on a 4-byte boundary.
The result returned by _`store-words`_ is `#inert`.

### address-of

`(address-of `_object_`)`

The _`address-of`_ applicative returns an exact integer
representing the address of _object_.

### sponsor-reserve

`(sponsor-reserve)`

The _`sponsor-reserve`_ applicative returns an exact integer
representing the address of a newly-allocated 32-byte block of memory.

### sponsor-release

`(sponsor-release `_address_`)`

The _`sponsor-release`_ applicative deallocates
the previously-reserved block of memory at _address_.
The result is `#inert`.

### sponsor-enqueue

`(sponsor-enqueue `_address_`)`

The _`sponsor-enqueue`_ applicative adds
the previously-reserved block of memory at _address_
to the actor runtime message-event queue.
The result is `#inert`.

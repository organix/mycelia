# A Type System for Programming Languages

The concept of _Type_ in a programming language
expresses a set of _constraints_ on a _value_.
A _Value_ may have several "is-a" relationships
with various types,
indicating that value is an acceptable substitute
for a placeholder of that type.
A _Type_ may also have several "is-a" relationships
with other types.
The "is-a" relationship is a directed acyclic graph.


## Basic Types

The [JSON](http://www.ecma-international.org/publications/files/ECMA-ST/ECMA-404.pdf) standard
describes a set of six basic data-types
that have proven to be valuable for information exchange.
We will start by organizing that set
into a hierarchy of disjoint sub-types.
Each sub-type has an "is-a" relationship
with its super-type.
This partitioning will form the basis
for describing more complex types.
The initial hierarchy looks like this:

  * Value
    * Object
      * Boolean
        * `true`
        * `false`
      * Number
      * String
      * Sequence
      * Dictionary
    * Nothing
      * `null`

Naming types can be quite challenging,
as many different names have been used
historically to denote the same semantic type.
For example, JSON uses `Array` and `Object`
where we have used `Sequence` and `Dictionary`.

### Value

_Value_ is the top of the hierarchy.
Every Value is either an _Object_ or _Nothing_.

### Object

An _Object_ **is-a** Value that denotes _Something_ (as opposed to _Nothing_).

### Nothing

_Nothing_ **is-a** Value that denotes the lack of an _Object_.
The concrete Value `null` is the sole instance of the _Nothing_ type.

### Boolean

A _Boolean_ **is-a** Object that denotes a Boolean logical value.
The concrete Values `true` and `false` are the only two instances of the _Boolean_ type.

### Number

A _Number_ **is-a** Object that denotes a mathematical numeric value.
There are many sub-types of Number,
many of which overlap each other,
while partitioning numbers in different ways.
JSON encodes exact decimal values of arbitrary (but finite) size and precision.

### String

A _String_ **is-a** Object that denotes a finite sequence of Unicode code-points (characters).

### Sequence

A _Sequence_ **is-a** Object that denotes a finite sequence of _Values_.

### Dictionary

A _Dictionary_ **is-a** Object that denotes a finite association
between _Strings_ (keys) and _Values_.


## Representations

There is often confusion between the sub-typing "is-a" relationship
and the "represents" relationship between values.
For example, JSON describes the "represents" relationship
between Unicode strings (code-point sequences)
and the six basic data-types that may be "encoded" in JSON.
This is **not** a sub-type (or substitutability) relationship.
It is a transformation from one value to another.

# A Type System for Actor Programming

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
\[\[Q: What about non-_String_ keys?\]\]


## Representations

There is often confusion between the sub-typing "is-a" relationship
and the "represents" relationship between values.
For example, JSON describes the "represents" relationship
between Unicode strings (code-point sequences)
and the six basic data-types that may be "encoded" in JSON.
This is **not** a sub-type (or substitutability) relationship.
It is a transformation from one value to another.

## Computational Types

The [Basic Types](#basic-types) from JSON all denote immutable data values.
Several additional types of Objects may arise during computation.
Some of these may have [Representations](#representations) in terms of the Basic Types,
which implies an additional layer of translation/interpretation.
Some important Computational Types include:

  * Actor
    * Function
  * Behavior
  * Message

### Actor

An _Actor_ **is-a** Object that designates the capability to send a _Message_.
Values of this type are **opaque**, but may be compared for identity.
Making a copy does not duplicate the _Actor_,
only the _Capability_ to send it a message.
For transport, the Capability can be represented by a specially-encoded _String_.

### Function

A _Function_ **is-a** Actor that performs a transformation
from a _domain_ value to a _range_ value.
The types of the domain and range may be different.
The Function is represented by an Actor with a restricted protocol.
In the simplest case, the restricted protocol entails that:

  * Each message includes a _customer_
  * Each message results in exactly one message to the _customer_
  * Equal messages (excluding the _customer_) produce equal results

### Behavior

A _Behavior_ **is-a** Object that describes the _Effects_
of an _Actor_ handling a _Message_.

### Message

A _Message_ **is-a** Value sent to an _Actor_
to invoke the actor's _Behavior_,
causing some (possibly empty) set of _Effects_.

## Abstract Data Types

### Bitvector

A _Bitvector_ **is-a** Object that denotes a finite sequence of _Boolean_ values.
For transport, a Bitvector can be represented by an arbitrary-precision Integer _Number_.

### Blob

A _Blob_ **is-a** Object that denotes a finite sequence of _Byte_ values
(Integer _Numbers_ in the range \[0, 255\]).
For transport, a Blob can be represented by a _String_,
with code-points restricted the _Byte_ range.

## Extended Types

### Stream

### Sum Type

### Product Type

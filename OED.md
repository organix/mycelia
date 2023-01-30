# Octet-Encoded Data (OED)

For more-efficient network transmission,
we propose an octet-stream encoding
which is compatible with [JSON](http://www.ecma-international.org/publications/files/ECMA-ST/ECMA-404.pdf).
There are multiple ways to encode the same abstract JSON value.
All valid encodings produce equivalent JSON values,
although some representation details are lost.
Every valid JSON value can be represented in OED.
Values may be round-tripped without semantic loss.
Arbitrarily large values are fully supported.
Decimal values can be represented exactly,
without loss due to base-2 translation.

## Goals

The encoding should acheive the following goals:

  * Self-describing data-types and representations
  * Better information-density than JSON
  * Arbitrarily large (finite) representation sizes
  * Lossless translation from JSON and machine types
  * Well-defined translation to JSON and machine types
  * Extension mechanism for application-defined representations
  * Capabilities can be distinguished from other data-types
  * Easy-to-implement encode/decode
  * Encoded data is navigable without fully decoding

## Design

Let's start with the easy values,
_false_ (`2#1000_0000`),
_true_ (`2#1000_0001`),
and _null_ (`2#1000_1111`).
A single octet for each typed value.
The four remaining JSON types,
_Number_, _String_, _Array_, and _Object_,
can encode arbitrarily-large values,
so we will need a _Number_ to describe their _size_.
A single-octet encoding for small integers
provides a base-case for the recursive definition
of _size_ of the _size_.
Including both positive and negative numbers
in the single-octet encoding
keeps the encoding small
for additional fields of larger encodings.
However, it is unclear how large the range should be.

The most basic form of arbitrary-sized _Numbers_
are arbitrary-length bit-strings.
With the addition of a _sign_ bit,
we can describe any finite _integer_ value.
By adding an _integer_ field for an _exponent_,
we can describe any finite _decimal_ value (assuming base-10).
By adding another _integer_ field for _base_,
we can describe any finite _rational_ value,
and encode alternate bases (such as 2 for IEEE floats)
without loss of precision.

  * Positive Integer: _type_=`2#1000_0010` _size_::Number _integer_::Octet\*
  * Negative Integer: _type_=`2#1000_0011` _size_::Number _integer_::Octet\*
  * Positive Decimal: _type_=`2#1000_0100` _exponent_::Number _size_::Number _integer_::Octet\*
  * Negative Decimal: _type_=`2#1000_0101` _exponent_::Number _size_::Number _integer_::Octet\*
  * Positive Rational: _type_=`2#1000_0110` _base_::Number _exponent_::Number _size_::Number _integer_::Octet\*
  * Negative Rational: _type_=`2#1000_0111` _base_::Number _exponent_::Number _size_::Number _integer_::Octet\*

The basic encoding is 2's-complement, least-significant-byte first.
The _sign_ is _positive_ (0) or _negative_ (1).
All bits above the most-significant bit are assumed to match the sign.
A 1-component _Number_ is just an _integer_ value.
A 2-component _Number_ also includes an _exponent_,
encoded as an additional _Number_.
A 3-component _Number_ also includes a _base_,
encoded as an additional _Number_.
The default _exponent_ is 0.
The default _base_ is 10.
The _size_ field is a _Number_ describing
the number of **bits** (not octets) in the _integer_ value.
There is no requirement that a _Number_ is encoded with the minimum number of octets.
If the _size_ is 0, the _Number_ is 0 (if positive) or -1 (if negative),
and there are no _integer_ octets.
The octets of the _integer_ value (LSB to MSB) follow the _size_.
If the number of encoded bits is not a multiple of 8,
the final octet (MSB) will be padded according to the _sign_.
The number designated is equal to (_integer_ × _base_ ^ _exponent_).
Note that all the components are signed.
Rational numbers may be encoded as an _exponent_ of -1,
with the _integer_ as the numerator, and the _base_ as the denominator.

The _String_ type represent an arbitrary-sized sequence of Unicode code-points.
UTF-8 has become the default encoding for textual data throughout the world-wide-web,
so we require explicit support for that encoding.
Raw octet data (BLOBs) are also an important use-case,
where the code-points represented are restricted to the range 0 thru 255.
In order to support extensions for application-defined representations,
and to encapsulate foreign data verbatim,
BLOBs may be labelled with encoding meta-data.
It is unclear if a memoization feature
is worth the additional complexity it introduces,
particularly when link data compression is likely.

  * Raw Octet BLOB: _type_=`2#1000_1010` _size_::Number _data_::Octet\*
  * Extension BLOB: _type_=`2#1000_1011` _meta_::Value _size_::Number _data_::Octet\*
  * UTF-8 String: _type_=`2#1000_1100` _length_::Number _size_::Number _data_::Octet\*

The _size_ field is a _Number_ describing
the number of **octets** in the _data_.
If the encoding is UTF-8,
the _length_ field is a _Number_ describing the
the number of code-points in the _String_.
If the _length_ is 0, there is no _size_ field (and no _data_).
The extension encoding includes a _meta_ field,
which is an arbitrary (OED-encoded) _Value_.
The extension may be converted to a _String_
by treating the octets of the entire encoded value
(including the Extention BLOB type prefix) as code-points.

The _Array_ type represents an arbitrary-sized sequence of _Value_ elements.
The values are not required to have the same type.

  * Array: _type_=`2#1000_1000` _length_::Number _size_::Number _elements_::Value\*

The _size_ field is a _Number_ describing
the number of **octets** encoding the _elements_.
The _length_ field is a _Number_ describing
the number of _elements_ in the _Array_.
If the _length_ is 0, there is no _size_ field (and no _elements_).

The _Object_ type represents an arbitrary-sized collection of _name_/_value_ members.
Each _name_ should be a _String_ for JSON compatibility,
otherwise the JSON value is the _String_ of encoded octets.
Each _value_ may be any type,
including nested _Object_ or _Array_ values.

  * Object: _type_=`2#1000_1001` _length_::Number _size_::Number _members_::(_name_::Value _value_::Value)\*

The _size_ field is a _Number_ describing
the number of **octets** encoding the _members_.
The _length_ field is a _Number_ describing
the number of _members_ in the _Object_.
If the _length_ is 0, there is no _size_ field (and no _members_).

## Interoperability

Every OED document corresponds to a JSON document
containing all the same data,
although without some of the meta-data describing representations.
Converting a JSON document to OED and back to JSON
should result in an equivalent document.
Following the recommendations in [RFC 8259](https://www.rfc-editor.org/rfc/rfc8259)
will increase the portability of OED documents.

## Summary

type/prefix   | suffix                                                     | description
--------------|------------------------------------------------------------|--------------
`2#0xxx_xxxx` | -                                                          | positive small integer (0..127)
`2#1000_0000` | -                                                          | `false`
`2#1000_0001` | -                                                          | `true`
`2#1000_0010` | _size_::Number _int_::Octet\*                              | Number (positive integer)
`2#1000_0011` | _size_::Number _int_::Octet\*                              | Number (negative integer)
`2#1000_0100` | _exp_::Number _size_::Number _int_::Octet\*                | Number (positive decimal)
`2#1000_0101` | _exp_::Number _size_::Number _int_::Octet\*                | Number (negative decimal)
`2#1000_0110` | _base_::Number _exp_::Number _size_::Number _int_::Octet\* | Number (positive rational)
`2#1000_0111` | _base_::Number _exp_::Number _size_::Number _int_::Octet\* | Number (negative rational)
`2#1000_1000` | _length_::Number _size_::Number _elements_::Value\*        | Array
`2#1000_1001` | _length_::Number _size_::Number _members_::Octet\*         | Object
`2#1000_1010` | _size_::Number _data_::Octet\*                             | String (Raw BLOB)
`2#1000_1011` | _meta_::Value _size_::Number _data_::Octet\*               | String (Extension BLOB)
`2#1000_1100` | _length_::Number _size_::Number _data_::Octet\*            | String (UTF-8)
`2#1000_1101` | _length_::Number _size_::Number _data_::Octet\*            | String (UTF-8 +memo)
`2#1000_1110` | _index_::Octet                                             | String (memo reference)
`2#1000_1111` | -                                                          | `null`
`2#1001_xxxx` | -                                                          | negative small integer (-112..-97)
`2#101x_xxxx` | -                                                          | negative small integer (-96..-65)
`2#11xx_xxxx` | -                                                          | negative small integer (-64..-1)

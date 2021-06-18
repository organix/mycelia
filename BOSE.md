# Binary Octet-Stream Encoding (BOSE)

For more-efficient network transmission, we propose an octet-stream encoding which is isomorphic to JSON.
As with ASCII text, there are multiple ways to encode the same abstract JSON value.
All valid encodings produce equivalent JSON values, although some representation details are lost.
Every valid JSON value can be represented in BOSE.
Values may be round-tripped without semantic loss.
Arbitrarily large values are fully supported.
Decimal values can be represented exactly, without loss due to base-2 translation.

## Example

Consider the following JSON Object:
```
{
    "space" : {
        "origin" : [ -40, -20 ],
        "extent" : [ 600, 460 ]
    },
    "shapes" : [
        {
            "origin" : [ 5, 3 ],
            "extent" : [ 21, 13 ]
        },
        {
            "origin" : [ 8, 5 ],
            "extent" : [ 13, 8 ]
        }
    ]
}
```
One possible BOSE representation would be:
```
0000:  07 d0 82 0a 85 73 70 61  63 65 05 a0 0b 86 6f 72  |.....space....or|
0010:  69 67 69 6e 06 83 82 58  6c 0b 86 65 78 74 65 6e  |igin...Xl..exten|
0020:  74 06 89 82 10 02 58 02  10 02 cc 01 0a 86 73 68  |t.....X.......sh|
0030:  61 70 65 73 04 9c 05 8c  09 00 04 82 85 83 09 01  |apes............|
0040:  04 82 95 8d 05 8c 09 00  04 82 88 85 09 01 04 82  |................|
0050:  8d 88                                             |..              |
```
A corresponding structure in "C" could be:
```
#include "bose.h"
octet_t buf[] = {
    object_n, n_80, n_2,
        utf8, n_5, 's', 'p', 'a', 'c', 'e',
        object, n_32,
            utf8_mem, n_6, 'o', 'r', 'i', 'g', 'i', 'n',
            array_n, n_3, n_2,
                n_m40,
                n_m20,
            utf8_mem, n_6, 'e', 'x', 't', 'e', 'n', 't',
            array_n, n_9, n_2,
                p_int_0, n_2, 600 & 0xFF, 600 >> 8,
                p_int_0, n_2, 460 & 0xFF, 460 >> 8,
        utf8, n_6, 's', 'h', 'a', 'p', 'e', 's',
        array, n_28,
            object, n_12,
                mem_ref, 0, array, n_2, n_5, n_3,
                mem_ref, 1, array, n_2, n_21, n_13,
            object, n_12,
                mem_ref, 0, array, n_2, n_8, n_5,
                mem_ref, 1, array, n_2, n_13, n_8,
};
```

## Single-Octet Encodings

There are six types of abstract data values representable in JSON:
  * Null
  * Boolean
  * Number
  * String
  * Array
  * Object

A small set of special values are encoded directly in a single octet:

encoding     | hex | value
-------------|-----|----------------
`2#00000000` |`00` | `false`
`2#00000001` |`01` | `true`
`2#00000010` |`02` | `[]`
`2#00000011` |`03` | `{}`
`2#00001111` |`0F` | `""`
`2#10000000` |`80` | `0`
`2#11111111` |`FF` | `null`

Small integer values (from `-64` through `126`) are also encoded in a single octet:

encoding     | hex | value
-------------|-----|----------------
`2#01nnnnnn` |`40`..`7F`| `-64`..`-1`
`2#1nnnnnnn` |`80`..`FE`| `0`..`126`

The encoding of `0` falls naturally within this range.
Negative integers are represented in 2's-complement format (all-bits-set = `-1`).

## Multi-Octet Encodings

Extended values (Number, String, Array, and Object) occupy more than one octet:

encoding     | hex | value          | extension
-------------|-----|----------------|------------
`2#00000100` |`04` | `[`...`]`      | size::Number ::Value\*
`2#00000101` |`05` | `{`...`}`      | size::Number (name::String ::Value)\*
`2#00000110` |`06` | `[` _count_ `]`      | size::Number count::Number ::Value\*n
`2#00000111` |`07` | `{` _count_ `}`      | size::Number count::Number (name::String ::Value)\*n
`2#00001000` |`08` | Octet\*        | size::Number bytes::Octet\*
`2#00001001` |`09` | @ Memo#        | index::Octet
`2#0000101m` |`0A`..`0B`| UTF-8          | size::Number chars::Octet\*
`2#0000110m` |`0C`..`0D`| UTF-16         | size::Number (hi::Octet lo::Octet)\*
`2#00001110` |`0E` | encoded        | size::Number name::String data::Octet\*
`2#0001sppp` |`10`..`1F`| Integer &pad   | size::Number int::Octet\*
`2#0010sppp` |`20`..`2F`| Decimal &pad   | size::Number exp::Number int::Octet\*
`2#0011sppp` |`30`..`3F`| Based &pad     | size::Number base::Number exp::Number int::Octet\*

Extended values (except memo refs) contain a _size_ indicating how many octets the value occupies after the size,
or how many to skip if the value is ignored.

### Number

An extended Number may be an Integer (`2#0001sppp`), Decimal (`2#0010sppp`), or Based/Rational (`2#0011sppp`).
The _s_ field is the sign (`0` for positive, `1` for negative).
The _ppp_ field is the number of padding bits added to the MSB (`0` through `7`) to fill the final octet.
Padding bits match the sign.

encoding     | hex | value           | extension
-------------|-----|-----------------|------------
`2#00010ppp` |`10`..`17`| +Integer &pad    | size::Number int::Octet\*
`2#00011ppp` |`18`..`1F`| -Integer &pad    | size::Number int::Octet\*
`2#00100ppp` |`20`..`27`| +Decimal &pad    | size::Number exp::Number int::Octet\*
`2#00101ppp` |`28`..`2F`| -Decimal &pad    | size::Number exp::Number int::Octet\*
`2#00110ppp` |`30`..`37`| +Based &pad      | size::Number base::Number exp::Number int::Octet\*
`2#00111ppp` |`38`..`3F`| -Based &pad      | size::Number base::Number exp::Number int::Octet\*
`2#01nnnnnn` |`40`..`7F`| -small [-64..-1] | -
`2#1nnnnnnn` |`80`..`FE`| +small [0..126]  | -

The octets of the _int_ portion are stored LSB first, with the MSB padded as described above.
Negative _int_ values are represented in 2's-complement format (all-bits-set = `-1`).
Decimal values include an _exp_ field that scales the value by powers of `10`.
Based values include a _base_ field that specifies the base, as well as the _exp_ field.
The fully general formula for a Number is (_int_ × _base_ ^ _exp_).
The default _base_ is `10`.
The default _exp_ is `0`.
Note that exact Rational numbers can be represented using _base_ as the denominator with an _exp_ of `-1`.

#### Binary Data (recommendation)

There are two ways to encode raw binary data.
Either as an arbitrarily large Integer, or as a String of octets.
A bit-string of any length can be encoded as an Integer with `0` or `1` padding to fill out the final octet.
A byte-blob of any length can be encoded as a String of raw octets,
which are just Unicode code-points restricted to the range `0` through `255`.
Neither encoding requires any sort of quote/escape processing.

### String

An extended String begins with `2#00001eem` where _ee_ indicates encoding, and _m_ indicates memoization.

encoding     | hex | value          | extension
-------------|-----|----------------|------------
`2#00001000` |`08` | Octet\*        | size::Number bytes::Octet\*
`2#00001001` |`09` | @ Memo#        | index::Octet
`2#0000101m` |`0A`..`0B`| UTF-8          | size::Number chars::Octet\*
`2#0000110m` |`0C`..`0D`| UTF-16         | size::Number (hi::Octet lo::Octet)\*
`2#00001110` |`0E` | encoded        | size::Number name::String data::Octet\*
`2#00001111` |`0F` | `""`           | -

Next is the _size_ of the value in octets, as defined above.
Unless this is a memoized string reference (`2#00001001`),
in which case the next octet is an _index_ into the memoization table.
The memoization table is treated as a ring-buffer,
starting at `0` for each top-level Value in a stream.
Memoized strings are often used for Object property names.
For UTF-8 and UTF-16 values,
when the _m_-bit is `1` an entry is stored at the current memo index and the index is incremented,
wrapping around from `2#11111111` back to `2#00000000`.
If _eem_ is `2#110`,
the _size_ is followed by a String that names the encoding.
A decoder will reject an encoding it does not recognize.
If _ee_ is `2#10` the string value consists of octet-pairs,
encoding 16-bit values MSB-first (per [RFC 2781](https://tools.ietf.org/html/rfc2781)).
A UTF-16 encoded string value may begin with a byte-order-mark
(included in the _size_, of course, but not in the string value)
to signal MSB-first (`16#FEFF`) or LSB-first (`16#FFFE`) ordering of octets.

#### Capability (recommendation)

In applications that require the transmission of [_capabilities_](https://en.wikipedia.org/wiki/Capability-based_security),
a String may be used to represent capabilities in transit.
This requires some way to distinguish these capabilities from normal Strings.
Our recommendation is to mark each String with a distinguishing prefix.
For normal Strings, a [byte-order-mark](https://en.wikipedia.org/wiki/Byte_order_mark) is a safe choice.
For capability Strings, a [data-link-escape](https://en.wikipedia.org/wiki/C0_and_C1_control_codes)
(which is `2#00010000`, or `^P` in ASCII) can provide the desired semantics,
interpreting the rest of the String as binary-octet data.

### Array

An extended Array may (`2#00000110`), or may not (`2#00000100`), specify an element _count_.
However, a _size_ in octets is always provided for non-empty Arrays.

encoding     | hex | value          | extension
-------------|-----|----------------|------------
`2#00000010` |`02` | `[]`           | -
`2#00000100` |`04` | `[`...`]`      | size::Number ::Value\*
`2#00000110` |`06` | `[` _count_ `]`      | size::Number count::Number ::Value\*n

The end of the array is reached when then specified number of octets have been consumed,
which should correspond to decoding the matching count of elements (if specified).
A decoder may reject a mismatch.

### Object

An extended Object may (`2#00000111`), or may not (`2#00000101`), specify a property _count_.
However, a _size_ in octets is always provided for non-empty Objects.

encoding     | hex | value          | extension
-------------|-----|----------------|------------
`2#00000011` |`03` | `{}`           | -
`2#00000101` |`05` | `{`...`}`      | size::Number (name::String ::Value)\*
`2#00000111` |`07` | `{` _count_ `}`      | size::Number count::Number (name::String ::Value)\*n

Properties are encoded as a String (property name) followed by an encoded Value.
Note that the property name strings may be memoized,
reducing the octet-size of the object.
The end of the object is reached when then specified number of octets have been consumed,
which should correspond to decoding the matching count of properties (if specified).
A decoder may reject a mismatch.

## Encoding Matrix

The following table summarizes the meaning of the first octet in a Value:

Hi \ Lo   | `2#_000` | `2#_001` | `2#_010` | `2#_011` | `2#_100` | `2#_101` | `2#_110` | `2#_111`
----------|----------|----------|----------|----------|----------|----------|----------|----------
`2#00000_`| `false`  | `true`   | `[]`     | `{}`     |`[`...`]` |`{`...`}` |`[` _n_ `]`|`{` _n_ `}`
`2#00001_`| octets   | @ memo#  | UTF-8    | UTF-8*   | UTF-16   | UTF-16*  | encoded  | `""`
`2#00010_`| +int &0  | +int &1  | +int &2  | +int &3  | +int &4  | +int &5  | +int &6  | +int &7
`2#00011_`| -int &0  | -int &1  | -int &2  | -int &3  | -int &4  | -int &5  | -int &6  | -int &7
`2#00100_`| +dec &0  | +dec &1  | +dec &2  | +dec &3  | +dec &4  | +dec &5  | +dec &6  | +dec &7
`2#00101_`| -dec &0  | -dec &1  | -dec &2  | -dec &3  | -dec &4  | -dec &5  | -dec &6  | -dec &7
`2#00110_`| +base &0 | +base &1 | +base &2 | +base &3 | +base &4 | +base &5 | +base &6 | +base &7
`2#00111_`| -base &0 | -base &1 | -base &2 | -base &3 | -base &4 | -base &5 | -base &6 | -base &7
`2#01000_`| `-64`    | `-63`    | `-62`    | `-61`    | `-60`    | `-59`    | `-58`    | `-57`
`2#01001_`| `-56`    | `-55`    | `-54`    | `-53`    | `-52`    | `-51`    | `-50`    | `-49`
`2#01010_`| `-48`    | `-47`    | `-46`    | `-45`    | `-44`    | `-43`    | `-42`    | `-41`
`2#01011_`| `-40`    | `-39`    | `-38`    | `-37`    | `-36`    | `-35`    | `-34`    | `-33`
`2#01100_`| `-32`    | `-31`    | `-30`    | `-29`    | `-28`    | `-27`    | `-26`    | `-25`
`2#01101_`| `-24`    | `-23`    | `-22`    | `-21`    | `-20`    | `-19`    | `-18`    | `-17`
`2#01110_`| `-16`    | `-15`    | `-14`    | `-13`    | `-12`    | `-11`    | `-10`    | `-9`
`2#01111_`| `-8`     | `-7`     | `-6`     | `-5`     | `-4`     | `-3`     | `-2`     | `-1`
`2#10000_`| `0`      | `1`      | `2`      | `3`      | `4`      | `5`      | `6`      | `7`
`2#10001_`| `8`      | `9`      | `10`     | `11`     | `12`     | `13`     | `14`     | `15`
`2#10010_`| `16`     | `17`     | `18`     | `19`     | `20`     | `21`     | `22`     | `23`
`2#10011_`| `24`     | `25`     | `26`     | `27`     | `28`     | `29`     | `30`     | `31`
`2#10100_`| `32`     | `33`     | `34`     | `35`     | `36`     | `37`     | `38`     | `39`
`2#10101_`| `40`     | `41`     | `42`     | `43`     | `44`     | `45`     | `46`     | `47`
`2#10110_`| `48`     | `49`     | `50`     | `51`     | `52`     | `53`     | `54`     | `55`
`2#10111_`| `56`     | `57`     | `58`     | `59`     | `60`     | `61`     | `62`     | `63`
`2#11000_`| `64`     | `65`     | `66`     | `67`     | `68`     | `69`     | `70`     | `71`
`2#11001_`| `72`     | `73`     | `74`     | `75`     | `76`     | `77`     | `78`     | `79`
`2#11010_`| `80`     | `81`     | `82`     | `83`     | `84`     | `85`     | `86`     | `87`
`2#11011_`| `88`     | `89`     | `90`     | `91`     | `92`     | `93`     | `94`     | `95`
`2#11100_`| `96`     | `97`     | `98`     | `99`     | `100`    | `101`    | `102`    | `103`
`2#11101_`| `104`    | `105`    | `106`    | `107`    | `108`    | `109`    | `110`    | `111`
`2#11110_`| `112`    | `113`    | `114`    | `115`    | `116`    | `117`    | `118`    | `119`
`2#11111_`| `120`    | `121`    | `122`    | `123`    | `124`    | `125`    | `126`    | `null`

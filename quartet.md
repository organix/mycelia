# Quartet

There are many possible models for describing an actor's behavior.
One simple model is an [imperative](https://en.wikipedia.org/wiki/Imperative_programming)
stack-oriented machine with a dictionary
(similar to [FORTH](https://en.wikipedia.org/wiki/Forth_(programming_language))).

Program source is provided as a stream of _words_
(whitespace separated in text format).
If the word parses as a _number_
the value is pushed on the data _stack_.
Otherwise the word is looked up in the current _dictionary_.
If the associated value is a _block_ it is executed,
otherwise the value is pushed on the data _stack_.
The data _stack_ holds parameters for executing blocks
and their return values.
Some blocks also consume words from the source stream.

An actor's behavior is described with a _block_.
The message received by the actor is the contents of the data stack.
The `SEND` primitive sends the current stack contents,
clearing the stack.
Values may be saved in the dictionary
by binding them to a word.
All dictionary changes are local to the executing behavior.

## Implementation

Blocks are essentially
[functional closures](https://en.wikipedia.org/wiki/Closure_(computer_programming)).
Executing a word definition is like applying the function
to the parameters on the stack (consuming them).
Result values (if any) are left on the stack.

The data stack contains universal integer values,
usually interpreted as signed 2's-complement numbers.
Numeric operations do not overflow,
but rather wrap around forming a ring,
which may be interpreted as either signed or unsigned.
The number of bits is not specified.

The quartet program `TRUE 1 LSR DUP NOT . .`
prints the minimum and maximum signed values.

### Tagged Types

Program source is represented by only two types,
_Words_ and _Numbers_.
Values on the data stack can have richer types.
Stack values can be
_Words_, _Numbers_, _Blocks_, _Procedures_, and _Actors_
(_Boolean_ values are just _Numbers_).
Any value that can appear on the stack
can also be bound to a _Word_ in the dictionary.

## Primitive Dictionary

The following primitive definitions are assumed to be present in the initial dictionary.

### Actor Primitives

Input                | Operation       | Output                  | Description
---------------------|-----------------|-------------------------|------------
_block_              | `CREATE`        | _actor_                 | Create a new actor with _block_ behavior
..._message_ _actor_ | `SEND`          | &mdash;                 | Send _message_ to _actor_
_block_              | `BECOME`        | &mdash;                 | Replace current actor's behavior with _block_
&mdash;              | `SELF`          | _actor_                 | Push the current actor's address on the data stack
&mdash;              | `FAIL`          | &mdash;                 | Abort processing and revert to prior state

### Dictionary and Quoting

Input                | Operation       | Output                  | Description
---------------------|-----------------|-------------------------|------------
_value_              | `=` _word_      | &mdash;                 | Bind _value_ to _word_ in the current dictionary
&mdash;              | `'` _word_      | _word_                  | Push (literal) _word_ on the data stack
&mdash;              | `@` _word_      | _value_                 | Lookup _value_ bound to _word_ in the current dictionary
&mdash;              | `[` ... `]`     | _block_                 | Create block (quoted) value
[ ...                | `(` ... `)`     | [ ... _value_           | Immediate (unquoted) value

### Boolean Conditionals

Input                | Operation       | Output                  | Description
---------------------|-----------------|-------------------------|------------
&mdash;              | `TRUE`          | TRUE                    | All bits set (1)
&mdash;              | `FALSE`         | FALSE                   | All bits clear (0)
_value_              | `ZERO?`         | _bool_                  | `TRUE` if _value_ = 0; otherwise `FALSE`
_bool_               | `IF` [ ]        | &mdash;                 | Execute block based on condition
_bool_               | `IF-ELSE` [ ] [ ] | &mdash;               | Choose block based on condition
_bool_               | `WHILE` [ ]     | &mdash;                 | Execute block while condition holds

### Stack Manipulation

Input                | Operation       | Output                  | Description
---------------------|-----------------|-------------------------|------------
_v_                  | `DROP`          | &mdash;                 | Drop the top element
_v_                  | `DUP`           | _v_ _v_                 | Duplicate the top element (same as `1 PICK`)
_v_<sub>2</sub> _v_<sub>1</sub> | `SWAP` | _v_<sub>1</sub> _v_<sub>2</sub> | Swap the top two elements (same as `2 ROLL`)
_v_<sub>n</sub> ... _v_<sub>1</sub> _n_ | `PICK` | _v_<sub>n</sub> ... _v_<sub>1</sub> _v_<sub>n</sub> | Duplicate element _n_
_v_<sub>n</sub> ... _v_<sub>1</sub> _n_ | `ROLL` | _v_<sub>n-1</sub> ... _v_<sub>1</sub> _v_<sub>n</sub> | Rotate stack elements (negative for reverse)
&mdash;              | `DEPTH`         | _n_                     | Number of items on the data stack

### Numeric and Logical Operations

Input                | Operation       | Output                  | Description
---------------------|-----------------|-------------------------|------------
&mdash;              | `INF`           | INF                     | Infinity/Undefined (only MSB set)
_n_                  | `NEG`           | -_n_                    | Numeric negation (2's complement)
_n_ _m_              | `ADD`           | _n+m_                   | Numeric addition
_n_ _m_              | `SUB`           | _n-m_                   | Numeric subtraction
_n_ _m_              | `MUL`           | _n*m_                   | Numeric multiplication
_n_ _m_              | `DIVMOD`        | _q_ _r_                 | Numeric division/modulus
_n_ _m_              | `COMPARE`       | _n-m_                   | Compare numeric values
_n_                  | `LT?`           | _bool_                  | `TRUE` if _n_ < 0; otherwise `FALSE`
_n_                  | `EQ?`           | _bool_                  | `TRUE` if _n_ = 0; otherwise `FALSE`
_n_                  | `GT?`           | _bool_                  | `TRUE` if _n_ > 0; otherwise `FALSE`
_n_                  | `NOT`           | ~_n_                    | Bitwise not (inversion)
_n_ _m_              | `AND`           | _n_&_m_                 | Bitwise and
_n_ _m_              | `OR`            | _n_\|_m_                | Bitwise or
_n_ _m_              | `XOR`           | _n_^_m_                 | Bitwise xor
_n_ _m_              | `LSL`           | _n_<<_m_                | Logical shift left
_n_ _m_              | `LSR`           | _n_>>_m_                | Logical shift right
_n_ _m_              | `ASR`           | _n_>>_m_                | Arithmetic shift right (sign-extend)

### Direct Memory Access

Input                | Operation       | Output                  | Description
---------------------|-----------------|-------------------------|------------
_address_            | `?`             | _value_                 | Load _value_ from _address_
_value_ _address_    | `!`             | &mdash;                 | Store _value_ into _address_
_address_            | `??`            | _value_                 | Atomic load _value_ from volatile _address_
_value_ _address_    | `!!`            | &mdash;                 | Atomic store _value_ into volatile _address_


### Interactive Features

Input                | Operation       | Output                  | Description
---------------------|-----------------|-------------------------|------------
&mdash;              | `WORDS`         | &mdash;                 | Print list of defined words
_code_               | `EMIT`          | &mdash;                 | Print ascii character _code_
&mdash;              | `...`           | &mdash;                 | Print stack contents (non-destructive)
_value_              | `.?`            | &mdash;                 | Print internal representation of _value_
_value_              | `.`             | &mdash;                 | Print _value_

## Example

```
[ ] = sink_beh 
@ sink_beh CREATE = sink

[ # cust
  UART0_RXDATA ??  # read UART receive fifo status/data
  DUP LT? IF-ELSE [
    DROP
    SELF SEND  # retry
  ] [
    16#FF AND SWAP SEND
  ]
] DUP = serial_read_beh CREATE = serial_read

[ # cust octet
  UART0_TXDATA ??  # read UART transmit fifo status
  LT? IF-ELSE [
    SELF SEND  # retry
  ] [
    UART0_TXDATA !!  # write UART fifo data
    SELF SWAP SEND
  ]
] DUP = serial_write_beh CREATE = serial_write

[ # octet
  DUP = octet 16#0D COMPARE
  EQ? IF [
    ' Stop dispatcher SEND
  ]
  SELF octet serial_write SEND
  @ serial_busy_beh BECOME
] = serial_echo_beh
[ # $serial_write
  serial_write COMPARE
  EQ? IF [
    SELF serial_read SEND
    @ serial_echo_beh BECOME
  ]
] = serial_busy_beh
@ serial_echo_beh CREATE = serial_echo

[ # output
  = output
  [ # cust octet
    SWAP = cust
    SELF SWAP output SEND
    SELF cust SEND
    output empty-Q serial_buffer_beh BECOME
  ]
] = serial_empty_beh
[ # output queue
  = queue
  = output
  [ # $output | cust octet
    DUP output COMPARE
    EQ? IF-ELSE [ # $output
      DROP
      queue Q-empty IF-ELSE [
        output serial_empty_beh BECOME
      ] [
        queue Q-TAKE
        output SWAP serial_buffer_beh BECOME
        SELF SWAP SEND
      ]
    ] [ # cust octet
      queue Q-PUT
      output SWAP serial_buffer_beh BECOME
      SELF SWAP SEND
    ]
  ]
] = serial_buffer_beh
serial_write serial_empty_beh CREATE = serial_buffer

serial_echo serial_read SEND  # start echo listen-loop
```

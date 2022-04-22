# uFork (Actor Virtual Machine)

The key idea behind this Actor Virtual Machine is
interleaved execution of threaded instruction streams.
Instruction streams are not assumed to be
arranged in consecutive memory locations.
Instead, each instruction contains a "pointer"
to the subsequent instruction,
or multiple pointers in the case of conditionals, etc.

This is combined with a lightweight computational context
(such as IP+SP) that makes it efficient
to enqueue the context after each instruction.
Then the next context can be dequeued and executed.
Thus an arbitrary number of instruction streams can be executed,
interleaved at the instruction level.

## Primitives

There are several groups of _primitives_
the provide a minimal (or at least very small) set of concepts
on which very complex systems can be built.
For our purposes,
we find Actors, Lambda Calculus, and PEG Parsers
to be useful building-blocks.

### [Actors](http://www.dalnefre.com/wp/2020/01/requirements-for-an-actor-programming-language/)

We take a transactional view of _actors_,
where all the _effects_
caused by an actor's _behavior_
in response to an _event_
become visible at the same logical instant.

Actor primitives include:

  * SEND(_target_, _message_)
  * CREATE(_behavior_)
  * BECOME(_behavior_)
  * ABORT(_reason_)

### [Lambda-Calculus](http://www.dalnefre.com/wp/2010/08/evaluating-expressions-part-1-core-lambda-calculus/)

Technically, there is only one "type" in lambda-calculus, the _function_ type.
However, it is useful to think about lambda-calculus
in terms of primitive expressions.

Lambda-Calculus primitives include:

  * Constant
  * Variable
  * Lambda-Abstraction
  * Function-Application

#### [LISP/Scheme/Kernel](http://www.dalnefre.com/wp/2011/11/fexpr-the-ultimate-lambda/)

There is a long lineage of languages, starting with LISP,
that have a very lambda-calculus like feel to them.
Many of them depart from the pure-functional nature of lambda-calculus,
but the core evaluation scheme is functional,
based on a small set of primitives:

  * Constant (includes Nil)
  * Symbol
  * Pair

The mapping to lambda-calculus is fairly direct.
_Constants_ (any object that evaluates to itself) are obvious.
_Symbols_ normally represent _variables_,
although unevaluated they represent unique values.
_Pairs_ normally represent function-application,
where the head is the functional-abstraction to be applied
and the tail is the parameters to the function.
However, there are special-forms (like `lambda`) which,
when they appear in function-position,
operate on the unevaluated parameters.
This is how `lambda` is used to construct new functional-abstractions.

### [PEG Parsers](http://www.dalnefre.com/wp/2011/02/parsing-expression-grammars-part-1/)

_Parsing Expression Grammars_ (PEGs) are a powerful,
but simple, tool for describing unambiguous parsers.

PEG primitives include:

  * Empty
  * Match(_predicate_)
  * Or(_first_, _rest_)
  * And(_first_, _rest_)
  * Not(_pattern_)

A key feature of PEGs is that _Or_ implements _prioritized choice_,
which means that _rest_ is tried only if _first_ fails.
Suprisingly, there are no repetition expressions in the primitive set.
This is because they can be trivially defined in primitive terms.

Derived PEGs include:

  * Optional(_pattern_) = Or(_pattern_, Empty)
  * Plus(_pattern_) = And(_pattern_, Star(_pattern_))
  * Star(_pattern_) = Or(Plus(_pattern_), Empty)

It is clearly important to be able to express loops
(or recursive references) in the grammar.
This is also how references to non-terminals are supported,
usually via some late-bound named-reference.

## Representation

The primary internal data-structure in **uFork** consists of four integers.

 t        | x        | y        | z
----------|----------|----------|----------
proc/type | head/car | tail/cdr | link/next

### Virtual Machine

#### Data Structures

 Structure                            | Description
--------------------------------------|---------------------------------
{t:Pair_T, x:car, y:cdr}              | pair-lists of user data
{t:Free_T, z:next}                    | cell in the free-list
{t:Pair_T, x:item, y:rest}            | stack entry holding _item_
{t:IP, x:SP, y:EP, z:next}            | continuation queue entry
{t:Event_T, x:target, y:msg, z:next } | actor event queue entry
{t:Actor_T, x:beh, y:?, z:?}          | idle actor
{t:Actor_T, x:beh, y:events', z:beh'} | busy actor, intially {y:(), z:?}

#### Instructions

 Input          | Instruction                   | Output  | Description
----------------|-------------------------------|---------|------------------------------
_T_             | {t:VM_cell, x:1, y:_K_}       | _cell_  | create cell {t:_T_}
_T_ _X_         | {t:VM_cell, x:2, y:_K_}       | _cell_  | create cell {t:_T_, x:_X_}
_T_ _X_ _Y_     | {t:VM_cell, x:3, y:_K_}       | _cell_  | create cell {t:_T_, x:_X_, y:_Y_}
_T_ _X_ _Y_ _Z_ | {t:VM_cell, x:4, y:_K_}       | _cell_  | create cell {t:_T_, x:_X_, y:_Y_, z:_Z_}
_cell_          | {t:VM_get, x:T, y:_K_}        | _t_     | get _t_ from _cell_
_cell_          | {t:VM_get, x:X, y:_K_}        | _x_     | get _x_ from _cell_
_cell_          | {t:VM_get, x:Y, y:_K_}        | _y_     | get _y_ from _cell_
_cell_          | {t:VM_get, x:Z, y:_K_}        | _z_     | get _z_ from _cell_
_cell_ _T_      | {t:VM_set, x:T, y:_K_}        | _cell'_ | set _t_ to _T_ in _cell_
_cell_ _X_      | {t:VM_set, x:X, y:_K_}        | _cell'_ | set _x_ to _X_ in _cell_
_cell_ _Y_      | {t:VM_set, x:Y, y:_K_}        | _cell'_ | set _y_ to _Y_ in _cell_
_cell_ _Z_      | {t:VM_set, x:Z, y:_K_}        | _cell'_ | set _z_ to _Z_ in _cell_
... _tail_ _head_ | {t:VM_pair, x:_n_, y:_K_}   | _pair_  | create {t:Pair_T, x:_head_, y:_tail_} (_n_ times)
_pair_          | {t:VM_part, x:_n_, y:_K_}     | ... _tail_ _head_ | split _pair_ into _head_ and _tail_ (_n_ times)
&mdash;         | {t:VM_push, x:_value_, y:_K_} | _value_ | push literal _value_ on stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_depth, y:_K_} | _v_<sub>_n_</sub> ... _v_<sub>1</sub> _n_ | count items on stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_drop, x:_n_, y:_K_} | &mdash; | remove _n_ items from stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_pick, x:_n_, y:_K_} | _v_<sub>_n_</sub> ... _v_<sub>1</sub> _v_<sub>_n_</sub> | copy item _n_ to top of stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_dup, x:_n_, y:_K_} |_v_<sub>_n_</sub> ... _v_<sub>1</sub> _v_<sub>_n_</sub> ... _v_<sub>1</sub> | duplicate _n_ items on stack
_n_ _m_         | {t:VM_alu, x:ADD, y:_K_}      | _n_+_m_ | sum of _n_ and _m_
_n_ _m_         | {t:VM_alu, x:SUB, y:_K_}      | _n_-_m_ | difference of _n_ and _m_
_n_ _m_         | {t:VM_alu, x:MUL, y:_K_}      | _n_\*_m_ | product of _n_ and _m_
_m_             | {t:VM_eq, x:_n_, y:_K_}       | _bool_  | `TRUE` if _n_ == _m_, otherwise `FALSE`
_n_ _m_         | {t:VM_cmp, x:EQ, y:_K_}       | _bool_  | `TRUE` if _n_ == _m_, otherwise `FALSE`
_n_ _m_         | {t:VM_cmp, x:GE, y:_K_}       | _bool_  | `TRUE` if _n_ >= _m_, otherwise `FALSE`
_n_ _m_         | {t:VM_cmp, x:GT, y:_K_}       | _bool_  | `TRUE` if _n_ > _m_, otherwise `FALSE`
_n_ _m_         | {t:VM_cmp, x:LT, y:_K_}       | _bool_  | `TRUE` if _n_ < _m_, otherwise `FALSE`
_n_ _m_         | {t:VM_cmp, x:LE, y:_K_}       | _bool_  | `TRUE` if _n_ <= _m_, otherwise `FALSE`
_n_ _m_         | {t:VM_cmp, x:NE, y:_K_}       | _bool_  | `TRUE` if _n_ != _m_, otherwise `FALSE`
_bool_          | {t:VM_if, x:_T_, y:_F_}       | &mdash; | continue _F_ if `FALSE`, otherwise continue _T_
&mdash;         | {t:VM_msg, x:0, y:_K_}        | _msg_   | copy event message to stack
&mdash;         | {t:VM_msg, x:_i_, y:_K_}      | _msg_<sub>_i_</sub> | copy message item _i_ to stack
&mdash;         | {t:VM_msg, x:-_i_, y:_K_}     | _tail_<sub>_i_</sub> | copy message tail _i_ to stack
&mdash;         | {t:VM_self, y:_K_}            | _actor_ | push current _actor_ on stack
_msg_ _actor_   | {t:VM_send, x:0, y:_K_}       | &mdash; | send _msg_ to _actor_
_m_<sub>_n_</sub> ... _m_<sub>1</sub> _actor_ | {t:VM_send, x:_n_, y:_K_}   | &mdash; | send (_m_<sub>1</sub> ... _m_<sub>_n_</sub>) to _actor_
_beh_           | {t:VM_new, x:0, y:_K_}        | _actor_ | create new _actor_ with behavior _beh_
_v_<sub>1</sub> ... _v_<sub>_n_</sub> _beh_ | {t:VM_new, x:_n_, y:_K_} | _actor_ | create new _actor_ with (_v_<sub>1</sub> ... _v_<sub>_n_</sub> . _beh_)
_beh_           | {t:VM_beh, x:0, y:_K_}        | &mdash; | replace behavior with _beh_
_v_<sub>1</sub> ... _v_<sub>_n_</sub> _beh_ | {t:VM_beh, x:_n_, y:_K_} | &mdash; | replace behavior with (_v_<sub>1</sub> ... _v_<sub>_n_</sub> . _beh_)
_reason_        | {t:VM_end, x:ABORT}           | &mdash; | abort actor transaction with _reason_
&mdash;         | {t:VM_end, x:STOP}            | &mdash; | stop current continuation (thread)
&mdash;         | {t:VM_end, x:COMMIT}          | &mdash; | commit actor transaction
_char_          | {t:VM_putc, y:_K_}            | &mdash; | write _char_ to console
&mdash;         | {t:VM_getc, y:_K_}            | _char_  | read _char_ from console
_value_         | {t:VM_debug, x:_tag_, y:_K_}  | &mdash; | debug_print _tag_: _value_ to console

### Object Graph

```
e_queue: [head,tail]--------------------------+
          |                                   V
          +-->[Event,to,msg,next]---> ... -->[Event,to,msg,NIL]
                     |  |
                     |  +--> actor message content
                     V
                    [Actor,beh,?,?]
                           |
                           +--> actor behavior code (initial IP)

k_queue: [head,tail]--------------------+
          |                             V
          +-->[ip,sp,ep,kp]---> ... -->[ip,sp,ep,NIL]
               |  |  |
               |  |  +-->[Event,to,msg,NIL]
               |  |             |  |
               |  |             |  +--> ...
               |  |             V
               |  |            [Actor,beh,es',beh']
               |  V
               | [Pair,car,cdr,?]
               |       |   |
               |       |   +--> ... -->[Pair,car,NIL,?]
               |       V
               |      item
               V
              [EQ,0,k,?]
                    |
                    +--> [IF,t,f,?]
                             | |
                             | +--> ...
                             V
                             ...
```

### PEG Structures

Message to Grammar:
```
             custs    accum    in (or NIL)
message: --->[*|*]--->[*|*]--->[*|*]---> next
              |        |        |
              v        v        v
             [*|*]    value    token
             /   \
            v     v
           ok    fail
```

Reply to _ok_:
```
             accum    in (or NIL)
message: --->[*|*]--->[*|*]---> next
              |        |
              v        v
             value    token
```

Reply to _fail_:
```
             in (or NIL)
message: --->[*|*]---> next
              |
              v
             token
```

## Inspiration

 * [Parsing Expression Grammars: A Recognition-Based Syntactic Foundation](https://bford.info/pub/lang/peg.pdf)
   * [OMeta: an Object-Oriented Language for Pattern Matching](http://www.vpri.org/pdf/tr2007003_ometa.pdf)
   * [PEG-based transformer provides front-, middle and back-end stages in a simple compiler](http://www.vpri.org/pdf/tr2010003_PEG.pdf)
 * [SectorLISP](http://justine.lol/sectorlisp2/)
 * [Ribbit](https://github.com/udem-dlteam/ribbit)
   * [A Small Scheme VM, Compiler and REPL in 4K](https://www.youtube.com/watch?v=A3r0cYRwrSs)
 * [From Folklore to Fact: Comparing Implementations of Stacks and Continuations](https://par.nsf.gov/servlets/purl/10201136)
 * [Schism](https://github.com/schism-lang/schism)
 * [A Simple Scheme Compiler](https://www.cs.rpi.edu/academics/courses/fall00/ai/scheme/reference/schintro-v14/schintro_142.html#SEC271)

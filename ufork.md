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
  * Fail
  * Match(_predicate_)
  * Or(_first_, _rest_)
  * And(_first_, _rest_)
  * Not(_pattern_)

A key feature of PEGs is that _Or_ implements _prioritized choice_,
which means that _rest_ is tried only if _first_ fails.
Suprisingly, there are no repetition expressions in the primitive set.
This is because they can be trivially defined in primitive terms.

Derived PEGs include:

  * Opt(_pattern_) = Or(And(_pattern_, Empty), Empty)
  * Plus(_pattern_) = And(_pattern_, Star(_pattern_))
  * Star(_pattern_) = Or(Plus(_pattern_), Empty)
  * Seq(_p_<sub>1</sub>, ..., _p_<sub>_n_</sub>) = And(_p_<sub>1</sub>, ... And(_p_<sub>_n_</sub>, Empty) ...)
  * Alt(_p_<sub>1</sub>, ..., _p_<sub>_n_</sub>) = Or(_p_<sub>1</sub>, ... Or(_p_<sub>_n_</sub>, Fail) ...)

It is clearly important to be able to express loops
(or recursive references) in the grammar.
This is also how references to non-terminals are supported,
usually via some late-bound named-reference.

## Representation

The cell is the primary internal data-structure in **uFork**.
It consists of four integers.

 t        | x        | y        | z
----------|----------|----------|----------
proc/type | head/car | tail/cdr | link/next

The integers in each field encode three basic types of value,
based on their 2 MSBs.

2-MSB | Interpretation
------|---------------
2#00  | cell address (with fields {_t_, _x_, _y_, _z_})
2#01  | negative small integer (fixnum)
2#10  | positive small integer (fixnum)
2#11  | internal procedure/type (no fields)

### Virtual Machine

#### Data Structures

 Structure                              | Description
----------------------------------------|---------------------------------
{t:Pair_T, x:car, y:cdr}                | pair-lists of user data
{t:Free_T, z:next}                      | cell in the free-list
{t:Pair_T, x:item, y:rest}              | stack entry holding _item_
{t:IP, x:SP, y:EP, z:next}              | continuation queue entry
{t:Event_T, x:target, y:msg, z:next}    | actor event queue entry
{t:Actor_T, x:beh, y:?, z:?}            | idle actor
{t:Actor_T, x:beh, y:events', z:beh'}   | busy actor, intially {y:(), z:?}
{t:Symbol_T, x:hash, y:string, z:value} | immutable symbolic-name

#### Instructions

 Input            | Instruction                   | Output   | Description
------------------|-------------------------------|----------|------------------------------
_v_               | {t:VM_typeq, x:_T_, y:_K_}    | _bool_   | `TRUE` if _v_ has type _T_, otherwise `FALSE`
_T_               | {t:VM_cell, x:1, y:_K_}       | _cell_   | create cell {t:_T_}
_T_ _X_           | {t:VM_cell, x:2, y:_K_}       | _cell_   | create cell {t:_T_, x:_X_}
_T_ _X_ _Y_       | {t:VM_cell, x:3, y:_K_}       | _cell_   | create cell {t:_T_, x:_X_, y:_Y_}
_T_ _X_ _Y_ _Z_   | {t:VM_cell, x:4, y:_K_}       | _cell_   | create cell {t:_T_, x:_X_, y:_Y_, z:_Z_}
_cell_            | {t:VM_get, x:T, y:_K_}        | _t_      | get _t_ from _cell_
_cell_            | {t:VM_get, x:X, y:_K_}        | _x_      | get _x_ from _cell_
_cell_            | {t:VM_get, x:Y, y:_K_}        | _y_      | get _y_ from _cell_
_cell_            | {t:VM_get, x:Z, y:_K_}        | _z_      | get _z_ from _cell_
_cell_ _T_        | {t:VM_set, x:T, y:_K_}        | _cell'_  | set _t_ to _T_ in _cell_
_cell_ _X_        | {t:VM_set, x:X, y:_K_}        | _cell'_  | set _x_ to _X_ in _cell_
_cell_ _Y_        | {t:VM_set, x:Y, y:_K_}        | _cell'_  | set _y_ to _Y_ in _cell_
_cell_ _Z_        | {t:VM_set, x:Z, y:_K_}        | _cell'_  | set _z_ to _Z_ in _cell_
... _tail_ _head_ | {t:VM_pair, x:_n_, y:_K_}     | _pair_   | create {t:Pair_T, x:_head_, y:_tail_} (_n_ times)
_pair_            | {t:VM_part, x:_n_, y:_K_}     | ... _tail_ _head_ | split _pair_ into _head_ and _tail_ (_n_ times)
_pair_            | {t:VM_nth, x:_n_, y:_K_}      | _item_<sub>_n_</sub> | extract item _n_ from _pair_
_pair_            | {t:VM_nth, x:-_n_, y:_K_}     | _tail_<sub>_n_</sub> | extract tail _n_ from _pair_
&mdash;           | {t:VM_push, x:_value_, y:_K_} | _value_  | push literal _value_ on stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_depth, y:_K_} | _v_<sub>_n_</sub> ... _v_<sub>1</sub> _n_ | count items on stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_drop, x:_n_, y:_K_} | &mdash; | remove _n_ items from stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_pick, x:_n_, y:_K_} | _v_<sub>_n_</sub> ... _v_<sub>1</sub> _v_<sub>_n_</sub> | copy item _n_ to top of stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_dup, x:_n_, y:_K_} |_v_<sub>_n_</sub> ... _v_<sub>1</sub> _v_<sub>_n_</sub> ... _v_<sub>1</sub> | duplicate _n_ items on stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_roll, x:_n_, y:_K_} | _v_<sub>_n_-1</sub> ... _v_<sub>1</sub> _v_<sub>_n_</sub> | roll item _n_ to top of stack
_v_<sub>_n_</sub> ... _v_<sub>1</sub> | {t:VM_roll, x:-_n_, y:_K_} | _v_<sub>1</sub> _v_<sub>_n_</sub> ... _v_<sub>2</sub> | roll top of stack to item _n_
_n_               | {t:VM_alu, x:NOT, y:_K_}      | ~_n_     | bitwise not _n_
_n_ _m_           | {t:VM_alu, x:AND, y:_K_}      | _n_&_m_  | bitwise _n_ and _m_
_n_ _m_           | {t:VM_alu, x:OR, y:_K_}       | _n_\|_m_ | bitwise _n_ or _m_
_n_ _m_           | {t:VM_alu, x:XOR, y:_K_}      | _n_^_m_  | bitwise _n_ exclusive-or _m_
_n_ _m_           | {t:VM_alu, x:ADD, y:_K_}      | _n_+_m_  | sum of _n_ and _m_
_n_ _m_           | {t:VM_alu, x:SUB, y:_K_}      | _n_-_m_  | difference of _n_ and _m_
_n_ _m_           | {t:VM_alu, x:MUL, y:_K_}      | _n_\*_m_ | product of _n_ and _m_
_m_               | {t:VM_eq, x:_n_, y:_K_}       | _bool_   | `TRUE` if _n_ == _m_, otherwise `FALSE`
_n_ _m_           | {t:VM_cmp, x:EQ, y:_K_}       | _bool_   | `TRUE` if _n_ == _m_, otherwise `FALSE`
_n_ _m_           | {t:VM_cmp, x:GE, y:_K_}       | _bool_   | `TRUE` if _n_ >= _m_, otherwise `FALSE`
_n_ _m_           | {t:VM_cmp, x:GT, y:_K_}       | _bool_   | `TRUE` if _n_ > _m_, otherwise `FALSE`
_n_ _m_           | {t:VM_cmp, x:LT, y:_K_}       | _bool_   | `TRUE` if _n_ < _m_, otherwise `FALSE`
_n_ _m_           | {t:VM_cmp, x:LE, y:_K_}       | _bool_   | `TRUE` if _n_ <= _m_, otherwise `FALSE`
_n_ _m_           | {t:VM_cmp, x:NE, y:_K_}       | _bool_   | `TRUE` if _n_ != _m_, otherwise `FALSE`
_n_ _c_           | {t:VM_cmp, x:CLS, y:_K_}      | _bool_   | `TRUE` if _n_ in _c_, otherwise `FALSE`
_bool_            | {t:VM_if, x:_T_, y:_F_}       | &mdash;  | continue _F_ if `FALSE`, otherwise continue _T_
&mdash;           | {t:VM_msg, x:0, y:_K_}        | _msg_    | copy event message to stack
&mdash;           | {t:VM_msg, x:_n_, y:_K_}      | _msg_<sub>_n_</sub> | copy message item _n_ to stack
&mdash;           | {t:VM_msg, x:-_n_, y:_K_}     | _tail_<sub>_n_</sub> | copy message tail _n_ to stack
&mdash;           | {t:VM_self, y:_K_}            | _actor_  | push current _actor_ on stack
_msg_ _actor_     | {t:VM_send, x:0, y:_K_}       | &mdash;  | send _msg_ to _actor_
_m_<sub>_n_</sub> ... _m_<sub>1</sub> _actor_ | {t:VM_send, x:_n_, y:_K_}   | &mdash; | send (_m_<sub>1</sub> ... _m_<sub>_n_</sub>) to _actor_
_beh_             | {t:VM_new, x:0, y:_K_}        | _actor_  | create new _actor_ with behavior _beh_
_v_<sub>1</sub> ... _v_<sub>_n_</sub> _beh_ | {t:VM_new, x:_n_, y:_K_} | _actor_ | create new _actor_ with (_v_<sub>1</sub> ... _v_<sub>_n_</sub> . _beh_)
_beh_             | {t:VM_beh, x:0, y:_K_}        | &mdash;  | replace behavior with _beh_
_v_<sub>1</sub> ... _v_<sub>_n_</sub> _beh_ | {t:VM_beh, x:_n_, y:_K_} | &mdash; | replace behavior with (_v_<sub>1</sub> ... _v_<sub>_n_</sub> . _beh_)
_reason_          | {t:VM_end, x:ABORT}           | &mdash;  | abort actor transaction with _reason_
&mdash;           | {t:VM_end, x:STOP}            | &mdash;  | stop current continuation (thread)
&mdash;           | {t:VM_end, x:COMMIT}          | &mdash;  | commit actor transaction
_chars_           | {t:VM_cvt, x:LST_NUM, y:_K_}  | _fixnum_ | convert _chars_ to _fixnum_
_chars_           | {t:VM_cvt, x:LST_SYM, y:_K_}  | _symbol_ | convert _chars_ to _symbol_
_char_            | {t:VM_putc, y:_K_}            | &mdash;  | write _char_ to console
&mdash;           | {t:VM_getc, y:_K_}            | _char_   | read _char_ from console
_value_           | {t:VM_debug, x:_tag_, y:_K_}  | &mdash;  | debug_print _tag_: _value_ to console

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

## LISP/Scheme Ground Environment

  * `(quote `_expr_`)`
  * `(list . `_args_`)`
  * `(lambda `_formals_` . `_body_`)`
  * `(seq . `_body_`)`
  * `(define `_symbol_` `_value_`)`
  * `(cons `_head_` `_tail_`)`
  * `(car `_list_`)`
  * `(cdr `_list_`)`
  * `(cadr `_list_`)`
  * `(caddr `_list_`)`
  * `(nth `_index_` `_list_`)`
  * `(null? . `_values_`)`
  * `(pair? . `_values_`)`
  * `(boolean? . `_values_`)`
  * `(number? . `_values_`)`
  * `(symbol? . `_values_`)`
  * `(actor? . `_values_`)`
  * `(if `_bool_` `_consequence_` `_alternative_`)`
  * `(eq? . `_values_`)`
  * `peg-lang  ; REPL grammar`
  * `(quit)`

### Lambda Compilation Test-Cases

```
(define nop (lambda _))                         ; equivalent to _par_
(define zero (lambda _ 0))
(define nil (lambda _ ()))
(define ap (lambda x x))                        ; equivalent to _list_
(define id (lambda (x) x))
(define r1 (lambda (x . y) y))
(define i2 (lambda (x y) y))
(define r2 (lambda (x y . z) z))
(define i3 (lambda (x y z) z))
(define l3 (lambda (x y z) (list x y z)))
(define n1 (lambda (x) (car x)))                ; equivalent to _car_
(define n2 (lambda (x) (car (cdr x))))          ; equivalent to _cadr_
(define n3 (lambda (x) (car (cdr (cdr x)))))    ; equivalent to _caddr_
(define c (lambda (y) (lambda (x) (list y x))))
```

#### Lambda Compiled Code Structures

```
            [Event,tgt,msg]--->[Pair,car,cdr]--->[Pair,car,NIL]
                    |                 |                 |
                    |                 v                 v
        +-----------|--------------> cust              env <--------------+
        |           |                                   ^                 |
        |           v                                   |                 |
        |   [Pair,car,cdr]---> params <-----------------|---------+       |
        |          |                                    |         |       |
        |          v                                    |         |       |
        |         comb                                  |         |       |
        |           ^                                   |         |       |
        |           |                                   |         |       |
        |   [Event,tgt,msg]--->[Pair,car,cdr]--->[Pair,car,NIL]   |       |
        |                             |                           |       |
        |                             |                           |       |
        |   [Actor,ip]<---------------+                           |       |
        |           |                                             |       |
        +-----------|-----------------+                           |       |
                    |                 |                           |       |
                    v                 |                           |       |
            [PUSH,m,k]                |                 +---------+       |
                  | |                 |                 |                 |
                  +-|--------->[Pair,car,cdr]--->[Pair,car,cdr]--->[Pair,car,NIL]
                    |
                    v
K_CALL:     [MSG,+0,k]---+                       [Actor,ip]
                         |                               |
                         |                               v
RESEND:     [MSG,+0,k]   |                       [PUSH,f,k]
                    |    |                            /  |
                    v    |                           /   v
            [SELF,?,k]---+     [Actor,ip]<----------+   AP_FUNC_B
                         |             |
                         |             v
SELF_EVAL:  [SELF,?,k]   |     [PUSH,UNIT,k]
                    |    |                |
                    v    |                :
CUST_SEND:  [MSG,+1,k]<--|----------------+
                    |    |
                    v    |
SEND_0:     [SEND,0,k]<--+
                    |
                    v
COMMIT:     [END,+1,?]
```

#### Execution Statistics Test-Case

```
((lambda (x) x) (list 1 2 3))                   ; => (1 2 3)
```

Date       | Events | Instructions | Description
-----------|--------|--------------|-------------
2022-05-17 |   1609 |        16435 | baseline measurement
2022-05-18 |   1279 |        15005 | XLAT in G_SEXPR_X
2022-05-18 |   1159 |        14485 | XLAT in G_SEXPR_X and G_LIST_X
2022-05-18 |   1173 |        14676 | XLAT in G_FIXNUM and G_SYMBOL
2022-05-18 |   1117 |        13869 | replace SEQ with AND in G_SEXPR
2022-05-18 |   1203 |        15029 | parse QUOTE -> CONST_BEH
2022-05-21 |   1205 |        15039 | delegate to GLOBAL_ENV
2022-05-22 |   1205 |        15030 | lambda interpreter

## PEG Tools

  * `(peg-source `_list_`)`
  * `(peg-start `_peg_` `_src_`)`
  * `peg-empty`
  * `peg-fail`
  * `peg-any`
  * `(peg-eq `_token_`)`
  * `(peg-or `_first_` `_rest_`)`
  * `(peg-and `_first_` `_rest_`)`
  * `(peg-class . `_classes_`)`
  * `(peg-opt `_peg_`)`
  * `(peg-plus `_peg_`)`
  * `(peg-star `_peg_`)`
  * `(peg-alt . `_pegs_`)`
  * `(peg-seq . `_pegs_`)`
  * `(peg-call `_name_`)`
  * `(peg-pred `_pred_` `_peg_`)`
  * `(peg-xform `_appl_` `_peg_`)`
  * `(list->number `_chars_`)`
  * `(list->symbol `_chars_`)`
  * `a-print`
  * `peg-lang`

### PEG Test-Cases

```
(define src (peg-source (list 45 52 50 48)))  ; '-' '4' '2' '0'
(peg-start peg-any src)
(peg-start (peg-and peg-any peg-empty) src)
(peg-start (peg-or (peg-eq 45) peg-empty) src)
(peg-start (peg-and (peg-or (peg-eq 45) peg-empty) peg-any) src)
(peg-start (peg-and (peg-or (peg-eq 45) peg-empty) (peg-and peg-any peg-empty)) src)
(define peg-digit (peg-class DGT))
(peg-start (peg-and (peg-or (peg-eq 45) peg-empty) (peg-and peg-digit peg-empty)) src)
(define peg-all (peg-or (peg-and peg-any (peg-call peg-all)) peg-empty))
(peg-start peg-all src)
(define peg-digits (peg-or (peg-and peg-digit (peg-call peg-digits)) peg-empty))
(define peg-number (peg-and (peg-or (peg-eq 45) peg-empty) peg-digits))
(peg-start peg-number src)
(define src (peg-source (list 70 111 111 10)))  ; 'F' 'o' 'o' '\n'
(define peg-alnum (peg-plus (peg-class UPR LWR)))
(peg-start peg-alnum src)
(peg-start (peg-and (peg-opt (peg-eq 45)) (peg-star (peg-class DGT))) (peg-source (list 45 52 50 48 10)))
(define sxp-optws (peg-star (peg-alt (peg-eq 9) (peg-eq 10) (peg-eq 13) (peg-eq 32))))
(define sxp-atom (peg-and sxp-optws (peg-plus (peg-class UPR LWR DGT SYM))))
(define sxp-list (peg-seq (peg-eq 40) (peg-star sxp-atom) sxp-optws (peg-eq 41)))
(define src (peg-source (list 40 76 73 83 84 32 49 50 51 9 55 56 57 48 41 13 10)))  ; (LIST 123 7890)
(peg-start sxp-list src)
(define src (peg-source (list 40 67 65 82 32 40 32 76 73 83 84 32 48 32 49 41 9 41)))  ; (CAR (LIST 0 1))
(define scm-pos (peg-xform list->number (peg-plus (peg-class DGT))))
(define scm-neg (peg-xform list->number (peg-and (peg-eq 45) (peg-plus (peg-class DGT)))))
(define scm-num (peg-or scm-neg scm-pos))
(peg-start (peg-pred number? scm-num) (peg-source (list 48 10)))
```

### PEG Structures

Message to Grammar:
```
--->[custs,context]--->[accum,in]---> NIL or --->[token,next]--->
      |                  |                         |
      v                  v                         v
    [ok,fail]
     /    \
    v      v
```

Reply to _ok_ customer:
```
--->[accum,in]---> NIL or --->[token,next]--->
      |                         |
      v                         v
```

Reply to _fail_ customer:
```
NIL or --->[token,next]--->
             |
             v
```

### Character Classes

| ch | dec | hex | CTL | DGT | UPR | LWR | DLM | SYM | HEX | WSP |
|----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
| ^@ |   0 |  00 |  x  |     |     |     |     |     |     |     |
| ^A |   1 |  01 |  x  |     |     |     |     |     |     |     |
| ^B |   2 |  02 |  x  |     |     |     |     |     |     |     |
| ^C |   3 |  03 |  x  |     |     |     |     |     |     |     |
| ^D |   4 |  04 |  x  |     |     |     |     |     |     |     |
| ^E |   5 |  05 |  x  |     |     |     |     |     |     |     |
| ^F |   6 |  06 |  x  |     |     |     |     |     |     |     |
| ^G |   7 |  07 |  x  |     |     |     |     |     |     |     |
| ^H |   8 |  08 |  x  |     |     |     |     |     |     |     |
| ^I |   9 |  09 |  x  |     |     |     |     |     |     |  x  |
| ^J |  10 |  0a |  x  |     |     |     |     |     |     |  x  |
| ^K |  11 |  0b |  x  |     |     |     |     |     |     |  x  |
| ^L |  12 |  0c |  x  |     |     |     |     |     |     |  x  |
| ^M |  13 |  0d |  x  |     |     |     |     |     |     |  x  |
| ^N |  14 |  0e |  x  |     |     |     |     |     |     |     |
| ^O |  15 |  0f |  x  |     |     |     |     |     |     |     |
| ^P |  16 |  10 |  x  |     |     |     |     |     |     |     |
| ^Q |  17 |  11 |  x  |     |     |     |     |     |     |     |
| ^R |  18 |  12 |  x  |     |     |     |     |     |     |     |
| ^S |  19 |  13 |  x  |     |     |     |     |     |     |     |
| ^T |  20 |  14 |  x  |     |     |     |     |     |     |     |
| ^U |  21 |  15 |  x  |     |     |     |     |     |     |     |
| ^V |  22 |  16 |  x  |     |     |     |     |     |     |     |
| ^W |  23 |  17 |  x  |     |     |     |     |     |     |     |
| ^X |  24 |  18 |  x  |     |     |     |     |     |     |     |
| ^Y |  25 |  19 |  x  |     |     |     |     |     |     |     |
| ^Z |  26 |  1a |  x  |     |     |     |     |     |     |     |
| ^[ |  27 |  1b |  x  |     |     |     |     |     |     |     |
| ^\ |  28 |  1c |  x  |     |     |     |     |     |     |     |
| ^] |  29 |  1d |  x  |     |     |     |     |     |     |     |
| ^^ |  30 |  1e |  x  |     |     |     |     |     |     |     |
| ^_ |  31 |  1f |  x  |     |     |     |     |     |     |     |
|    |  32 |  20 |     |     |     |     |     |     |     |  x  |
| !  |  33 |  21 |     |     |     |     |     |  x  |     |     |
| "  |  34 |  22 |     |     |     |     |  x  |     |     |     |
| #  |  35 |  23 |     |     |     |     |     |  x  |     |     |
| $  |  36 |  24 |     |     |     |     |     |  x  |     |     |
| %  |  37 |  25 |     |     |     |     |     |  x  |     |     |
| &  |  38 |  26 |     |     |     |     |     |  x  |     |     |
| '  |  39 |  27 |     |     |     |     |  x  |     |     |     |
| (  |  40 |  28 |     |     |     |     |  x  |     |     |     |
| )  |  41 |  29 |     |     |     |     |  x  |     |     |     |
| \* |  42 |  2a |     |     |     |     |     |  x  |     |     |
| +  |  43 |  2b |     |     |     |     |     |  x  |     |     |
| ,  |  44 |  2c |     |     |     |     |  x  |     |     |     |
| -  |  45 |  2d |     |     |     |     |     |  x  |     |     |
| .  |  46 |  2e |     |     |     |     |     |  x  |     |     |
| /  |  47 |  2f |     |     |     |     |     |  x  |     |     |
| 0  |  48 |  30 |     |  x  |     |     |     |     |  x  |     |
| 1  |  49 |  31 |     |  x  |     |     |     |     |  x  |     |
| 2  |  50 |  32 |     |  x  |     |     |     |     |  x  |     |
| 3  |  51 |  33 |     |  x  |     |     |     |     |  x  |     |
| 4  |  52 |  34 |     |  x  |     |     |     |     |  x  |     |
| 5  |  53 |  35 |     |  x  |     |     |     |     |  x  |     |
| 6  |  54 |  36 |     |  x  |     |     |     |     |  x  |     |
| 7  |  55 |  37 |     |  x  |     |     |     |     |  x  |     |
| 8  |  56 |  38 |     |  x  |     |     |     |     |  x  |     |
| 9  |  57 |  39 |     |  x  |     |     |     |     |  x  |     |
| :  |  58 |  3a |     |     |     |     |     |  x  |     |     |
| ;  |  59 |  3b |     |     |     |     |  x  |     |     |     |
| <  |  60 |  3c |     |     |     |     |     |  x  |     |     |
| =  |  61 |  3d |     |     |     |     |     |  x  |     |     |
| >  |  62 |  3e |     |     |     |     |     |  x  |     |     |
| ?  |  63 |  3f |     |     |     |     |     |  x  |     |     |
| @  |  64 |  40 |     |     |     |     |     |  x  |     |     |
| A  |  65 |  41 |     |     |  x  |     |     |     |  x  |     |
| B  |  66 |  42 |     |     |  x  |     |     |     |  x  |     |
| C  |  67 |  43 |     |     |  x  |     |     |     |  x  |     |
| D  |  68 |  44 |     |     |  x  |     |     |     |  x  |     |
| E  |  69 |  45 |     |     |  x  |     |     |     |  x  |     |
| F  |  70 |  46 |     |     |  x  |     |     |     |  x  |     |
| G  |  71 |  47 |     |     |  x  |     |     |     |     |     |
| H  |  72 |  48 |     |     |  x  |     |     |     |     |     |
| I  |  73 |  49 |     |     |  x  |     |     |     |     |     |
| J  |  74 |  4a |     |     |  x  |     |     |     |     |     |
| K  |  75 |  4b |     |     |  x  |     |     |     |     |     |
| L  |  76 |  4c |     |     |  x  |     |     |     |     |     |
| M  |  77 |  4d |     |     |  x  |     |     |     |     |     |
| N  |  78 |  4e |     |     |  x  |     |     |     |     |     |
| O  |  79 |  4f |     |     |  x  |     |     |     |     |     |
| P  |  80 |  50 |     |     |  x  |     |     |     |     |     |
| Q  |  81 |  51 |     |     |  x  |     |     |     |     |     |
| R  |  82 |  52 |     |     |  x  |     |     |     |     |     |
| S  |  83 |  53 |     |     |  x  |     |     |     |     |     |
| T  |  84 |  54 |     |     |  x  |     |     |     |     |     |
| U  |  85 |  55 |     |     |  x  |     |     |     |     |     |
| V  |  86 |  56 |     |     |  x  |     |     |     |     |     |
| W  |  87 |  57 |     |     |  x  |     |     |     |     |     |
| X  |  88 |  58 |     |     |  x  |     |     |     |     |     |
| Y  |  89 |  59 |     |     |  x  |     |     |     |     |     |
| Z  |  90 |  5a |     |     |  x  |     |     |     |     |     |
| [  |  91 |  5b |     |     |     |     |  x  |     |     |     |
| \\ |  92 |  5c |     |     |     |     |     |  x  |     |     |
| ]  |  93 |  5d |     |     |     |     |  x  |     |     |     |
| ^  |  94 |  5e |     |     |     |     |     |  x  |     |     |
| \_ |  95 |  5f |     |     |     |     |     |  x  |     |     |
| \` |  96 |  60 |     |     |     |     |  x  |     |     |     |
| a  |  97 |  61 |     |     |     |  x  |     |     |  x  |     |
| b  |  98 |  62 |     |     |     |  x  |     |     |  x  |     |
| c  |  99 |  63 |     |     |     |  x  |     |     |  x  |     |
| d  | 100 |  64 |     |     |     |  x  |     |     |  x  |     |
| e  | 101 |  65 |     |     |     |  x  |     |     |  x  |     |
| f  | 102 |  66 |     |     |     |  x  |     |     |  x  |     |
| g  | 103 |  67 |     |     |     |  x  |     |     |     |     |
| h  | 104 |  68 |     |     |     |  x  |     |     |     |     |
| i  | 105 |  69 |     |     |     |  x  |     |     |     |     |
| j  | 106 |  6a |     |     |     |  x  |     |     |     |     |
| k  | 107 |  6b |     |     |     |  x  |     |     |     |     |
| l  | 108 |  6c |     |     |     |  x  |     |     |     |     |
| m  | 109 |  6d |     |     |     |  x  |     |     |     |     |
| n  | 110 |  6e |     |     |     |  x  |     |     |     |     |
| o  | 111 |  6f |     |     |     |  x  |     |     |     |     |
| p  | 112 |  70 |     |     |     |  x  |     |     |     |     |
| q  | 113 |  71 |     |     |     |  x  |     |     |     |     |
| r  | 114 |  72 |     |     |     |  x  |     |     |     |     |
| s  | 115 |  73 |     |     |     |  x  |     |     |     |     |
| t  | 116 |  74 |     |     |     |  x  |     |     |     |     |
| u  | 117 |  75 |     |     |     |  x  |     |     |     |     |
| v  | 118 |  76 |     |     |     |  x  |     |     |     |     |
| w  | 119 |  77 |     |     |     |  x  |     |     |     |     |
| x  | 120 |  78 |     |     |     |  x  |     |     |     |     |
| y  | 121 |  79 |     |     |     |  x  |     |     |     |     |
| z  | 122 |  7a |     |     |     |  x  |     |     |     |     |
| {  | 123 |  7b |     |     |     |     |  x  |     |     |     |
| \| | 124 |  7c |     |     |     |     |  x  |     |     |     |
| }  | 125 |  7d |     |     |     |     |  x  |     |     |     |
| ~  | 126 |  7e |     |     |     |     |     |  x  |     |     |
| ^? | 127 |  7f |  x  |     |     |     |     |     |     |     |

### PEG for LISP/Scheme

```
(define not-eol
  (lambda (x) (if (eq? x 10) #f #t)))
(define scm-comment
  (peg-seq (peg-eq 59) (peg-star (peg-pred not-eol peg-any)) (peg-eq 10)))
(define scm-wsp
  (peg-star (peg-or scm-comment (peg-class WSP)) ))
(define scm-symbol
  (peg-xform list->symbol
    (peg-plus (peg-class UPR LWR DGT SYM)) ))
(define scm-u-num
  (peg-plus (peg-class DGT)))
(define scm-s-num
  (peg-and (peg-or (peg-eq 45) (peg-eq 43)) scm-u-num))
(define scm-number
  (peg-xform list->number
    (peg-or scm-s-num scm-u-num)))
(define scm-quote
  (peg-xform (lambda x (list (quote quote) (nth -1 x)))
    (peg-and (peg-eq 39) (peg-call scm-expr)) ))
(define scm-literal
  (peg-alt
    (peg-xform (lambda _ #f) (peg-seq 35 102))
    (peg-xform (lambda _ #t) (peg-seq 35 116))
    (peg-xform (lambda _ #?) (peg-seq 35 63)) ))
(define scm-tail
  (peg-alt
    (peg-xform (lambda x (cons (nth 1 x) (nth 5 y)))
      (peg-seq (peg-call scm-sexpr) scm-wsp (peg-eq 46) scm-wsp (peg-call scm-sexpr)))
    (peg-and (peg-call scm-sexpr) (peg-call scm-tail))
    peg-empty))
(define scm-list
  (peg-xform cadr
    (peg-seq (peg-eq 40) scm-tail scm-wsp (peg-eq 41)) ))
(define scm-expr
  (peg-alt scm-list scm-literal scm-quote scm-number scm-symbol))
(define scm-sexpr
  (peg-xform cdr
    (peg-and scm-wsp scm-expr)))
```

## Inspiration

  * [Parsing Expression Grammars: A Recognition-Based Syntactic Foundation](https://bford.info/pub/lang/peg.pdf)
    * [OMeta: an Object-Oriented Language for Pattern Matching](http://www.vpri.org/pdf/tr2007003_ometa.pdf)
    * [PEG-based transformer provides front-, middle and back-end stages in a simple compiler](http://www.vpri.org/pdf/tr2010003_PEG.pdf)
  * [SectorLISP](http://justine.lol/sectorlisp2/)
  * [Ribbit](https://github.com/udem-dlteam/ribbit)
    * [A Small Scheme VM, Compiler and REPL in 4K](https://www.youtube.com/watch?v=A3r0cYRwrSs)
  * [Schism](https://github.com/schism-lang/schism)
  * [A Simple Scheme Compiler](https://www.cs.rpi.edu/academics/courses/fall00/ai/scheme/reference/schintro-v14/schintro_142.html#SEC271)

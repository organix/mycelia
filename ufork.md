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
{t:Actor_T, x:beh, y:sp, z:?}           | idle actor
{t:Actor_T, x:beh', y:sp', z:events}    | busy actor, intially {z:()}
{t:Symbol_T, x:hash, y:string, z:value} | immutable symbolic-name
{t:Fexpr_T, x:actor, y:?, z:?}          | interpreter (cust ast env)

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
&mdash;           | {t:VM_end, x:RELEASE}         | &mdash;  | commit transaction and free actor
_rawint_          | {t:VM_cvt, x:INT_FIX, y:_K_}  | _fixnum_ | convert _rawint_ to _fixnum_
_fixnum_          | {t:VM_cvt, x:FIX_INT, y:_K_}  | _rawint_ | convert _fixnum_ to _rawint_
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
                    [Actor,beh,sp,?]
                           |   |
                           |   +--> actor state (initial SP)
                           |
                           +--> actor behavior (initial IP)

k_queue: [head,tail]--------------------+
          |                             V
          +-->[ip,sp,ep,kp]---> ... -->[ip,sp,ep,NIL]
               |  |  |
               |  |  +-->[Event,to,msg,NIL]
               |  |             |  |
               |  |             |  +--> ...
               |  |             V
               |  |            [Actor,beh',sp',events]---> ... -->[Event,to,msg,NIL]
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

### Common Code Structures

```
K_CALL:     [MSG,+0,k]---+
                         |
                         |
RESEND:     [MSG,+0,k]   |
                    |    |
                    v    |
            [SELF,?,k]---+
                         |
                         |
SELF_EVAL:  [SELF,?,k]   |
                    |    |
                    v    |
CUST_SEND:  [MSG,+1,k]   |
                    |    |
                    v    |
SEND_0:     [SEND,0,k]<--+    RELEASE_0:  [SEND,0,k]
                    |                             |
                    v                             v
COMMIT:     [END,+1,?]        RELEASE:    [END,+2,?]
```

## LISP/Scheme Ground Environment

### Primitive Procedures (and values)

  * `empty-env`
  * `global-env`
  * `(quote `_expr_`)`
  * `(list . `_values_`)`
  * `(lambda `_formals_` . `_body_`)`
  * `(par .  `_exprs_`)`
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
  * `(if `_test_` `_consequence_` `_alternative_`)`
  * `(cond (`_test_` `_expr_`) . `_clauses_`)`
  * `(eq? . `_values_`)`
  * `(= . `_numbers_`)`
  * `(< . `_numbers_`)`
  * `(<= . `_numbers_`)`
  * `(+ . `_numbers_`)`
  * `(- . `_numbers_`)`
  * `(* . `_numbers_`)`
  * `peg-lang  ; REPL grammar`
  * `(eval `_expr_` . `_optenv_`)`
  * `(apply `_proc_` `_args_` . `_optenv_`)`
  * `(quit)`

### Derived Procedures

  * `(caar `_list_`)`
  * `(cdar `_list_`)`
  * `(cddr `_list_`)`
  * `(cadddr `_list_`)`
  * `(length `_list_`)`
  * `(list* `_value_` . `_values_`)`

### Lambda Compilation Test-Cases

```
(define par (lambda _))
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
(define length (lambda (p) (if (pair? p) (+ (length (cdr p)) 1) 0)))
(define s2 (lambda (x y) x y))
```

### Execution Statistics Test-Case

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
2022-05-25 |   1040 |        12911 | enhanced built-in parser
2022-05-30 |   1228 |        15259 | full-featured built-in parser
2022-06-03 |   1194 |        15062 | meta-circular interpreter
2022-06-04 |   1226 |        14986 | set SP in BECOME
2022-06-04 |   1226 |        14170 | set SP in CREATE
2022-06-05 |   1226 |        13867 | use RELEASE and RELEASE_0

#### Bootstrap Library

Date       | Events | Instructions | Description
-----------|--------|--------------|-------------
2022-06-07 |   7123 |        82277 | baseline measurement
2022-06-09 |   7083 |        82342 | M_EVAL pruned `apply`
2022-06-10 |   9360 |       108706 | M_EVAL pruned `eval`
2022-06-11 |   9697 |       113301 | parse "\_" as Symbol_T
2022-06-12 |   9697 |       113301 | `lambda` body is `seq`
2022-06-12 |  10351 |       120910 | `evlis` is `par`

Date       | Events | Instructions | Description
-----------|--------|--------------|-------------
2022-06-07 |   1151 |        13092 | (testcase - baseline)
2022-06-09 |   1127 |        13057 | M_EVAL pruned `apply`
2022-06-10 |   1133 |        13055 | M_EVAL pruned `eval`
2022-06-11 |   1175 |        13629 | parse "\_" as Symbol_T
2022-06-12 |   1177 |        13652 | `lambda` body is `seq`
2022-06-12 |   1201 |        13842 | `evlis` is `par`

## PEG Tools

  * `(peg-source `_list_`)`
  * `(peg-start `_peg_` `_src_`)`
  * `(peg-chain `_peg_` `_src_`)`
  * `peg-empty`
  * `peg-fail`
  * `peg-any`
  * `(peg-eq `_token_`)`
  * `(peg-or `_first_` `_rest_`)`
  * `(peg-and `_first_` `_rest_`)`
  * `(peg-not `_peg_`)`
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

### PEG Derivations

  * `(define peg-end (peg-not peg-any))  ; end of input`
  * `(define peg-peek (lambda (ptrn) (peg-not (peg-not ptrn))))  ; positive lookahead`
  * `(define peg-ok? (lambda (x) (if (pair? x) (if (actor? (cdr x)) #f #t) #f)))`
  * `(define peg-value (lambda (x) (if (pair? x) (if (actor? (cdr x)) #? (car x)) #?)))`

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

### LISP/Scheme Grammar

```
(define lex-eol (peg-eq 10))  ; end of line
(define lex-optwsp (peg-star (peg-class WSP)))
(define scm-to-eol (peg-or lex-eol (peg-and peg-any (peg-call scm-to-eol))))
(define scm-comment (peg-and (peg-eq 59) scm-to-eol))
(define scm-optwsp (peg-star (peg-or scm-comment (peg-class WSP))))
```

```
(define lex-eot (peg-not (peg-class DGT UPR LWR SYM)))  ; end of token
(define scm-ignore (peg-xform (lambda _ '_) (peg-and (peg-plus (peg-eq 95)) lex-eot)))
(define scm-const (peg-xform cadr (peg-seq
  (peg-eq 35)
  (peg-alt
    (peg-xform (lambda _ #f) (peg-eq 102))
    (peg-xform (lambda _ #t) (peg-eq 116))
    (peg-xform (lambda _ #?) (peg-eq 63))
    (peg-xform (lambda _ #unit) (peg-seq (peg-eq 117) (peg-eq 110) (peg-eq 105) (peg-eq 116))))
  lex-eot)))
```

```
(define lex-sign (peg-or (peg-eq 45) (peg-eq 43)))  ; [-+]
(define lex-digit (peg-or (peg-class DGT) (peg-eq 95)))  ; [0-9_]
(define lex-digits (peg-xform car (peg-and (peg-plus lex-digit) lex-eot)))
(define lex-number (peg-xform list->number (peg-or (peg-and lex-sign lex-digits) lex-digits)))
```

```
(define scm-symbol (peg-xform list->symbol (peg-plus (peg-class DGT UPR LWR SYM))))
(define scm-quoted (peg-xform (lambda (x) (list quote (cdr x)))
  (peg-and (peg-eq 39) (peg-call scm-expr))))

(define scm-dotted (peg-xform caddr
  (peg-seq scm-optwsp (peg-eq 46) (peg-call scm-sexpr) scm-optwsp (peg-eq 41))))
(define scm-tail (peg-xform cdr (peg-and
  scm-optwsp
  (peg-or
    (peg-xform (lambda _ ()) (peg-eq 41))
    (peg-and
      (peg-call scm-expr)
      (peg-or scm-dotted (peg-call scm-tail)) )) )))
(define scm-list (peg-xform cdr (peg-and (peg-eq 40) scm-tail)))
(define scm-expr (peg-alt scm-list scm-ignore scm-const lex-number scm-symbol scm-quoted))
(define scm-sexpr (peg-xform cdr (peg-and scm-optwsp scm-expr)))

;(define src (peg-source '(9 40 97 32 46 32 98 41 10)))  ; "\t(a . b)\n"
;(define rv (peg-start scm-sexpr src))
```

### PEG Test-Cases

```
(define src (peg-source (list 45 52 50 48)))  ; "-420"
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

(define src (peg-source (list 70 111 111 10)))  ; "Foo\n"
(define peg-alnum (peg-plus (peg-class UPR LWR)))
(peg-start peg-alnum src)
(peg-start (peg-and (peg-opt (peg-eq 45)) (peg-star (peg-class DGT))) (peg-source (list 45 52 50 48 10)))

(define sxp-optws (peg-star (peg-alt (peg-eq 9) (peg-eq 10) (peg-eq 13) (peg-eq 32))))
(define sxp-atom (peg-and sxp-optws (peg-plus (peg-class UPR LWR DGT SYM))))
(define sxp-list (peg-seq (peg-eq 40) (peg-star sxp-atom) sxp-optws (peg-eq 41)))
(define src (peg-source (list 40 76 73 83 84 32 49 50 51 32 55 56 57 48 41 13 10)))  ; "(LIST 123 7890)"
;(define src (peg-source (list 40 67 65 82 32 40 32 76 73 83 84 32 48 32 49 41 9 41)))  ; "(CAR ( LIST 0 1)\t)"
(peg-start sxp-list src)

(define scm-pos (peg-xform list->number (peg-plus (peg-class DGT))))
(define scm-neg (peg-xform list->number (peg-and (peg-eq 45) (peg-plus (peg-class DGT)))))
;(define scm-num (peg-xform car (peg-and (peg-or scm-neg scm-pos) (peg-eq 10))))
;(define scm-num (peg-xform car (peg-and (peg-or scm-neg scm-pos) (peg-not peg-any))))
;(define scm-num (peg-xform car (peg-and (peg-or scm-neg scm-pos) (peg-class UPR LWR SYM))))
(define scm-num (peg-xform car (peg-and (peg-or scm-neg scm-pos) (peg-not (peg-class UPR LWR SYM)))))
;(define scm-num (peg-xform car (peg-and (peg-plus (peg-class DGT)) (peg-not (peg-class UPR LWR SYM)))))
;(define scm-num (peg-xform car (peg-and (peg-plus (peg-class DGT)) (peg-not (peg-class LWR)))))
;(define scm-num (peg-xform car (peg-and (peg-plus (peg-class DGT)) (peg-not (peg-class WSP)))))
;(define scm-num (peg-and (peg-plus (peg-class DGT)) (peg-not (peg-class WSP))))
;(define scm-num (peg-and (peg-plus (peg-class DGT)) (peg-not (peg-eq 10))))
;(define scm-num (peg-and (peg-class DGT) (peg-not (peg-eq 10))))
;(define scm-num (peg-and (peg-eq 48) (peg-not (peg-eq 10))))
(peg-start scm-num (peg-source (list 49 115 116 10)))  ; "1st\n"
;(peg-start (peg-pred number? scm-num) (peg-source (list 48 10)))  ; "0\n"
(peg-start scm-num (peg-source (list 48 10)))  ; "0\n"
;(peg-start (peg-and (peg-eq 48) (peg-eq 10)) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) (peg-eq 13)) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) peg-any) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) peg-empty) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) peg-fail) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) (peg-not peg-any)) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) (peg-not peg-empty)) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) (peg-not peg-fail)) (peg-source (list 48 10)))
;(peg-start (peg-and (peg-eq 48) (peg-not (peg-eq 13))) (peg-source (list 48 10)))
(peg-start (peg-and (peg-eq 48) (peg-not (peg-eq 10))) (peg-source (list 48 10)))
;(peg-start (peg-not (peg-eq 13)) (peg-source (list 48 10)))
;(peg-start (peg-not (peg-eq 48)) (peg-source (list 48 10)))

(peg-start peg-end (peg-source (list)))
(peg-start peg-end (peg-source (list 32)))
(peg-start peg-end (peg-source (list 10)))
(peg-start peg-end (peg-source (list 32 10)))

(peg-start peg-any (peg-source (list)))
(peg-start peg-any (peg-source (list 32)))
(peg-start peg-any (peg-source (list 10)))
(peg-start peg-any (peg-source (list 32 10)))

(peg-start (peg-eq 32) (peg-source (list)))
(peg-start (peg-eq 32) (peg-source (list 32)))
(peg-start (peg-eq 32) (peg-source (list 10)))
(peg-start (peg-eq 32) (peg-source (list 32 10)))

(peg-start (peg-not (peg-eq 32)) (peg-source (list)))
(peg-start (peg-not (peg-eq 32)) (peg-source (list 32)))
(peg-start (peg-not (peg-eq 32)) (peg-source (list 10)))
(peg-start (peg-not (peg-eq 32)) (peg-source (list 32 10)))

(peg-start (peg-peek (peg-eq 32)) (peg-source (list)))
(peg-start (peg-peek (peg-eq 32)) (peg-source (list 32)))
(peg-start (peg-peek (peg-eq 32)) (peg-source (list 10)))
(peg-start (peg-peek (peg-eq 32)) (peg-source (list 32 10)))

(define src (peg-source (list 57 13 10)))  ; "9\r\n"
(peg-start (peg-and (peg-class DGT) (peg-class WSP)) (peg-chain peg-any src))

(define src (peg-source (list 9 32 49 32 50 51 32 52 53 54 32 55 56 57 48 13 10)))  ; "\t 1 23 456 7890\r\n"
;(define wsp-number (peg-xform cdr (peg-and (peg-star (peg-class WSP)) (peg-plus (peg-class DGT))) ))
;(peg-start (peg-plus (peg-xform list->number wsp-number)) src)
;(define lang-numbers (peg-plus (peg-xform list->number peg-any)))
(define wsp-token (peg-xform cdr
  (peg-and (peg-star (peg-class CTL WSP)) (peg-or (peg-class DLM) (peg-plus (peg-class DGT UPR LWR SYM)))) ))
(define lang-tokens (peg-plus peg-any))
;(peg-start lang-numbers (peg-chain wsp-number src))
(peg-start lang-tokens (peg-chain wsp-token src))
```

```
(define src (peg-source (list 9 32 59 32 120 13 10 121)))  ; "\t ; x\r\n y"
(define not-eol (lambda (x) (if (eq? x 10) #f #t)))
(define scm-comment (peg-seq (peg-eq 59) (peg-star (peg-pred not-eol peg-any)) (peg-eq 10)))
(define scm-wsp (peg-star (peg-or scm-comment (peg-class WSP)) ))
(define scm-symbol (peg-xform list->symbol (peg-plus (peg-class UPR LWR DGT SYM)) ))
(define scm-sexpr (peg-xform cdr (peg-and scm-wsp scm-symbol)))
(peg-start scm-sexpr src)
```

```
(define src (peg-source (list 39 97 98 10)))  ; "'ab\n"
(define scm-quote
  (peg-xform (lambda (x) (list (quote quote) (cdr x)))
    (peg-and (peg-eq 39) (peg-call scm-expr)) ))
(define scm-expr (peg-xform list->symbol (peg-plus (peg-class UPR LWR DGT SYM))))
(peg-start scm-quote src)
```

```
(define src (peg-source (list 40 97 32 46 32 98 41 10)))  ; "(a . b)\n"
(define scm-wsp (peg-star (peg-class WSP)))
(define scm-symbol (peg-xform list->symbol (peg-plus (peg-class UPR LWR DGT SYM))))
;(define scm-tail (peg-alt (peg-and (peg-call scm-sexpr) (peg-call scm-tail)) peg-empty))
(define scm-tail (peg-alt
  (peg-xform (lambda (x) (cons (nth 1 x) (nth 5 x)))
    (peg-seq (peg-call scm-sexpr) scm-wsp (peg-eq 46) scm-wsp (peg-call scm-sexpr)))
  (peg-and (peg-call scm-sexpr) (peg-call scm-tail))
  peg-empty))
(define scm-list (peg-xform cadr (peg-seq (peg-eq 40) (peg-call scm-tail) scm-wsp (peg-eq 41))))
(define scm-expr (peg-alt scm-list scm-symbol))
(define scm-sexpr (peg-xform cdr (peg-and scm-wsp scm-expr)))
(peg-start scm-sexpr src)
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


| ch | dec | hex | CTL | DGT | UPR | LWR | DLM | SYM | HEX | WSP |
|----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
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


| ch | dec | hex | CTL | DGT | UPR | LWR | DLM | SYM | HEX | WSP |
|----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
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


| ch | dec | hex | CTL | DGT | UPR | LWR | DLM | SYM | HEX | WSP |
|----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
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


## Meta-circular LISP Interpreter

The `META_EVALUATE` compile-time feature switch
enables an assembly-coded implementation
of a McCarthy-style meta-circular LISP interpreter.
The algorithm is based on the listing on page 13 of
"The LISP 1.5 Programmer's Manual".

```
eval[e;a] =
    [atom[e] → cdr[assoc[e;a]];
     atom[car[e]] →
             [eq[car[e];QUOTE] → cadr[e];
              eq[car[e];COND] → evcon[cdr[e];a];
              T → apply[car[e];evlis[cdr[e];a];a]];
     T → apply[car[e];evlis[cdr[e];a];a]] 
apply[fn;x;a] =
     [atom[fn] → [eq[fn;CAR] → caar[x];
                  eq[fn;CDR] → cdar[x];
                  eq[fn;CONS] → cons[car[x]; cadr[x]];
                  eq[fn;ATOM] → atom[car[x]];
                  eq[fn;EQ] → eq[car[x];cadr[x]];
                  T → apply[eval[fn;a];x;a]];
      eq[car[fn];LAMBDA] →
                  eval[caddr[fn];pairlis[cadr[fn];x;a]];
      eq[car[fn];LABEL] →
                  apply[caddr[fn];x;cons[cons[cadr[fn];caddr[fn]];a]]]
```

A LISP rendition of the assembly-coded implementation
might look like this:

```
(define eval
  (lambda (form env)
    (if (symbol? form)
      (lookup form env)                 ; bound variable
      (if (pair? form)
        (if (eq? (car form) 'quote)     ; (quote <form>)
          (cadr form)
          (if (eq? (car form) 'if)      ; (if <pred> <cnsq> <altn>)
            (evalif (eval (cadr form) env) (caddr form) (cadddr form) env)
            (apply (car form) (evlis (cdr form) env) env)))
        form))))                        ; self-evaluating form

(define apply
  (lambda (fn args env)
    (if (symbol? fn)
      (if (eq? fn 'list)                ; (list . <args>)
        args
        (if (eq? fn 'cons)              ; (cons <first> <rest>)
          (cons (car args) (cadr args))
          (if (eq? fn 'car)             ; (car <pair>)
            (caar args)
            (if (eq? fn 'cdr)           ; (cdr <pair>)
              (cdar args)
              (if (eq? fn 'eq?)         ; (eq? <left> <right>)
                (eq? (car args) (cadr args))
                (if (eq? fn 'pair?)     ; (pair? <value>)
                  (pair? (car args))
                  (if (eq? fn 'symbol?) ; (symbol? <value>)
                    (symbol? (car args))
                    (apply (lookup fn env) args env))))))))
      (if (pair? fn)
        (if (eq? (car fn) 'lambda)      ; ((lambda <frml> <body>) <args>)
          (eval (caddr fn) (zip (cadr fn) args env))
          (apply (eval fn env) args env))
        #?)))

(define lookup                          ; look up variable binding in environment
  (lambda (key alist)
    (if (pair? alist)
      (if (eq? (caar alist) key)
        (cdar alist)
        (lookup key (cdr alist)))
      #?)))                             ; value is undefined

(define evalif                          ; if `test` is #f, evaluate `altn`,
  (lambda (test cnsq altn env)          ; otherwise evaluate `cnsq`.
    (if test
      (eval cnsq env)
      (eval altn env))))

(define evlis
  (lambda (opnds env)
    (if (pair? opnds)
      (cons (eval (car opnds) env) (evlis (cdr opnds) env))
      ())))                             ; value is NIL

(define zip
  (lambda (xs ys env)
    (if (pair? xs)
      (cons (cons (car xs) (car ys)) (zip (cdr xs) (cdr ys) env))
      env)))
```

### Meta-circular Evolution

A series of evolutionary steps
take the meta-circular evaluator above
and enhance it with various new features.
The features implemented here are:

  * Match dotted-tail in `lambda` parameters
  * Lexical scope in `lambda` definition and evaluation
  * Implement `define` for top-level symbol binding

The hybrid reference-implementation looks like this:

```
(define eval
  (lambda (form env)
    (if (symbol? form)
      (lookup form env)                 ; bound variable
      (if (pair? form)
        (if (eq? (car form) 'quote)     ; (quote <form>)
          (cadr form)
          (if (eq? (car form) 'if)      ; (if <pred> <cnsq> <altn>)
            (evalif (eval (cadr form) env) (caddr form) (cadddr form) env)
            (if (eq? (car form) 'lambda) ; (lambda <frml> <body>)
              (CREATE (closure-beh (cadr form) (caddr form) env))
              (if (eq? (car form) 'define) ; (define <symbol> <expr>)
                (set-z (cadr form) (eval (caddr form) env))
                (apply (car form) (evlis (cdr form) env) env)))))
        form))))                        ; self-evaluating form

(define apply
  (lambda (fn args env)
    (if (symbol? fn)
      (if (eq? fn 'list)                ; (list . <args>)
        args
        (if (eq? fn 'cons)              ; (cons <first> <rest>)
          (cons (car args) (cadr args))
          (if (eq? fn 'car)             ; (car <pair>)
            (caar args)
            (if (eq? fn 'cdr)           ; (cdr <pair>)
              (cdar args)
              (if (eq? fn 'eq?)         ; (eq? <left> <right>)
                (eq? (car args) (cadr args))
                (if (eq? fn 'pair?)     ; (pair? <value>)
                  (pair? (car args))
                  (if (eq? fn 'symbol?) ; (symbol? <value>)
                    (symbol? (car args))
                    (apply (lookup fn env) args env))))))))
      (if (pair? fn)
        (if (eq? (car fn) 'lambda)      ; ((lambda <frml> <body>) <args>)
          (eval (caddr fn) (zip (cadr fn) args env))
          (apply (eval fn env) args env))
        (if (actor? fn)
          (CALL fn args)                ; delegate to "functional" actor
          #?)))))

(define lookup                          ; look up variable binding in environment
  (lambda (key alist)
    (if (pair? alist)
      (if (eq? (caar alist) key)
        (cdar alist)
        (lookup key (cdr alist)))
      (if (symbol? key)
        (get-z key)                     ; get top-level binding
        #?))))                          ; value is undefined

(define evalif                          ; if `test` is #f, evaluate `altn`,
  (lambda (test cnsq altn env)          ; otherwise evaluate `cnsq`.
    (if test
      (eval cnsq env)
      (eval altn env))))

(define evlis
  (lambda (opnds env)
    (if (pair? opnds)
      (cons (eval (car opnds) env) (evlis (cdr opnds) env))
      ())))                             ; value is NIL

(define zip
  (lambda (xs ys env)
    (if (pair? xs)
      (cons (cons (car xs) (car ys)) (zip (cdr xs) (cdr ys) env))
      (if (symbol? xs)
        (cons (cons xs ys) env)         ; dotted-tail binds to &rest
        env))))

(define closure-beh
  (lambda (frml body env)
    (BEH (cust . args)
      (eval body (zip frml args env)))))
```

By moving the normal applicative functions
into the global environment,
the implementation of `apply` is greatly simplified.
Additional features implemented here are:

  * Replace special-cases in `apply` with environment bindings
  * Remove literal match for `lambda` in `apply`
  * Allow delegation to actor environments

The current reference-implementation looks like this:

```
(define eval
  (lambda (form env)
    (if (symbol? form)
      (lookup form env)                 ; bound variable
      (if (pair? form)
        (if (eq? (car form) 'quote)     ; (quote <form>)
          (cadr form)
          (if (eq? (car form) 'if)      ; (if <pred> <cnsq> <altn>)
            (evalif (eval (cadr form) env) (caddr form) (cadddr form) env)
            (if (eq? (car form) 'cond)  ; (cond (<test> <expr>) . <clauses>)
              (evcon (cdr form) env)
              (if (eq? (car form) 'lambda) ; (lambda <frml> <body>)
                (CREATE (closure-beh (cadr form) (caddr form) env))
                (if (eq? (car form) 'define) ; (define <symbol> <expr>)
                  (set-z (cadr form) (eval (caddr form) env))
                  (apply (car form) (evlis (cdr form) env) env))))))
        form))))                        ; self-evaluating form

(define apply
  (lambda (fn args env)
    (if (symbol? fn)
      (apply (lookup fn env) args env)
      (if (pair? fn)
        (apply (eval fn env) args env)
        (if (actor? fn)
          (CALL fn args)                ; delegate to "functional" actor
          #?)))))

(define lookup                          ; look up variable binding in environment
  (lambda (key env)
    (if (pair? env)                     ; association list
      (if (eq? (caar env) key)
        (cdar env)
        (lookup key (cdr env)))
      (if (actor? env)
        (CALL env key)                  ; delegate to actor environment
        (if (symbol? key)
          (get-z key)                   ; get top-level binding
          #?))))                        ; value is undefined

(define evalif                          ; if `test` is #f, evaluate `altn`,
  (lambda (test cnsq altn env)          ; otherwise evaluate `cnsq`.
    (if test
      (eval cnsq env)
      (eval altn env))))

(define evcon                           ; (cond (<test> <expr>) . <clauses>)
  (lambda (clauses env)
    ((lambda (clause)
      (if (pair? clause)
        (if (eval (car clause) env)
          (eval (cadr clause) env)
          (evcon (cdr clauses) env))
        #?)
    ) (car clauses))))

(define evlis                           ; map `eval` over a list of operands
  (lambda (opnds env)
    (if (pair? opnds)
      (cons (eval (car opnds) env) (evlis (cdr opnds) env))
      ())))                             ; value is NIL

(define zip                             ; extend `env` by binding names `xs` to values `ys`
  (lambda (xs ys env)
    (if (pair? xs)
      (cons (cons (car xs) (car ys)) (zip (cdr xs) (cdr ys) env))
      (if (symbol? xs)
        (cons (cons xs ys) env)         ; dotted-tail binds to &rest
        env))))

(define closure-beh
  (lambda (frml body env)
    (BEH (cust . args)
      (eval body (zip frml args env)))))
```

Moving operatives (special forms) into the environment,
and making it possible to define new ones,
requires a refactoring of the basic meta-circular interpreter.
The key idea is that we can't decide if the operands should be evaluated
until we know if the function is applicative or operative.
However, the traditional `apply` takes a list of arguments (already evaluated).
Instead, we have `eval` call `invoke`,
which evaluates the operands for applicatives only.

Additional features implemented here are:

  * Introduce `Fexpr_T` for operative interpreters
  * `eval`/`invoke`/`apply` distinguish applicatives/operatives
  * Replace special-cases in `eval` with environment bindings
  * `lambda` body is `seq`
  * `evlis` is `par`

The refactored reference-implementation looks like this:

```
(define eval
  (lambda (form env)
    (if (symbol? form)                  ; bound variable
      (lookup form env)
      (if (pair? form)                  ; procedure call
        (invoke (eval (car form) env) (cdr form) env)
        form))))                        ; self-evaluating form

(define invoke
  (lambda (fn opnds env)
    (if (actor? fn)                     ; _applicative_
      ;(apply fn (evlis opnds env) env)
      (apply fn (CALL op-par (list opnds env)) env)
      (apply fn opnds env))))

(define apply
  (lambda (fn args env)
    (if (actor? fn)
      (CALL fn args)
      (if (fexpr? fn)
        (CALL (get-x fn) (list args env))
        #?))))

(define lookup                          ; look up variable binding in environment
  (lambda (key env)
    (if (pair? env)                     ; association list
      (if (eq? (caar env) key)
        (cdar env)
        (lookup key (cdr env)))
      (if (actor? env)
        (CALL env key)                  ; delegate to actor environment
        (if (symbol? key)
          (get-z key)                   ; get top-level binding
          #?))))                        ; value is undefined

(define evlis                           ; map `eval` over a list of operands
  (lambda (opnds env)
    (if (pair? opnds)
      (cons (eval (car opnds) env) (evlis (cdr opnds) env))
      ())))                             ; value is NIL

(define op-par                          ; (par . <exprs>)
  (CREATE
    (BEH (cust opnds env)
      (if (pair? opnds)
        (SEND
          (CREATE (fork-beh cust eval op-par))
          (list ((car opnds) env) ((cdr opnds) env)))
        (SEND cust ()))
      )))

(define zip                             ; extend `env` by binding names `xs` to values `ys`
  (lambda (xs ys env)
    (if (pair? xs)
      (cons (cons (car xs) (car ys)) (zip (cdr xs) (cdr ys) env))
      (if (symbol? xs)
        (cons (cons xs ys) env)         ; dotted-tail binds to &rest
        env))))

(define closure-beh                     ; lexically-bound applicative function
  (lambda (frml body env)
    (BEH (cust . args)
      (evbody #unit body (zip frml args env)))))

(define op-quote                        ; (quote <form>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (car opnds)
      ))))

(define op-lambda                       ; (lambda <frml> . <body>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (CREATE (closure-beh (car opnds) (cdr opnds) env))
      ))))

(define op-define                       ; (define <symbol> <expr>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (set-z (car opnds) (eval (cadr opnds) env))
      ))))

(define evalif                          ; if `test` is #f, evaluate `altn`,
  (lambda (test cnsq altn env)          ; otherwise evaluate `cnsq`.
    (if test
      (eval cnsq env)
      (eval altn env))))

(define op-if                           ; (if <pred> <cnsq> <altn>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (evalif (eval (car opnds) env) (cadr opnds) (caddr opnds) env)
      ))))

(define op-cond                         ; (cond (<test> <expr>) . <clauses>)
  (CREATE
    (BEH (cust opnds env)
      (if (pair? (car opnds))
        (if (eval (caar opnds) env)
          (SEND cust (eval (cadr (car opnds)) env))
          (SEND SELF (list cust (cdr opnds) env)))
        (SEND cust #?)) )))

(define evbody                          ; evaluate a list of expressions,
  (lambda (value body env)              ; returning the value of the last.
    (if (pair? body)
      (evbody (eval (car body) env) (cdr body) env)
      value)))
(define op-seq                          ; (seq . <body>)
  (CREATE
    (BEH (cust opnds env)
      ;(SEND cust (evbody #unit opnds env))
      (SEND (CREATE (k-seq-beh cust opnds env)) #unit)
    )))
(define k-seq-beh
  (lambda (cust body env)
    (BEH value
      (if (pair? body)
        (SEND
          (CREATE (k-seq-beh cust (cdr body) env))
          (eval (car body) env))
        (SEND cust value)) )))
```

#### Test-Cases

```
(eval '(cons (car '(a b c)) (cdr '(x y z))))
(eval '(lambda (x) x))
(eval '((lambda (x) x) (list 1 2 3)))
(eval '((lambda (x) x) '(lambda (x) x)))
(eval '((lambda (f) (f 42)) '(lambda (x) x)))
(eval '((lambda (f) (f 42)) (lambda (x) x)))
```

## Inspiration

  * [Parsing Expression Grammars: A Recognition-Based Syntactic Foundation](https://bford.info/pub/lang/peg.pdf)
    * [OMeta: an Object-Oriented Language for Pattern Matching](http://www.vpri.org/pdf/tr2007003_ometa.pdf)
    * [PEG-based transformer provides front-, middle and back-end stages in a simple compiler](http://www.vpri.org/pdf/tr2010003_PEG.pdf)
  * [The LISP 1.5 Programmer's Manual](https://www.softwarepreservation.org/projects/LISP/book/LISP%201.5%20Programmers%20Manual.pdf)
  * [SectorLISP](http://justine.lol/sectorlisp2/)
  * [Ribbit](https://github.com/udem-dlteam/ribbit)
    * [A Small Scheme VM, Compiler and REPL in 4K](https://www.youtube.com/watch?v=A3r0cYRwrSs)
  * [Schism](https://github.com/schism-lang/schism)
  * [A Simple Scheme Compiler](https://www.cs.rpi.edu/academics/courses/fall00/ai/scheme/reference/schintro-v14/schintro_142.html#SEC271)

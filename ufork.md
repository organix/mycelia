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

The primary data-structure in **uFork** consists of four integers.

 t        | x        | y        | z
----------|----------|----------|----------
proc/type | head/car | tail/cdr | link/next

### Virtual Machine

#### Data Structures

##### Free-List

 t        | x        | y        | z
----------|----------|----------|----------
Free_T    |UNDEF     |UNDEF     |next

##### Stack

 t        | x        | y        | z
----------|----------|----------|----------
Pair_T    |item      |rest      |

##### Continuation

 t        | x        | y        | z
----------|----------|----------|----------
instr_ptr |stack_ptr |event_ptr |k_next

#### Instructions

 t        | x        | y        | z
----------|----------|----------|----------
VM_cell   |slots     |next_ip   |
VM_push   |literal   |next_ip   |
VM_drop   |count     |next_ip   |
VM_dup    |count     |next_ip   |
VM_eqv    |          |next_ip   |
VM_cmp    |relation  |next_ip   |
VM_if     |true_ip   |false_ip  |
VM_act    |effect    |next_ip   |
VM_putc   |          |next_ip   |
VM_getc   |          |next_ip   |

### Actors

#### Instructions

 t        | x        | y        | z
----------|----------|----------|----------
SELF      |          |          |
SEND      |target    |message   |
CREATE    |behavior  |          |
BECOME    |behavior  |          |
ABORT     |reason    |          |
COMMIT    |          |          |

#### Data Structures

 t        | x        | y        | z
----------|----------|----------|----------
Event_T   |target    |message   |next
Actor_T   |behavior  |events'   |behavior'

### Object Graph

```
e_queue: [head,tail]--------------------------+
          |                                   V
          +-->[Event,to,msg,next]---> ... -->[Event,to,msg,NIL]

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
              [EQ,?,ip,?]
                    |
                    +--> [IF,t,f,?]
                             | |
                             | +--> ...
                             V
                             ...
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

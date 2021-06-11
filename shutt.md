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

### operative?

`(operative? . `_objects_`)`

The _`operative?`_ applicative returns `#t`
if _objects_ all have _operative_ type,
otherwise `#f`.

### applicative?

`(applicative? . `_objects_`)`

The _`applicative?`_ applicative returns `#t`
if _objects_ all have _applicative_ type,
otherwise `#f`.

### combiner?

`(combiner? . `_objects_`)`

The _`combiner?`_ applicative returns `#t`
if _objects_ all either _operative_ or _applicative_ type,
otherwise `#f`.

### ignore?

`(ignore? . `_objects_`)`

The _`ignore?`_ applicative returns `#t`
if _objects_ are all `#ignore`,
otherwise `#f`.

### environment?

`(environment? . `_objects_`)`

The _`environment?`_ applicative returns `#t`
if _objects_ all have _environment_ type,
otherwise `#f`.

### eq?

`(eq? . `_objects_`)`

The _`eq?`_ applicative returns `#t`
unless some two of its arguments
are different objects,
otherwise `#f`.
For any particular two objects,
the result returned by _`eq?`_ is always the same.

### equal?

`(equal? . `_objects_`)`

The _`equal?`_ applicative returns `#t`
unless some two of its arguments
have different values,
otherwise `#f`.
For any particular two objects,
the result returned by _`eq?`_ may change
if one of them is mutated.

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
(<b><i>$lambda</i></b> ⟨formals⟩ . ⟨objects⟩)
</pre>
is equivalent to
<pre>
(<b><i>wrap</i></b> (<b><i>$vau</i></b> ⟨formals⟩ #ignore . ⟨objects⟩))
</pre>

### eval

`(eval `_expression_` `_environment_`)`

The _`eval`_ applicative evaluates _expression_
as a tail context in _environment_,
and returns the resulting value.

### make-env

`(make-env . `_environments_`)`

The _`make-env`_ applicative constructs and returns a new environment,
with initially no local bindings,
and an optional parent environment.


## Numeric Features

The current implementation supports integers only,
using 32-bit signed 2's-complement representation.

### number?

`(number? . `_objects_`)`

The _`number?`_ applicative returns `#t`
if _objects_ all have _number_ type,
otherwise `#f`.

### =?

`(=? . `_numbers_`)`

Applicative _`=?`_ is a predicate that returns `#t` iff
all its arguments are numerically equal to each other.

### <?, <=?, >=?, >?

`(<? . `_numbers_`)`

`(<=? . `_numbers_`)`

`(>=? . `_numbers_`)`

`(>? . `_numbers_`)`

Each of these applicatives is a predicate that returns `#t` iff
the numerical values of every two consecutive elements
obey the order indicated by the name of the applicative.

### +

`(+ . `_numbers_`)`

Applicative _`+`_ returns the sum of the elements of _numbers_.
If _numbers_ is empty, the sum of its elements is `0`.

### -

`(- `_number_` . `_numbers_`)`

Applicative _`-`_ returns the sum of _number_ with
the negation of the sum of _numbers_.

### *

`(* . `_numbers_`)`

Applicative _`*`_ returns the product of the elements of _numbers_.
If _numbers_ is empty, the product of its elements is `1`.

## Bit-Vector Features

In this implementation, _numbers_ can be treated as 32-bit binary vectors.

### bit-not

`(bit-not `_number_`)`

### bit-and

`(bit-and . `_numbers_`)`

### bit-or

`(bit-or . `_numbers_`)`

### bit-xor

`(bit-xor . `_numbers_`)`

### bit-lsl

`(bit-lsl `_number_` `_amount_`)`

### bit-lsr

`(bit-lsr `_number_` `_amount_`)`

### bit-asr

`(bit-asr `_number_` `_amount_`)`


## Library Features

### car

`(car `_pair_`)`

#### Derivation

```
($define! car
  ($lambda ((x . #ignore)) x))
```

### cdr

`(cdr `_pair_`)`

#### Derivation

```
($define! cdr
  ($lambda ((#ignore . x)) x))
```

### caar, cdar, cadr, cddr, caddr

`(caar `_pair_`)`

`(cdar `_pair_`)`

`(cadr `_pair_`)`

`(cddr `_pair_`)`

`(caddr `_pair_`)`

#### Derivation

```
($define! caar
  ($lambda (((x . #ignore) . #ignore)) x))
($define! cdar
  ($lambda (((#ignore . x) . #ignore)) x))
($define! cadr
  ($lambda ((#ignore x . #ignore)) x))
($define! cddr
  ($lambda ((#ignore . (#ignore . x))) x))
($define! caddr
  ($lambda ((#ignore . (#ignore . (x . #ignore)))) x))
```

### get-current-env

`(get-current-env)`

#### Derivation

```
($define! get-current-env
  (wrap ($vau () e e)))
```

### make-standard-env

`(make-standard-env)`

#### Derivation

```
($define! make-standard-env
  ($lambda () (get-current-env)))
```

### $binds?

`($binds? `⟨env⟩` . `⟨symbols⟩`)`

_**not implemented**_

### $get

`($get `⟨env⟩` `⟨symbol⟩`)`

Operative _`$get`_ evaluates ⟨env⟩ in the dynamic environment.
The resulting _env_, must be an environment.
If ⟨symbol⟩ is not bound in _env_, an error is signaled.
If this is not desired, use [`$binds?`](#binds) to check first.

#### Derivation

```
($define! $get
  ($vau (env symbol) dyn
    (eval symbol
      (eval env dyn))))
```

### $set!

`($set! `⟨env⟩` `⟨formal⟩` `⟨value⟩`)`

Operative _`$set!`_ evaluates ⟨env⟩ and ⟨value⟩ in the dynamic environment.
The results are _env_ (which must be an environment) and _value_.
Then ⟨formal⟩ is matched to _value_ in the environment _env_.
The result returned by _`$set!`_ is `#inert`.

#### Derivation

```
($define! $set!
  ($vau (env formal value) dyn
    (eval
      (list $define! formal
        (list (unwrap eval) value dyn))
      (eval env dyn))))
```

### apply

`(apply `_applicative_` `_object_` `_environment_`)`

`(apply `_applicative_` `_object_`)`

When the first syntax is used,
applicative _`apply`_ combines the underlying combiner of _applicative_
with _object_ in dynamic environment _environment_, as a tail context.
The expression
<pre>
(<b><i>apply</i></b> <i>applicative</i> <i>object</i> <i>environment</i>)
</pre>
is equivalent to
<pre>
(<b><i>eval</i></b> (<b><i>cons</i></b> (<b><i>unwrap</i></b> <i>applicative</i>) <i>object</i>) <i>environment</i>)
</pre>
The second syntax is just syntactic sugar; the expression
<pre>
(<b><i>apply</i></b> <i>applicative</i> <i>object</i>)
</pre>
is equivalent to
<pre>
(<b><i>apply</i></b> <i>applicative</i> <i>object</i> (<b><i>make-env</i></b>))
</pre>

#### Derivation

```
($define! apply
  ($lambda (appl arg . opt)
    (eval
      (cons (unwrap appl) arg)
      ($if (null? opt) (make-env) (car opt)))
    ))
```

### list*

`(list* `_object_` . `_objects_`)`

The expression
<pre>
(<b><i>list*</i></b> (<b><i>list</i></b> 1 2) (<b><i>list</i></b> 3 4))
</pre>
evaluates to
<pre>
((1 2) 3 4)
</pre>

#### Derivation

```
($define! list*
  ($lambda (h . t)
    ($if (null? t)
      h
      (cons h (apply list* t)))
    ))
```

### $cond

`($cond . `⟨clauses⟩`)`

⟨clauses⟩ should be a list of clause expressions,
each of the form `(`⟨test⟩` . `⟨body⟩`)`,
where ⟨body⟩ is a list of expressions.

The expression
<pre>
(<b><i>$cond</i></b> (⟨test⟩ . ⟨body⟩) . ⟨clauses⟩)
</pre>
is equivalent to
<pre>
(<b><i>$if</i></b> ⟨test⟩ (<b><i>$sequence</i></b> . ⟨body⟩) (<b><i>$cond</i></b> . ⟨clauses⟩))
</pre>
while the expression <code>(<b><i>$cond</i></b>)</code> is equivalent to `#inert`.

#### Derivation

```
($define! $cond
  ($vau clauses env
    ($if (null? clauses)
      #inert
      (apply
        ($lambda ((test . body) . rest)
          ($if (eval test env)
            (eval (cons $sequence body) env)
            (eval (cons $cond rest) env)))
        clauses))))
```

### $provide!

`($provide! `⟨symbols⟩` . `⟨body⟩`)`

The _`$provide!`_ operative constructs a child _e_ of the dynamic environment _d_;
evaluates the elements of ⟨body⟩ in _e_, from left to right, discarding all of the results;
and exports all of the bindings of symbols in ⟨symbols⟩ from _e_ to _d_,
i.e., binds each symbol in _d_ to the result of looking it up in _e_.
The result returned by _`$provide!`_ is `#inert`.

#### Derivation

```
($define! $provide!
  ($vau (symbols . body) env
    (eval
      (list $define! symbols
        (list
          (list $lambda ()
            (list* $sequence body)
            (list* list symbols))))
      env)))
```

### length

`(length `_object_`)`

Applicative _`length`_ returns the (exact) improper-list length of _object_.
That is, it returns the number of consecutive cdr references
that can be followed starting from object.
If _object_ is not a pair, it returns `0`;

#### Derivation

```
($define! length
  ($lambda (object)
    ($if (pair? object)
      (+ 1 (length (cdr object)))
      0)
    ))
```

### append

`(append . `_lists_`)`

The _`append`_ applicative returns a freshly allocated list
of the elements of all the specified lists, in order,
except that if there is a last specified element of _lists_, it is not copied,
but is simply referenced by the cdr of the preceding pair (if any) in the resultant list.

#### Derivation

```
($define! append
  ($lambda x
    ($if (pair? x)
      (apply ($lambda (h . t)
        ($if (pair? t)
          ($if (null? h)
            (apply append t)
            (cons (car h) (apply append (cons (cdr h) t)))
          )
        h)
      ) x)
      x)
  ))
```

### reverse

`(reverse `_list_`)`

The _`reverse`_ applicative returns a freshly allocated list
of the elements of _list_, in reverse order.

#### Derivation

```
($define! reverse
  (($lambda ()
    ($define! push-pop
      ($lambda (r s)
        ($if (null? s)
          r
          (push-pop
            (cons (car s) r)
            (cdr s)))))
    ($lambda (s)
      (push-pop () s))
  )))
```

### filter

`(filter `_predicate_` `_list_`)`

The _`filter`_ applicative passes each of the elements of _list_
as an argument to _predicate_,
one at a time in no particular order,
using a fresh empty environment for each call.
_`filter`_ constructs and returns a list
of all elements of _list_
on which _predicate_ returned `#t`,
in the same order as in _list_.

#### Derivation

```
($define! filter
  ($lambda (accept? xs)
    ($if (null? xs)
      ()
      (($lambda ((first . rest))
        ($if (eval (list (unwrap accept?) first) (make-env))
          (cons first (filter accept? rest))
          (filter accept? rest))
      ) xs)) ))
```

### map

`(map `_applicative_` . `_lists_`)`

_lists_ must be a nonempty list of lists;
if there are two or more,
they must all have the same length.
If _lists_ is empty,
or if all of its elements are not lists of the same length,
an error is signaled.

The _`map`_ applicative applies _applicative_ element-wise
to the elements of the lists in _lists_
(i.e., applies it to a list of the first elements of the lists,
to a list of the second elements of the lists, etc.),
using the dynamic environment from which _`map`_ was called,
and returns a list of the results, in order.
The applications may be performed in any order,
as long as their results occur in the resultant list
in the order of their arguments in the original lists.

#### Derivation

```
($provide! (map)
  ($define! map
    (wrap ($vau (applicative . lists) env
      ($if (apply null? lists env)
        ()
        (appl applicative (peel lists () ()) env)
      ))))
  ($define! peel
    ($lambda (((head . tail) . more) heads tails)
      ($if (null? more)
        (list (cons head heads) (cons tail tails))
        (($lambda ((heads tails))
          (list (cons head heads) (cons tail tails)))
        (peel more heads tails)))
    ))
  ($define! appl
    ($lambda (applicative (heads tails) env)
      (cons
        (apply applicative heads env)
        ($if (apply null? tails env)
          ()
          (appl applicative (peel tails () ()) env)))
    ))
)
```

#### Examples

```
> (map + (list 1 2) (list 3 4))
(4 6)

> (map + (list 1) (list 2))
(3)

> (map + (list 1 2))
(1 2)

> (map + (list 1))
(1)

> (map + ())
()

> (map cons (list 1 2 3) (list 4 5 6))
((1 . 4) (2 . 5) (3 . 6))

> (map car (list (list 1 2) (list 3 4) (list 5 6)))
(1 3 5)

> (map cdr (list (list 1 2) (list 3 4) (list 5 6)))
((2) (4) (6))
```

### reduce

`(reduce `_args_` `_binop_` `_zero_`)`

_args_ should be a list. _binop_ should be an applicative.
If _args_ is empty, applicative _`reduce`_ returns _zero_.
If _args_ is nonempty, applicative _`reduce`_ uses binary operation _binop_
to merge all the elements of _args_ into a single object,
using any associative grouping of the elements.
That is, the sequence of objects initially found in _args_
is repeatedly decremented in length
by applying _binop_ to a list of any two consecutive objects,
replacing those two objects with the result
at the point in the sequence where they occurred;
and when the sequence contains only one object,
that object is returned.

#### Derivation

```
($define! reduce
  ($lambda (args binop zero)
    ($if (null? args)
      zero
      (($lambda ((first . rest))
        ($if (null? rest)
          first
          (binop first (reduce rest binop zero)))
      ) args)) ))
```

#### Alternatives

```
($define! foldl
  ($lambda (args binop zero)
    ($if (null? args)
      zero
      (($lambda ((first . rest))
        (foldl rest binop (binop zero first))
      ) args)) ))
```

```
($define! foldr
  ($lambda (args binop zero)
    ($if (null? args)
      zero
      (($lambda ((first . rest))
        (binop first (foldr rest binop zero))
      ) args)) ))
```

#### Examples

```
> (reduce (list 1 2 3) cons 0)
(1 2 . 3)

> (foldl (list 1 2 3) cons 0)
(((0 . 1) . 2) . 3)

> (foldr (list 1 2 3) cons 0)
(1 2 3 . 0)
```

### $let

`($let `⟨bindings⟩` . `⟨body⟩`)`

⟨bindings⟩ should be a list of formal-parameter-tree/expression pairings,
each of the form `(`⟨formals⟩` `⟨expression⟩`)`,
where each ⟨formals⟩ is a formal parameter tree.

The expression
<pre>
(<b><i>$let</i></b> ((⟨form<sub>1</sub>⟩ ⟨exp<sub>1</sub>⟩) ... (⟨form<sub><i>n</i></sub>⟩ ⟨exp<sub><i>n</i></sub>⟩)) . ⟨body⟩)
</pre>
is equivalent to
<pre>
((<b><i>$lambda</i></b> (⟨form<sub>1</sub>⟩ ... ⟨form<sub><i>n</i></sub>⟩) . ⟨body⟩) ⟨exp<sub>1</sub>⟩ ... ⟨exp<sub><i>n</i></sub>⟩)
</pre>

Thus, the ⟨exp<sub><i>k</i></sub>⟩ are first evaluated
in the dynamic environment, in any order;
then a child environment _e_ of the dynamic environment is created,
with the ⟨form<sub><i>k</i></sub>⟩ matched in _e_
to the results of the evaluations of the ⟨exp<sub><i>k</i></sub>⟩;
and finally the subexpressions of ⟨body⟩ are evaluated in _e_
from left to right, with the last (if any) evaluated as a tail context,
or if ⟨body⟩ is empty the result is `#inert`.

#### Derivation

```
($define! $let
  ($vau (bindings . body) env
    (eval (cons
      (list* $lambda (map car bindings) body)
      (map cadr bindings)) env)
  ))
```

#### Example

```
> ($let ((one 1) (two 2)) (list one two))
(1 2)
```

### $let*

`($let* `⟨bindings⟩` . `⟨body⟩`)`

The binding expressions are guaranteed to be evaluated from left to right,
and each of these evaluations has access to the bindings of previous evaluations.
However, each of these evaluations takes place
in a child of the environment of the previous one,
and bindings for the previous evaluation take place in the child, too.
So, if one of the binding expressions is a _`$vau`_ or _`$lambda`_ expression,
the resulting combiner still can’t be recursive;
and only the first binding expression is evaluated in the dynamic environment.

#### Derivation

```
($define! $let*
  ($vau (bindings . body) env
    (eval ($if (null? bindings)
      (list* $let bindings body)
      (list $let
        (list (car bindings))
        (list* $let* (cdr bindings) body))
    ) env)))
```

#### Example

```
> ($let* ((one 1) (two (+ one one))) (list one two))
(1 2)
```

### $letrec

`($letrec `⟨bindings⟩` . `⟨body⟩`)`

The binding expressions may be evaluated in any order.
None of them are evaluated in the dynamic environment,
so there is no way to capture the dynamic environment using _`$letrec`_;
and none of the bindings are made until after the expressions have been evaluated,
so the expressions cannot see each others’ results;
but since the bindings are in the same environment as the evaluations,
they can be recursive, and even mutually recursive, combiners.

#### Derivation

```
($define! $letrec
  ($vau (bindings . body) env
    (eval (list* $let ()
      (list $define!
        (map car bindings)
        (list* list (map cadr bindings)))
      body) env)))
```

#### Example

```
> ($letrec (
    (odd ($lambda (x) ($if (=? x 0) #f (even (- x 1)))))
    (even ($lambda (x) ($if (=? x 0) #t (odd (- x 1)))))
  ) (list (odd 3) (even 3)))
(#t #f)
```

### $letrec*

`($letrec* `⟨bindings⟩` . `⟨body⟩`)`

The binding expressions are guaranteed to be evaluated from left to right;
each of these evaluations has access to the bindings of previous evaluations;
and the result of each evaluation is matched
in the same environment where it was performed,
so if the result is a combiner, it can be recursive.
Further, the result of each binding expression is matched separately,
so there is nothing to prevent the same symbol from occurring in more than one ⟨formals⟩.
However, since each evaluation takes place in a child of the previous one,
and even the first does not take place in the dynamic environment,
there is no way to capture the dynamic environment using _`$letrec*`_,
and no way for combiners resulting from different binding expressions to be mutually recursive.

#### Derivation

```
($define! $letrec*
  ($vau (bindings . body) env
    (eval ($if (null? bindings)
      (list* $letrec bindings body)
      (list $letrec
        (list (car bindings))
        (list* $letrec* (cdr bindings) body))
    ) env)))
```

### $let-redirect

`($let-redirect `⟨env-exp⟩` `⟨bindings⟩` . `⟨body⟩`)`

The binding expressions are evaluated in the dynamic environment,
but the local environment is a child of
the environment resulting from evaluating ⟨env-exp⟩.
This promotes semantic stability,
by protecting the meaning of expressions in the body
from unexpected changes to the client’s environment
(much as static scoping protects explicitly constructed compound combiners).

In the interests of maintaining clarity and orthogonality of semantics,
there are no variants of _`$let-redirect`_ analogous to
the variants _`$let*`_, etc., of _`$let`_.
The variants of _`$let`_ modulate its role
in locally augmenting the current environment,
whereas the primary purpose of _`$let-redirect`_
is presumed to be locally replacing the current environment;
so it was judged better to provide just one environment replacement device,
insulating it as much as possible from complexities of environment augmentation.

#### Derivation

```
($define! $let-redirect
  ($vau (env-exp bindings . body) env
    (eval (list*
      (eval (list* $lambda (map car bindings) body) (eval env-exp env))
      (map cadr bindings))
    env)))
```

### $let-safe

`($let-safe `⟨bindings⟩` . `⟨body⟩`)`

The expression
<pre>
(<b><i>$let-safe</i></b> ⟨bindings⟩ . ⟨body⟩)
</pre>
is equivalent to
<pre>
(<b><i>$let-redirect</i></b> (<b><i>make-standard-env</i></b>) ⟨bindings⟩ . ⟨body⟩)
</pre>

#### Derivation

```
($define! $let-safe
  ($vau (bindings . body) env
    (eval
      (list* $let-redirect (make-standard-env) bindings body)
    env)))
```

### $remote-eval

`($remote-eval `⟨exp⟩` `⟨env-exp⟩`)`

Operative _`$remote-eval`_ evaluates ⟨exp⟩ as a tail context,
in the environment that must result from
the evaluation of ⟨env-exp⟩ in the dynamic environment.

#### Derivation

```
($define! $remote-eval
  ($vau (o e) d
    (eval o (eval e d))))
```

### $bindings->env

`($bindings->env . `⟨bindings⟩`)`

The expression
<pre>
(<b><i>$bindings->env</i></b> . ⟨bindings⟩)
</pre>
is equivalent to
<pre>
(<b><i>$let-redirect</i></b> (<b><i>make-env</i></b>) ⟨bindings⟩ (<b><i>get-current-env</i></b>))
</pre>

#### Derivation

```
($define! $bindings->env
  ($vau bindings denv
    (eval (list
      $let-redirect
      (make-env)
      bindings
      (list get-current-env))
    denv)))
```

### not?

`(not? `_boolean_`)`

Applicative _`not?`_ is a predicate
that returns the logical negation of its argument.

#### Derivation

```
($define! not? ($lambda (x) ($if x #f #t)))
```

### and?

`(and? `_boolean_`)`

Applicative _`and?`_ is a predicate
that returns `#t` unless one or more of its arguments are `#f`.

#### Derivation

```
($define! and?
  ($lambda args
    (eval (cons $and? args) (get-current-env)) ))
```

### or?

`(or? `_boolean_`)`

Applicative _`or?`_ is a predicate
that returns `#f` unless one or more of its arguments are `#t`.

#### Derivation

```
($define! or?
  ($lambda args
    (eval (cons $or? args) (get-current-env)) ))
```

### $and?

`($and? . `⟨objects⟩`)`

Operative _`$and?`_ is a predicate that
evaluates its operands from left to right,
until either an operand evaluates to `#f` (result is `#f`),
or the end of the list is reached (result is `#t`).
If the an operand does not evaluate to a _boolean_,
an error is signaled.

#### Derivation

```
($define! $and?
  ($vau x e
    ($cond
      ((null? x) #t)
      ((null? (cdr x)) (eval (car x) e))  ; tail context
      ((eval (car x) e) (apply (wrap $and?) (cdr x) e))
      (#t #f)
    )))
```

### $or?

`($or? . `⟨objects⟩`)`

Operative _`$or?`_ is a predicate that
evaluates its operands from left to right,
until either an operand evaluates to `#t` (result is `#t`),
or the end of the list is reached (result is `#f`).
If the an operand does not evaluate to a _boolean_,
an error is signaled.

#### Derivation

```
($define! $or?
  ($vau x e
    ($cond
      ((null? x) #f)
      ((null? (cdr x)) (eval (car x) e))  ; tail context
      ((eval (car x) e) #t)
      (#t (apply (wrap $or?) (cdr x) e))
    )))
```

## Machine Tools

### dump-bytes

`(dump-bytes `_address_` `_count_`)`

The _`dump-bytes`_ applicative prints a byte dump to the console,
starting at _address_ and continuing for _count_ bytes.

#### Example

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

**_WARNING!_** This is a dangerous operation. Be careful!

### dump-words

`(dump-words `_address_` `_count_`)`

The _`dump-words`_ applicative prints a 32-bit word dump to the console,
starting at _address_ and continuing for _count_ words.
The _address_ must be aligned on a 4-byte boundary.

#### Example

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

**_WARNING!_** This is a dangerous operation. Be careful!

### address-of

`(address-of `_object_`)`

The _`address-of`_ applicative returns an exact integer
representing the address of _object_.

### content-of

`(content-of `_address_`)`

The _`content-of`_ applicative returns the object at _address_.

**_WARNING!_** This is a dangerous operation. Be careful!

### dump-env

`(dump-env `_environment_`)`

The _`dump-words`_ applicative prints an environment dump to the console.

#### Example

```
> (dump-env (get-current-env))
0000e8a0: get-current-env = #wrap@0000edc0[0000ee00]
0000ee60: cdr = #wrap@0000ed60[0000ed80]
0000ee40: car = #wrap@0000ed00[0000ed20]
0000eea0: --scope--
0000e880: list = #wrap@00008fa0[00008f00]
0000e840: pair? = #wrap@000098e0[000098c0]
0000e800: null? = #wrap@00009920[00009900]
...
```

### sponsor-reserve

`(sponsor-reserve)`

The _`sponsor-reserve`_ applicative returns an exact integer
representing the address of a newly-allocated 32-byte block of memory.

### sponsor-release

`(sponsor-release `_address_`)`

The _`sponsor-release`_ applicative deallocates
the previously-reserved block of memory at _address_.
The result is `#inert`.

**_WARNING!_** This is a dangerous operation. Be careful!

### sponsor-enqueue

`(sponsor-enqueue `_address_`)`

The _`sponsor-enqueue`_ applicative adds
the previously-reserved block of memory at _address_
to the actor runtime message-event queue.
The result is `#inert`.

**_WARNING!_** This is a dangerous operation. Be careful!

### $timed

`($timed . `⟨objects⟩`)`

The _`$timed`_ operative evaluates the elements of the list ⟨objects⟩
in the dynamic environment, one at a time from left to right (c.f.: [`$sequence`](#sequence)).
The result is the number of microseconds elapsed while performing the computation.

#### Example

```
($define! f ($lambda (x) ($if (=? x 0) 0 (f (- x 1))) ))

> ($timed (f 1000))
1588543
```

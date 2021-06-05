# Parsing Expression Grammar (PEG)

A PEG parser is a deterministic recursive-decent style parser
that features prioritized choice,
token predicate matching,
and arbitrary look-ahead.

Each parsing expression takes a list of tokens as input,
and returns either `(#t `_value_` `_remainder_`)` on success,
or `(#f `_remainder_`)` on match failure.
_value_ is the semantic value of the match,
which varies based on the type of the parsing expression.
_remainder_ is the list of unmatched tokens in the input.

## Primitive Parsing Expressions

### peg-empty

Succeed without consuming any input.
The semantic value is `()`.

```
($define! peg-empty
  ($lambda (in)
    (list #t () in)))
```

### peg-fail

Fail without consuming any input.

```
($define! peg-fail
  ($lambda (in)
    (list #f in)))
```

### peg-any

If _input_ is empty, fail.
Otherwise consume the first token of _input_
and succeed.
The semantic value is the token.

```
($define! peg-any
  ($lambda (in)
    ($if (null? in)
      (list #f in)
      ($let (((token . rest) in))
        (list #t token rest))) ))
```

### peg-if

If _input_ is empty, fail.
Otherwise consume the first token of _input_
and succeed if `(test? `_token_`)` is `#t`.
The semantic value is the token.

```
($define! peg-if
  ($lambda (test?)
    ($lambda (in)
      ($if (null? in)
        (list #f in)
        ($let (((token . rest) in))
          ($if (test? token)
            (list #t token rest)
            (list #f in)) )) )))
```

#### Example

```
($define! match-nl  ; match newline character (ASCII 10)
  (peg-if ($lambda (token) (=? 10 token))) )
```

### peg-or

Attempt to match _left_.
If successful, return the result.
Otherwise return the result
of attempting to match _right_
with the initial _input_.

```
($define! peg-or
  ($lambda (left right)
    ($lambda (in)
      ($let (((ok . state) (left in)))
        ($if ok
          (cons #t state)
          (right in))) )))
```

### peg-and

Attempt to match _left_.
If successful, attempt to match _right_
with the _remainder_ from _left_.
If both are successful,
the semantic value is the `cons`
of the values from _left_ and _right_.
Otherwise fail without consuming any input.

```
($define! peg-and
  ($lambda (left right)
    ($lambda (in)
      ($let (((ok . state) (left in)))
        ($if ok
          ($let (((lval rest) state))
            ($let (((ok . state) (right rest)))
              ($if ok
                ($let (((rval rest) state))
                  (list #t (cons lval rval) rest))
                (list #f in))))
          (list #f in))) )))
```

### peg-not

If _peg_ matches the _input_, fail.
Otherwise the semantic value is `#inert`
and the match succeeds without consuming any input.
This is negative look-ahead.

```
($define! peg-not
  ($lambda (peg)
    ($lambda (in)
      ($let (((ok . #ignore) (peg in)))
        ($if ok
          (list #f in)
          (list #t #inert in))) )))
```

### peg-xform

Attempt to match _peg_.
Transform the entire result with _xform_.

```
($define! peg-xform
  ($lambda (peg xform)
    ($lambda (in)
      (xform (peg in)) )))
```

#### Example

```
($define! digits->number
  ($lambda (n ds)
    ($if (null? ds)
      n
      (digits->number
        (+ (* 10 n) (car ds) -48)
        (cdr ds))
    )))

($define! peg->number
  ($lambda ((ok . state))
    ($if ok
      ($let (((value rest) state))
        (list #t (digits->number 0 value) rest))
      (cons #f state)
    )))

($define! match-digits (peg-plus (peg-range 48 57)))

> ((peg-xform match-digits peg->number) (list 49 50 51 10))
(#t 123 (10))
```


## Derived Parsing Expressions

### peg-equal

If _input_ is empty, fail.
Otherwise consume the first token of _input_
and succeed if `(equal? `_value_` `_token_`)` is `#t`.
The semantic value is the token.

```
($define! peg-equal
  ($lambda (value)
    (peg-if ($lambda (token) (equal? value token))) ))
```

#### Alternate (inline) Derivation

```
($define! peg-equal
  ($lambda (value)
    ($lambda (in)
      ($if (null? in)
        (list #f in)
        ($let (((token . rest) in))
          ($if (equal? value token)
            (list #t token rest)
            (list #f in)) )) )))
```

### peg-range

If _input_ is empty, fail.
Otherwise consume the first token of _input_
and succeed if `(<=? `_lo_` `_token_` `_hi_`)` is `#t`.
The semantic value is the token.

```
($define! peg-range
  ($lambda (lo hi)
    (peg-if ($lambda (token) (<=? lo token hi))) ))
```

### peg-alt

Attempt to match each expression in _pegs_ in order,
each starting at the initial _input_.
Return the result of the first successful match.
Otherwise fail without consuming any input.

```
($define! peg-alt
  ($lambda pegs
    ($if (pair? pegs)
      (peg-or (car pegs) (apply peg-alt (cdr pegs)))
      peg-fail)))
```

### peg-seq

Attempt to match each expression in _pegs_ in order,
initially at _input_,
then at each consecutive _remainder_
left by the previous match.
If all are successful,
the semantic value is a `list`
of the semantic values from each match.
Otherwise fail without consuming any input.

```
($define! peg-seq
  ($lambda pegs
    ($if (pair? pegs)
      (peg-and (car pegs) (apply peg-seq (cdr pegs)))
      peg-empty)))
```

### peg-opt

Match 0 or 1 occurances of _peg_.
If _peg_ matches the _input_,
the sematic value is a one-element `list`
of the semantic value matched.
Otherwise the semantic value is `()`
and the match succeeds without consuming any input.

```
($define! peg-opt
  ($lambda (peg)
    (peg-or peg peg-empty)))
```

### peg-star

Match 0 or more occurances of _peg_,
eagerly consuming as many as possible.
The semantic value is a `list` (possible empty)
of the semantic values from each match.

```
($define! peg-star
  ($lambda (peg)
    ($lambda (in)
      ((peg-opt (peg-and peg (peg-star peg))) in))))
```

### peg-plus

Match 1 or more occurances of _peg_,
eagerly consuming as many as possible.
If succesful, the semantic value is a `list`
of the semantic values from each match.
Otherwise fail without consuming any input.

```
($define! peg-plus
  ($lambda (peg)
    (peg-and peg (peg-star peg)) ))
```

### peg-peek

If _peg_ matches the _input_,
the semantic value is `#inert`
and the match succeeds without consuming any input.
Otherwise fail without consuming any input.
This is positive look-ahead.

```
($define! peg-peek
  ($lambda (peg)
    (peg-not (peg-not peg)) ))
```

## Infix Expression Example

A grammar for parsing arithmetic expression with infix operators.
Operator precedence is encoded into the grammar itself.

```
; expr   = term ([-+] term)*
; term   = factor ([*/] factor)*
; factor = '(' expr ')' | number
; number = [0-9]+

($define! match-expr
  ($lambda (in)
    ((peg-xform
      (peg-seq
        match-term
        (peg-star
          (peg-seq
            (peg-or
              (peg-equal 43)  ; plus
              (peg-equal 45)  ; minus
            )
          match-term)))
      peg->binop
    ) in)))
($define! match-term
  ($lambda (in)
    ((peg-xform
      (peg-seq
        match-factor
        (peg-star
          (peg-seq
            (peg-or
              (peg-equal 42)  ; times
              (peg-equal 47)  ; divide
            )
            match-factor)))
      peg->binop
    ) in)))
($define! match-factor
  ($lambda (in)
    ((peg-alt
      (peg-xform
        (peg-seq
          (peg-equal 40)  ; open paren
          match-expr
          (peg-equal 41)  ; close paren
        )
        ($lambda ((ok . state))
          ($if ok
            ($let (((value rest) state))
              (list #t (cadr value) rest))
            (cons #f state)
          ))
      )
      match-number
    ) in)))
($define! match-number
  ($lambda (in)
    ((peg-xform
      (peg-plus (peg-range 48 57))  ; [0-9]+
      ($lambda ((ok . state))
        ($if ok
          ($let (((value rest) state))
            (list #t (digits->number 0 value) rest))
          (cons #f state)
        ))
    ) in)))

($define! char->appl
  ($lambda (ascii)
    ($cond
      ((eq? ascii 43) +)
      ((eq? ascii 45) -)
      ((eq? ascii 42) *)
      ((eq? ascii 47) /)
      (#t list)
    )))
($define! infix->prefix
  ($lambda ((t0 op-args))
    ($if (null? op-args)
      t0
      (infix->prefix (list (list* (caar op-args) t0 (cdar op-args)) (cdr op-args)))
;      (infix->prefix (list (list* (char->appl (caar op-args)) t0 (cdar op-args)) (cdr op-args)))
    )))
($define! peg->binop
  ($lambda ((ok . state))
    ($if ok
      ($let (((value rest) state))
        (list #t (infix->prefix value) rest))
      (cons #f state)
    )))
($define! digits->number
  ($lambda (n ds)
    ($if (null? ds)
      n
      (digits->number
        (+ (* 10 n) (car ds) -48)
        (cdr ds))
    )))
```

```
; test-case "1+2"
> (match-expr (list 49 43 50))
(#t (43 1 2) ())

; test-case "1+2*3"
> (match-expr (list 49 43 50 42 51))
(#t (43 1 (42 2 3)) ())

; test-case "1*2+3"
> (match-expr (list 49 42 50 43 51))
(#t (43 (42 1 2) 3) ())

; test-case "1+2*3-90"
> (match-expr (list 49 43 50 42 51 45 57 48))
(#t (45 (43 1 (42 2 3)) 90) ())

; test-case "1+2*(3-90)"
> (match-expr (list 49 43 50 42 40 51 45 57 48 41))
(#t (43 1 (42 2 (45 3 90))) ())
```

## LISP/Scheme Example

A grammar for parsing languages in the LISP/Scheme family.

```
; sexpr  = _ (list | atom)
; list   = '(' sexpr* _ ')'
; atom   = (number | symbol)
; number = [0-9]+
; symbol = [a-zA-Z0-9]+
; _      = [ \t-\r]*

($define! match-sexpr
  ($lambda (in)
    ((peg-and
      match-optws
      (peg-or
        match-list
        match-atom
      )
    ) in)))
($define! match-list
  ($lambda (in)
    ((peg-seq
      (peg-equal 40)  ; open paren
      (peg-star match-sexpr)
      match-optws
      (peg-equal 41)  ; close paren
    ) in)))
($define! match-atom
  ($lambda (in)
    ((peg-or
      match-number
      match-symbol
    ) in)))
($define! match-number
  ($lambda (in)
    ((peg-plus
      (peg-range 48 57)  ; digit
    ) in)))
($define! match-symbol
  ($lambda (in)
    ((peg-plus
      (peg-alt
        (peg-range 97 122)  ; lowercase
        (peg-range 65 90)   ; uppercase
        (peg-range 48 57)   ; digit
      )
    ) in)))
($define! match-optws
  ($lambda (in)
    ((peg-star
      (peg-range 0 32)  ; whitespace (+ctrls)
    ) in)))
```

```
; test-case "(CAR ( LIST 0 1)\t)"
(match-sexpr (list 40 67 65 82 32 40 32 76 73 83 84 32 48 32 49 41 9 41))
```

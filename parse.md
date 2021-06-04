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

## Usage Examples

```
> ((peg-star (peg-equal 10)) (list 10 10 10 13 10 13))
(#t (10 10 10) (13 10 13))

; with arm-asm (38656 byte kernel.img)
> ($timed ((peg-star (peg-equal 10)) (list 10 10 10 13 10 13)))
1009458

; without arm-asm (31872 byte kernel.img)
> ($timed ((peg-star (peg-equal 10)) (list 10 10 10 13 10 13)))
468171
448403
437850

```

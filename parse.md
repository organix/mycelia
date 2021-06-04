# Parsing Expression Grammar (PEG)

A PEG parser is a deterministic recursive-decent style parser
that features prioritized choice,
token predicate matching,
and arbitrary look-ahead.

Each parsing expression takes a list of tokens as input,
and returns either `(#t `value` `remaining`)` on success,
or `(#f `remaining`)` on match failure.
The _remaining_ value is the list of unmatched tokens
remaining in the input list.
The _value_ is the semantic value of the match,
which varies based on the type of the parsing expression.

## Primitive Parsing Expressions

### peg-empty

```
($define! peg-empty
  ($lambda (in)
    (list #t () in)))
```

### peg-fail

```
($define! peg-fail
  ($lambda (in)
    (list #f in)))
```

### peg-any

```
($define! peg-any
  ($lambda (in)
    ($if (null? in)
      (list #f in)
      ($let (((token . rest) in))
        (list #t token rest))) ))
```

### peg-if

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

### peg-xform

```
($define! peg-xform
  ($lambda (peg xform)
    ($lambda (in)
      (xform (peg in)) )))
```

## Derived Parsing Expressions

### peg-equal

```
($define! peg-equal
  ($lambda (value)
    (peg-if ($lambda (token) (equal? value token))) ))
```

### Alternate (inline) Derivation

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

```
($define! peg-range
  ($lambda (lo hi)
    (peg-if ($lambda (token) ($if (>=? lo token) (<=? hi token) #f))) ))
```

### peg-alt

```
($define! peg-alt
  ($lambda pegs
    ($if (pair? pegs)
      (peg-or (car pegs) (apply peg-alt (cdr pegs)))
      peg-fail)))
```

### peg-seq

```
($define! peg-seq
  ($lambda pegs
    ($if (pair? pegs)
      (peg-and (car pegs) (apply peg-seq (cdr pegs)))
      peg-empty)))
```

### peg-opt

```
($define! peg-opt
  ($lambda (peg)
    (peg-or peg peg-empty)))
```

### peg-star

```
($define! peg-star
  ($lambda (peg)
    ($lambda (in)
      ((peg-opt (peg-and peg (peg-star peg))) in))))
```

### peg-plus

```
($define! peg-plus
  ($lambda (peg)
    (peg-and peg (peg-star peg)) ))
```

### peg-not

```
($define! peg-not
  ($lambda (peg)
    (peg-xform peg
      ($lambda (ok . state)
        ($if ok
          (list #f (cadr state))
          (list #t #inert (car state))) )) ))
```

### Alternate (inline) Derivation

```
($define! peg-not
  ($lambda (peg)
    ($lambda (in)
      ($let (((ok . #ignore) (peg in)))
        ($if ok
          (list #f in)
          (list #t #inert in))) )))
```

### peg-peek

```
($define! peg-peek
  ($lambda (peg)
    (peg-not (peg-not peg)) ))
```

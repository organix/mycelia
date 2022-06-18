;;
;; library.scm (extended library definitions)
;;

(define current-env (vau _ e e))
(define macro (vau (frml . body) env
  (eval (list vau frml '_env_ (list eval (cons seq body) '_env_)) env) ))
;current-env
;==> #fexpr@2127
;(current-env)
;==> ()
;(define f (lambda x (seq (list list x (current-env) current-env))))
;(f 1 2 3)
;==> (#actor@565 (+1 +2 +3) ((x +1 +2 +3) (_ . #?)) #fexpr@2127)
;(define m (macro x (list list x (current-env) current-env)))
;(m 1 2 3)
;==> (#? #? #fexpr@2127)
;(define v (vau x e (eval (seq (list list x (current-env) current-env)) e) ))
;(v 1 2 3)
;==> (#? #? #fexpr@2127)

;(define qlist (macro x (list quote x)))
(define qlist (vau x _ x))
(define quote (vau (x) _ x))
(define seq (macro body _ (list (list* 'lambda '_ body))))

(define integer? number?)  ; integers are currently the only number type implemented

(define when
  (macro (cond . body)
    (list if cond (cons seq body) #unit)))

(define list* (lambda (h . t) (if (pair? t) (cons h (apply list* t)) h)))
;(define list* (lambda xs (reduce cons #? xs)))

(define cond
  (macro clauses
    (if (null? clauses)
      #unit
      (apply
        (lambda ((test . body) . rest)
          (list if test (cons seq body) (cons cond rest)) )
        clauses) )))

;(define append (lambda (x y) (if (null? x) y (cons (car x) (append (cdr x) y)))))  ; two lists only
(define append
  (lambda x
    (if (pair? x)
      (apply
        (lambda (h . t)
          (if (pair? t)
            (if (pair? h)
              (cons
                (car h)
                (apply append (cons (cdr h) t)))
              (apply append t))
            h))
        x)
      x)))

(define filter
  (lambda (pred? xs)
    (if (pair? xs)
      (if (pred? (car xs))
        (cons (car xs) (filter pred? (cdr xs)))
        (filter pred? (cdr xs)))
      ())))

(define reduce
  (lambda (binop zero xs)
    (if (pair? xs)
      (if (pair? (cdr xs))
        (binop (car xs) (reduce binop zero (cdr xs)))
        (car xs))
      zero)))
(define foldl
  (lambda (binop zero xs)
    (if (pair? xs)
      (foldl binop (binop zero (car xs)) (cdr xs))
      zero)))
(define foldr
  (lambda (binop zero xs)
    (if (pair? xs)
      (binop (car xs) (foldr binop zero (cdr xs)))
      zero)))

;(define reverse (lambda (xs) (if (pair? xs) (append (reverse (cdr xs)) (list (car xs))) xs)))  ; O(n^2) algorithm
(define reverse
  (lambda (xs)
    (foldl (lambda (x y) (cons y x)) () xs)))
; An alternative using an explicit helper function.
;(define push-pop (lambda (to from)
;  (if (pair? from) (push-pop (cons (car from) to) (cdr from)) to)))
(define reverse
  (lambda (xs)
    (define push-pop (lambda (to from)
      (if (pair? from) (push-pop (cons (car from) to) (cdr from)) to)))
    (push-pop () xs)))

;(define map (lambda (f xs) (if (pair? xs) (cons (f (car xs)) (map f (cdr xs))) ())))  ; f takes only 1 arg
(define map
  (lambda (f . xs)
    (if (pair? (car xs))
      (cons
        (apply f (foldr (lambda (x y) (cons (car x) y)) () xs))
        (apply map (cons f (foldr (lambda (x y) (cons (cdr x) y)) () xs))))
      ())))

;(define expand-let (vau (kvs . body) _ (cons (list* 'lambda (map car kvs) body) (map cadr kvs)))
(define let
  (macro (bindings . body)
    (cons
      (list* lambda (map car bindings) body)
      (map cadr bindings))))

(define provide
  (macro (symbols . body)
    (list define symbols
      (list
        (list lambda ()
          (cons seq body)
          (cons list symbols)) ))))

(define newline
  (lambda ()
    (emit 10)))

; interative (tail-recursive) tree -> sequence
(define fringe
  (lambda (t s r)
    (cond
      ((pair? t)
        (if (null? (cdr t))
          (fringe (car t) s r)
          (fringe (car t) s (cons (cdr t) r))))
      ((symbol? t)
        (fringe r (cons t s) ()))
      ((null? r)
        s)
      (#t
        (fringe r s ()))
    )))
;(fringe '((a b) c . d) () ())
;==> (d c b a)

; helper function to recognize valid variable names
(define var-name? (lambda (x) (if (symbol? x) (if (eq? x '_) #f #t) #f)))
; simple tree-recursive implementation
(define zip                             ; extend `env` by binding names `x` to values `y`
  (lambda (x y env)
    (if (pair? x)
      (zip (car x) (car y) (zip (cdr x) (cdr y) env))
      (if (var-name? x)
        (cons (cons x y) env)
        env))))
;(zip '((a b) c . d) '((1 2 3) (4 5 6) (7 8 9)) global-env)
;==> ((a . +1) (b . +2) (c +4 +5 +6) (d (+7 +8 +9)) . #actor@55)
; interative (tail-recursive) implementation
(define zip-it                          ; extend `env` by binding names `x` to values `y`
  (lambda (x y xs ys env)
    (cond
      ((pair? x)
        (if (null? (cdr x))
          (zip-it (car x) (car y) xs ys env)
          (zip-it (car x) (car y) (cons (cdr x) xs) (cons (cdr y) ys) env)))
      ((var-name? x)
        (zip-it xs ys () () (cons (cons x y) env)))
      ((null? xs)
        env)
      (#t
        (zip-it xs ys () () env))
    )))
;(zip-it '((a b) c . d) '((1 2 3) (4 5 6) (7 8 9)) () () global-env)
;==> ((d (+7 +8 +9)) (c +4 +5 +6) (b . +2) (a . +1) . #actor@55)

; Quasi-Quotation based on `vau`
(define quasiquote
  (vau (x) e
    (if (pair? x)
      (if (eq? (car x) 'unquote)
        (eval (cadr x) e)
        (quasi-list x))
      x)))
(define quasi-list
  (lambda (x)
    (if (pair? x)
      (if (pair? (car x))
        (if (eq? (caar x) 'unquote-splicing)
          (append (eval (cadar x) e) (quasi-list (cdr x)))
          (cons (apply quasiquote (list (car x)) e) (quasi-list (cdr x))))
        (cons (car x) (quasi-list (cdr x))))
      x)))
;((lambda (x) `(x ,x ,(car x) ,(cdr x) ,@x 'x)) '(1 2 3))

; short-circuit logical connectives
($define! $and?
  ($vau x e
    ($cond
      ((null? x) #t)
      ((null? (cdr x)) (eval (car x) e))  ; tail context
      ((eval (car x) e) (apply (wrap $and?) (cdr x) e))
      (#t #f)
    )))
($define! $or?
  ($vau x e
    ($cond
      ((null? x) #f)
      ((null? (cdr x)) (eval (car x) e))  ; tail context
      ((eval (car x) e) #t)
      (#t (apply (wrap $or?) (cdr x) e))
    )))
; macro definitions using quasiquote templates
(define or
  (macro x
    (if (pair? x)
      (if (pair? (cdr x))
        `(let ((_test_ ,(car x)))  ; FIXME: need (gensym) here?
          (if _test_
            _test_
            (or ,@(cdr x))))
        (car x))  ; tail-call
      #f)))
(define expand-or
  (lambda (x)
    (if (pair? x)
      (if (pair? (cdr x))
        `(let ((_test_ ,(car x)))  ; FIXME: need (gensym) here?
          (if _test_
            _test_
            (or ,@(cdr x))))
        (car x))  ; tail-call
      #f)))
; macro definitions using explicit construction
(define expand-or
  (lambda (x)
    (if (pair? x)
      (if (pair? (cdr x))
        (list let (list (list '_test_ (car x)))  ; FIXME: need (gensym) here?
          (list if '_test_
            '_test_
            (cons 'or (cdr x))))
        (car x))  ; tail-call
      #f)))
(define or
  (macro x
    (expand-or x)))
;(or #f (eq? 0 1) (not 1) -1 (eq? 1 1) -no-eval-) ==> -1
(define and
  (macro x
    (if (pair? x)
      (if (pair? (cdr x))
        (list let (list (list '_test_ (car x)))  ; FIXME: need (gensym) here?
          (list if '_test_
            (cons 'and (cdr x))
            '_test_))
        (car x))  ; tail-call
      #t)))
(define or
  (macro x
    (if (pair? x)
      (if (pair? (cdr x))
        (list let (list (list '_test_ (car x)))  ; FIXME: need (gensym) here?
          (list if '_test_
            '_test_
            (cons 'or (cdr x))))
        (car x))  ; tail-call
      #f)))

; Little Schemer (4th edition)
(define atom? (lambda (x) (and (not (pair? x)) (not (null? x)))))
(define add1 (lambda (x) (+ x 1)))
(define sub1 (lambda (x) (- x 1)))

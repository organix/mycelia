;;;
;;; library.scm (extended library definitions)
;;;

(define current-env (vau _ e e))
(define macro (vau (frml . body) env
  (eval (list vau frml '_env_ (list eval (cons seq body) '_env_)) env) ))
;current-env
;==> #fexpr@6913
;(current-env)
;==> ()
;(define f (lambda x (seq (list list x (current-env) current-env))))
;(f 1 2 3)
;==> (#actor@575 (+1 +2 +3) ((x +1 +2 +3) (_ . #?)) #fexpr@6913)
;(define m (macro x (list list x (current-env) current-env)))
;(m 1 2 3)
;==> (#? #? #fexpr@6913)
;(define v (vau x e (eval (seq (list list x (current-env) current-env)) e) ))
;(define v (vau x e (eval (list list x (current-env) current-env) e) ))
;(v 1 2 3)
;==> (#? #? #fexpr@6913)
;(define q (macro x `(list x ,x ,@x 'x)))
;(q +1 +2 +3)
;==> (#? #? +1 +2 +3 x)
;(define qq (macro x `'(list x ,x ,@x 'x)))
;(qq +1 +2 +3)
;==> (list x (+1 +2 +3) +1 +2 +3 (quote x))

;(define qlist (macro x (list quote x)))
(define qlist (vau x _ x))
(define quote (vau (x) _ x))
(define seq (macro body _ (list (list* 'lambda '_ body))))  ; WARNING! introduces spurious local scope

(define integer? number?)  ; integers are currently the only number type implemented
(define equal?
  (lambda (x y)
    (if (pair? x)
        (if (equal? (car x) (car y))
            (equal? (cdr x) (cdr y))
            #f)
        (eq? x y))))

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
;; An alternative using an explicit helper function.
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

;; interative (tail-recursive) tree -> sequence
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
        (fringe r s ())) )))
;(fringe '((a b) c . d) () ())
;==> (d c b a)

;; match flat argument list w/ dotted-tail
(define zip                             ; extend `env` by binding names `x` to values `y`
  (lambda (x y env)
    (if (pair? x)
        (cons (cons (car x) (car y)) (zip (cdr x) (cdr y) env))
        (if (symbol? x)
            (cons (cons x y) env)         ; dotted-tail binds to &rest
            env))))
;; helper function to recognize valid variable names
(define var-name? (lambda (x) (if (symbol? x) (if (eq? x '_) #f #t) #f)))
;; simple tree-recursive implementation
(define zip                             ; extend `env` by binding names `x` to values `y`
  (lambda (x y env)
    (if (pair? x)
        (zip (car x) (car y) (zip (cdr x) (cdr y) env))
        (if (var-name? x)
            (cons (cons x y) env)
            env))))
;(zip '((a b) c . d) '((1 2 3) (4 5 6) (7 8 9)) global-env)
;==> ((a . +1) (b . +2) (c +4 +5 +6) (d (+7 +8 +9)) . #actor@55)
;; interative (tail-recursive) implementation
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
        (zip-it xs ys () () env)) )))
;(zip-it '((a b) c . d) '((1 2 3) (4 5 6) (7 8 9)) () () global-env)
;==> ((d (+7 +8 +9)) (c +4 +5 +6) (b . +2) (a . +1) . #actor@55)
;((lambda ((a b) c . d) (list a b c d)) '(1 2 3) '(4 5 6) '(7 8 9))
;==> (+1 +2 (+4 +5 +6) ((+7 +8 +9)))

;;
;; Quasi-Quotation based on `vau`
;;
(define quasiquote
  (vau (x) e
    (if (pair? x)
        (if (eq? (car x) 'unquote)
            (eval (cadr x) e)
            (quasi-list x e))
        x)))
(define quasi-list
  (lambda (x e)
    (if (pair? x)
        (if (pair? (car x))
            (if (eq? (caar x) 'unquote-splicing)
                (append (eval (cadar x) e) (quasi-list (cdr x) e))
                (cons (apply quasiquote (list (car x)) e) (quasi-list (cdr x) e)))
            (cons (car x) (quasi-list (cdr x) e)))
        x)))
;((lambda (x) `(x ,x ,(car x) ,(cdr x) ,@x 'x ,(current-env))) '(1 2 3))

(define gensym
  (lambda ()
    (cell Symbol_T (get-x '_) (get-y '_)) ))

;;;
;;; short-circuit logical connectives
;;;

;; examples from Kernel
($define! $and?
  ($vau x e
    ($cond
      ((null? x) #t)
      ((null? (cdr x)) (eval (car x) e))  ; tail context
      ((eval (car x) e) (apply (wrap $and?) (cdr x) e))
      (#t #f) )))
($define! $or?
  ($vau x e
    ($cond
      ((null? x) #f)
      ((null? (cdr x)) (eval (car x) e))  ; tail context
      ((eval (car x) e) #t)
      (#t (apply (wrap $or?) (cdr x) e)) )))
;; macro definitions using explicit construction
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
(define or (macro x (expand-or x)))
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
;; macro definitions using quasiquote templates
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
(define or (macro x (expand-or x)))
(define expand-or
  (lambda (x)
    (cond
      ((pair? x)
        (define t (gensym))
        `(seq
          (define ,t ,(car x))
          (if ,t ,t (or ,@(cdr x))) ))
      (#t
        #f) )))
(define or
  (macro x
    (cond
      ((pair? x)
        (define t (gensym))
        `(,seq
          (,define ,t ,(car x))
          (,if ,t ,t (,or ,@(cdr x))) ))
      (#t
        #f) )))

;;;
;;; macro helpers
;;;

;(define expand-with-gensyms
;  (lambda (syms . body)
(define with-gensyms
  (macro (syms . body)
    (define defsym (lambda (s) `(define ,s (gensym))))
    `(seq ,@(map defsym syms) ,@body) ))

;;;
;;; encapsulated (sealed) data-types
;;;

(define new-seal
  (lambda ()
    (define brand (gensym))
    (define seal
      (lambda (payload)
        (cell brand payload)))
    (define unseal
      (lambda (sealed)
        (if (eq? (get-t sealed) brand)
            (get-x sealed)
            #?)))
    (define sealed?
      (lambda objs
        (if (pair? objs)
            (if (eq? (get-t (car objs)) brand)
                (apply sealed? (cdr objs))
                #f)
            #t)))
    (list seal unseal sealed?)))

;;;
;;; secure immutable polymorphic abstract data-types
;;;

(define new-adt
  (lambda (dispatch)
    (define new
      (lambda (x y z)
        (cell dispatch x y z)))
    (define adt?
      (lambda objs
        (if (pair? objs)
            (if (eq? (get-t (car objs)) dispatch)
                (apply adt? (cdr objs))
                #f)
            #t)))
    (define fields
      (lambda (adt)
        (if (eq? (get-t adt) dispatch)
            (list (get-x adt) (get-y adt) (get-z adt))
            #?)))
    (list new adt? fields) ))
(define adt-call
  (lambda (adt . method)
    ;(print 'adt-call: (get-t adt) (get-x adt) (get-y adt) (get-z adt) (cons adt method))  ;; tracing...
    (if (actor? (get-t adt))
        (CALL (get-t adt) (cons adt method))
        #?)))

;; dict = { t:dict-dispatch, x:key, y:value, z:next }
(define dict-dispatch
  (lambda (this selector . args)
    (let (((key value next) (dict-fields this)))
      ;(print 'dict-dispatch: this (cons selector args) key value next)  ;; tracing...
      (cond
        ((eq? selector 'get)                                  ; (get <key>)
          (if (eq? key (car args))
              value
              (if (dict? next)
                  (adt-call next 'get (car args))
                  #?)))
        ((eq? selector 'has)                                  ; (has <key>)
          (if (eq? key (car args))
              #t
              (if (dict? next)
                  (adt-call next 'has (car args))
                  #f)))
        ((eq? selector 'set)                                  ; (set <key> <value>)
          (if (adt-call this 'has (car args))
              (new-dict (car args) (cadr args) (adt-call this 'delete (car args)))
              (new-dict (car args) (cadr args) this) ))
        ((eq? selector 'delete)                               ; (delete <key>)
          (if (eq? key (car args))
              next
              (if (dict? next)
                (new-dict key value (adt-call next 'delete (car args)))
                this)))
        ((eq? selector 'zip)                                  ; (zip)
          (if (dict? next)
              (cons (cons key value) (adt-call next 'zip))
              (cons (cons key value) next) ))
        (#t
          #?) )) ))
(define (new-dict dict? dict-fields) (new-adt dict-dispatch))
;> (define d0 (new-dict 'foo 123 ()))
;==> #unit
;> (dict? d0)
;==> #t
;> (adt-call d0 'has 'foo)
;==> #t
;> (adt-call d0 'has 'bar)
;==> #f
;> (adt-call d0 'get 'foo)
;==> +123
;> (adt-call d0 'get 'bar)
;==> #?
;> (adt-call d0 'delete 'foo)
;==> ()
;> (eq? (adt-call d0 'delete 'bar) d0)
;==> #t
;> (define d1 (adt-call d0 'set 'bar 456))
;==> #unit
;> (dict? d1)
;==> #t
;> (adt-call d1 'get 'foo)
;==> +123
;> (adt-call d1 'get 'bar)
;==> +456
;> (list d0 d1)
;==> (^6453 ^10641)
;> (list (get-t d0) (get-x d0) (get-y d0) (get-z d0))
;==> (#actor@5038 foo +123 ())
;> (list (get-t d1) (get-x d1) (get-y d1) (get-z d1))
;==> (#actor@5038 bar +456 ^6453)
;> (eq? (caddr (dict-fields d1)) d0)
;==> #t

;;;
;;; Little Schemer (4th edition)
;;;

(define add1 (lambda (x) (+ x 1)))
(define sub1 (lambda (x) (- x 1)))
(define atom? (lambda (x) (and (not (pair? x)) (not (null? x)))))
(define list? (lambda (p) (if (pair? p) (list? (cdr p)) (null? p))))
(define member? (lambda (x xs) (if (pair? xs) (or (eq? x (car xs)) (member? x (cdr xs))) #f)))

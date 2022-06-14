;;
;; library.scm (extended library definitions)
;;

(define cadar (lambda (x) (car (cdr (car x)))))

(define current-env (vau _ e e))
;(define qlist (macro x (list quote x)))
(define qlist (vau x _ x))
(define quote (vau (x) _ x))
(define seq (macro body _ (list (list* 'lambda '_ body))))

(define integer? number?)  ; integers are currently the only number type implemented

(define when
  (macro (cond . body)
    (list if cond (cons seq body) #unit)))

(define cond
  (macro clauses
    (if (null? clauses)
      #unit
      (apply
        (lambda ((test . body) . rest)
          (list if test (cons seq body) (cons cond rest)) )
        clauses) )))

(define map (lambda (f xs) (if (pair? xs) (cons (f (car xs)) (map f (cdr xs))) ())))  ; f takes only 1 arg

(define let
  (macro (bindings . body)
    (cons
      (list* lambda (map car bindings) body)
      (map cadr bindings))))

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
  (lambda (pred? list)
    (if (pair? list)
      (if (pred? (car list))
        (cons (car list) (filter pred? (cdr list)))
        (filter pred? (cdr list)))
      ())))

(define reduce
  (lambda (args binop zero)
    (if (null? args)
      zero
      ((lambda ((first . rest))
        (if (null? rest)
          first
          (binop first (reduce rest binop zero)))
      ) args)) ))
(define foldl
  (lambda (args binop zero)
    (if (null? args)
      zero
      ((lambda ((first . rest))
        (foldl rest binop (binop zero first))
      ) args)) ))
(define foldr
  (lambda (args binop zero)
    (if (null? args)
      zero
      ((lambda ((first . rest))
        (binop first (foldr rest binop zero))
      ) args)) ))

;(define reverse (lambda (xs) (if (pair? xs) (append (reverse (cdr xs)) (list (car xs))) xs)))  ; O(n^2) algorithm
(define reverse
  (lambda (xs)
    (foldl xs (lambda (x y) (cons y x)) ())))
; An alternative using an explicit helper function.
;(define push-pop (lambda (to from)
;  (if (pair? from) (push-pop (cons (car from) to) (cdr from)) to)))
(define reverse
  (lambda (xs)
    (define push-pop (lambda (to from)
      (if (pair? from) (push-pop (cons (car from) to) (cdr from)) to)))
    (push-pop () xs)))

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

; Little Schemer (4th edition)
(define atom? (lambda (x) (and (not (pair? x)) (not (null? x)))))
(define add1 (lambda (x) (+ x 1)))
(define sub1 (lambda (x) (- x 1)))

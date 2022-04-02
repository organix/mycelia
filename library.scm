;;
;; library.scm (extended library definitions)
;;

(define integer? number?)  ; integers are currently the only number type implemented

(define cond
  (macro clauses _
    (if (null? clauses)
      #unit
      (apply
        (lambda ((test . body) . rest)
          (list if test (cons seq body) (cons cond rest)) )
        clauses) )))

(define let
  (macro (bindings . body) _
    (cons
      (list* lambda (map car bindings) body)
      (map cadr bindings))))

;(define append (lambda (x y) (if (null? x) y (cons (car x) (append (cdr x) y)))))  ; two lists only
(define append
  (lambda x
    (if (pair? x)
      (apply (lambda (h . t)
        (if (pair? t)
          (if (null? h)
            (apply append t)
            (cons
              (car h)
              (apply append (cons (cdr h) t)) ))
        h)
      ) x)
      x) ))

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
    (define rcons (lambda (x y) (cons y x)))
    (foldl xs rcons ())))

(define provide
  (macro (symbols . body) _
    (list define symbols
      (list
        (list lambda ()
          (cons seq body)
          (cons list symbols)) ))))

(define newline
  (lambda ()
    (emit 10)))

; Little Schemer (4th edition)
(define atom? (lambda (x) (and (not (pair? x)) (not (null? x)))))
(define add1 (lambda (x) (+ x 1)))
(define sub1 (lambda (x) (- x 1)))

; Handy Macros
(define current-env (macro _ env env))
(define qlist (macro x _ (list quote x)))

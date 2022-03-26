;;
;; library.scm (extended library definitions)
;;

(define integer? number?)  ; integers are currently the only number type implemented

(define current-env (macro _ env env))
(define qlist (macro x _ (list quote x)))

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

(define provide
  (macro (symbols . body) _
    (list define symbols
      (list
        (cons lambda (cons ()
          (append body (list (cons list symbols))) )) ))))

;
; Extras
;

(define w (lambda (f) (f f)))  ; self-application
(define Y  ; applicative Y-combinator (recursive fixed-point)
	(lambda (f) 
		((lambda (g) 
			(g g)) 
			(lambda (h) 
				(lambda (x) 
					((f (h h)) x))))))

(define fact  ; recursive factorial (inefficient)
  (lambda (n)
    (if (> n 1)
      (* n (fact (- n 1)))
      1)))

; mutual recursion example (very inefficient)
(define even?
  (lambda (n)
    (if (= n 0) #t
      (odd? (- n 1)))))
(define odd?
  (lambda (n)
    (if (= n 0) #f
      (even? (- n 1)))))

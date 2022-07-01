;;
;; examples.scm (tutorial examples)
;;

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
;(define fact (lambda (n) (if (< n 2) 1 (* n (fact (- n 1))))))

(define fib  ; O(n^3) performance?
  (lambda (n)
    (if (< n 2)
      n
      (+ (fib (- n 1)) (fib (- n 2))))))
(define fib  ; exercise local `let` bindings
  (lambda (n)
    (if (< n 2)
      n
      (let ((x (fib (- n 1)))
            (y (fib (- n 2))))
        (+ x y)) )))
(define fib  ; exercise local `define` bindings
  (lambda (n)
    (if (< n 2)
      n
      (seq
        (define x (fib (- n 1)))
        (define y (fib (- n 2)))
        (+ x y)) )))

; mutual recursion example (very inefficient)
(define even?
  (lambda (n)
    (if (= n 0) #t
      (odd? (- n 1)))))
(define odd?
  (lambda (n)
    (if (= n 0) #f
      (even? (- n 1)))))

; Ackermann function
(define ack
  (lambda (n m)
    (cond ((eq? n 0)
            (+ m 1))
          ((eq? m 0)
            (ack (- n 1) 1))
          (#t
            (ack (- n 1) (ack n (- m 1)))) )))

(define member?
  (lambda (x xs)
    (if (pair? xs)
      (or (eq? x (car xs)) (member? x (cdr xs)))
      #f)))

;
; variadic mapping function
;
;(define map (lambda (f xs) (if (pair? xs) (cons (f (car xs)) (map f (cdr xs))) ())))  ; single-arg version
;(define foldr (lambda (op z xs) (if (pair? xs) (op (car xs) (foldr op z (cdr xs))) z)))
(define map
  (lambda (f . xs)
    (if (pair? (car xs))
      (cons
        (apply f (foldr (lambda (x y) (cons (car x) y)) () xs))
        (apply map (cons f (foldr (lambda (x y) (cons (cdr x) y)) () xs))))
      ())))

;
; pure-functional dequeue
;

(provide (new-dq dq-empty? dq-put dq-push dq-pop)
  (define new-dq
    (lambda ()
      '(()) ))
  (define dq-empty?
    (lambda ((p . q))
      (null? p)))
  (define dq-norm
    (lambda (p q)
      (if (null? p)
        (cons (reverse q) ())
        (cons p q))))
  (define dq-put
    (lambda ((p . q) x)
      (dq-norm p (cons x q))))
  (define dq-push
    (lambda ((p . q) x)
      (dq-norm (cons x p) q)))
  (define dq-pop
    (lambda (((x . p) . q))
      (list x (dq-norm p q)))))

;
; a stateful counting procedure
;
(provide (counter)
  (define env (current-env))
  (define count 0)
  (define counter
    (lambda ()
      (eval
        '(seq
          (define count (+ count 1))
          count)
        env))))

;
; scoping test case
;

(define x 0)
(define f
  (lambda ()
    (define x 1)
    ;(print 'x) (debug-print x) ; not in scheme
    (define g (lambda () x))
    (define x 2) ; (set! x 2) in scheme
    g))
(define h
  (lambda (x)
    (define x 3)
    ((f))))
; (h 4)
; ==> 2

;
; PEG number-list example (requires: parse.scm)
;   regex: ([\t-\r ]*[+-]?[0-9]+)*
;

(define peg-number-list
  (lambda (in)
    ((peg-star
      (peg-seq
        (peg-star
          (peg-or
            (peg-range 9 13)
            (peg-eq 32)))
        (peg-opt
          (peg-or
            (peg-eq 43)
            (peg-eq 45)))
        (peg-plus
          (peg-range 48 57))))
    in)))
(define number-list-example '(48 9 45 49 50 13 32 43 51 54 57 10))  ; "0\t-12\r +369\n"
(seq (print (peg-number-list number-list-example)) (newline))  ; test-case

;
; S-Expression Grammar
;
; sexpr  = _ (list | atom)
; list   = '(' sexpr* _ ')'
; atom   = number | symbol
; number = [0-9]+
; symbol = [a-zA-Z0-9]+
; _      = [ \t-\r]*
;

(define sexpr-grammar '(
  (sexpr
    (seq _ (alt list atom)))
  (list
    (seq (eq 40) (star sexpr) _ (eq 41)))
  (atom
    (alt number symbol))
  (number
    (plus (class DGT)))  ; digit
  (symbol
    (plus (class DGT LWR UPR SYM)))  ; excludes DLM "'(),;[]`{|}
  (_
    (star (class WSP)))  ; whitespace
))

; test-case "(CAR ( LIST 0 1)\t)"
(define sexpr-example '(40 67 65 82 32 40 32 76 73 83 84 32 48 32 49 41 9 41))

;
; ASM Experiments
;
(define print
  (cell Actor_T
    (cell VM_msg (fix->int -1)  ; #-1
      (cell VM_push a-print
        (cell VM_send (fix->int 0)  ; #0
          RV_UNIT)))
    ()))

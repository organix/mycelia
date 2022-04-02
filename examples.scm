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

; mutual recursion example (very inefficient)
(define even?
  (lambda (n)
    (if (= n 0) #t
      (odd? (- n 1)))))
(define odd?
  (lambda (n)
    (if (= n 0) #f
      (even? (- n 1)))))

(define member?
  (lambda (x xs)
    (if (pair? xs)
      (or (eq? x (car xs)) (member? x (cdr xs)))
      #f)))

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
; scoping test case
;

(define x 0)
(define f
  (lambda ()
    (define x 1)
    (print 'x) (debug-print x) ; not in scheme
    (define g
      (lambda ()
        x))
    (define x 2) ; (set! x 2) in scheme
    g))
(define h
  (lambda (x)
    (define x 3)
    ((f))))
; (h 4)
; ==> 2

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
    (and _ (or list atom)))
  (list
    (seq (eq 40) (star sexpr) _ (eq 41)))
  (atom
    (or number symbol))
  (number
    (plus (range 48 57)))  ; digit
  (symbol
    ;(plus (if ident-char?)))  ; not whitespace or ();'"`,[]{}|
    (plus (alt
      (range 97 122)  ; lowercase
      (range 65 90)   ; uppercase
      (range 48 57)   ; digit
    )))
  (_
    (star (range 0 32)))  ; whitespace
))

; test-case "(CAR ( LIST 0 1)\t)"
(define sexpr-example '(40 67 65 82 32 40 32 76 73 83 84 32 48 32 49 41 9 41))

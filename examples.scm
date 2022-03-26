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

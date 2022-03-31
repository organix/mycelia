;;
;; parse.scm (based on PEG3.hum)
;;

(define peg-empty
	(lambda (in)
		(list #t () in)))
(define peg-fail
	(lambda (in)
		(list #f in)))
(define peg-any
	(lambda (in)
		(if (null? in)
			(list #f in)
			(let (((token . rest) in))
				(list #t token rest)))))
(define peg-eq
	(lambda (value)
		(lambda (in)
			(if (null? in)
				(list #f in)
				(let (((token . rest) in))
					(if (equal? token value)
						(list #t token rest)
						(list #f in)))))))
(define peg-if
	(lambda (test?)
		(lambda (in)
			(if (null? in)
				(list #f in)
				(let (((token . rest) in))
					(if (test? token)
						(list #t token rest)
						(list #f in)))))))
(define peg-or
	(lambda (left right)
		(lambda (in)
			(let (((ok . state) (left in)))
				(if ok
					(cons #t state)
					(right in))))))
(define peg-and
	(lambda (left right)
		(lambda (in)
			(let (((ok . state) (left in)))
				(if ok
					(let (((lval rest) state))
						(let (((ok . state) (right rest)))
							(if ok
								(let (((rval rest) state))
									(list #t (cons lval rval) rest))
								(list #f in))))
					(list #f in))))))
(define peg-alt
	(lambda pegs
		(if (pair? pegs)
			(peg-or (car pegs) (apply peg-alt (cdr pegs)))
			peg-fail)))
(define peg-seq
	(lambda pegs
		(if (pair? pegs)
			(peg-and (car pegs) (apply peg-seq (cdr pegs)))
			peg-empty)))
(define peg-opt
	(lambda (peg)
		(peg-or peg peg-empty)))
(define peg-star
	(lambda (peg)
		(lambda (in)
			((peg-opt (peg-and peg (peg-star peg))) in))))
(define peg-plus
	(lambda (peg)
		(peg-and peg (peg-star peg))))
(define peg-not
	(lambda (peg)
		(lambda (in)
			(let (((ok . state) (peg in)))
				(if ok
					(list #f in)
					(list #t () in))))))
(define peg-peek
	(lambda (peg)
		(peg-not (peg-not peg))))

;
; test fixture
;
; expr   = term ([-+] term)*
; term   = factor ([*/] factor)*
; factor = '(' expr ')' | number
; number = [0-9]+
;
(define peg-expr
	(lambda (in)
		((peg-seq
			peg-term
			(peg-star (peg-seq
				(peg-or (peg-eq 45) (peg-eq 43))  ; minus/plus
				peg-term)))
		in)))
(define peg-term
	(lambda (in)
		((peg-seq
			peg-factor
			(peg-star (peg-seq
				(peg-or (peg-eq 42) (peg-eq 47))  ; star/slash
				peg-factor)))
		in)))
(define peg-factor
	(lambda (in)
		((peg-alt
			(peg-seq
				(peg-eq 40)  ; open paren
				peg-expr
				(peg-eq 41))  ; close paren
			peg-number)
		in)))
(define peg-number
	(lambda (in)
		((peg-plus peg-digit)
		in)))
(define peg-digit
	(lambda (in)
		((peg-alt
			(peg-eq 48)  ; zero
			(peg-eq 49)  ; one
			(peg-eq 50)  ; two
			(peg-eq 51)  ; three
			(peg-eq 52)  ; four
			(peg-eq 53)  ; five
			(peg-eq 54)  ; six
			(peg-eq 55)  ; seven
			(peg-eq 56)  ; eight
			(peg-eq 57))  ; nine
		in)))

(define expr (list 49 43 50 42 51 45 57 48))  ; 1 + 2 * 3 - 9 0
(peg-expr expr)  ; test-case

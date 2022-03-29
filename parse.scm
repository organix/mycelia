;;
;; parse.scm (based on PEG3.hum)
;;

(define match-empty
	(lambda (in)
		(list #t () in)))
(define match-fail
	(lambda (in)
		(list #f in)))
(define match-any
	(lambda (in)
		(if (null? in)
			(list #f in)
			(let (((token . rest) in))
				(list #t token rest)))))
(define match-eq
	(lambda (value)
		(lambda (in)
			(if (null? in)
				(list #f in)
				(let (((token . rest) in))
					(if (equal? token value)
						(list #t token rest)
						(list #f in)))))))
(define match-if
	(lambda (test?)
		(lambda (in)
			(if (null? in)
				(list #f in)
				(let (((token . rest) in))
					(if (test? token)
						(list #t token rest)
						(list #f in)))))))
(define match-or
	(lambda (left right)
		(lambda (in)
			(let (((ok . state) (left in)))
				(if ok
					(cons #t state)
					(right in))))))
(define match-and
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
(define match-alt
	(lambda matches
		(if (pair? matches)
			(match-or (car matches) (apply match-alt (cdr matches)))
			match-fail)))
(define match-seq
	(lambda matches
		(if (pair? matches)
			(match-and (car matches) (apply match-seq (cdr matches)))
			match-empty)))
(define match-opt
	(lambda (match)
		(match-or match match-empty)))
(define match-star
	(lambda (match)
		(lambda (in)
			((match-opt (match-and match (match-star match))) in))))
(define match-plus
	(lambda (match)
		(match-and match (match-star match))))
(define match-not
	(lambda (match)
		(lambda (in)
			(let (((ok . state) (match in)))
				(if ok
					(list #f in)
					(list #t () in))))))
(define match-peek
	(lambda (match)
		(match-not (match-not match))))

;
; test fixture
;
; expr   = term ([-+] term)*
; term   = factor ([*/] factor)*
; factor = '(' expr ')' | number
; number = [0-9]+
;
(define match-expr
	(lambda (in)
		((match-seq
			match-term
			(match-star (match-seq
				(match-or (match-eq 45) (match-eq 43))  ; minus/plus
				match-term)))
		in)))
(define match-term
	(lambda (in)
		((match-seq
			match-factor
			(match-star (match-seq
				(match-or (match-eq 42) (match-eq 47))  ; star/slash
				match-factor)))
		in)))
(define match-factor
	(lambda (in)
		((match-alt
			(match-seq
				(match-eq 40)  ; open paren
				match-expr
				(match-eq 41))  ; close paren
			match-number)
		in)))
(define match-number
	(lambda (in)
		((match-plus match-digit)
		in)))
(define match-digit
	(lambda (in)
		((match-alt
			(match-eq 48)  ; zero
			(match-eq 49)  ; one
			(match-eq 50)  ; two
			(match-eq 51)  ; three
			(match-eq 52)  ; four
			(match-eq 53)  ; five
			(match-eq 54)  ; six
			(match-eq 55)  ; seven
			(match-eq 56)  ; eight
			(match-eq 57))  ; nine
		in)))

(define expr (list 49 43 50 42 51 45 57 48))  ; 1 + 2 * 3 - 9 0
(match-expr expr)  ; match test-case

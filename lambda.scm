;;;
;;; Lambda Calculus
;;; w/ De Bruijn indexed variables
;;;

; lookup: (cust index)
(define empty-env
  (CREATE
    (BEH (cust _index)
      ;(seq (print 'empty-env) (debug-print _index))
      (SEND cust #undefined))))
(define bound-beh
  (lambda (value next)
    (BEH (cust index)
      ;(seq (print 'bound-beh) (debug-print index))
      (define index (- index 1))
      (if (zero? index)
        (SEND cust value)
        (SEND next (list cust index))))))

; eval: (cust env)
(define value-beh
  (BEH (cust _)               ; eval
    (SEND cust SELF)))
(define const-beh
  (lambda (value)
    (BEH (cust _)             ; eval
      ;(seq (print 'const-beh) (debug-print value))
      (SEND cust value))))
(define var-beh
  (lambda (index)
    (BEH (cust env)           ; eval
      ;(seq (print 'var-beh) (debug-print index))
      (SEND env (list cust index)))))

; apply: (cust param env)
(define op-lambda             ; (lambda <body>)
  (CREATE
    (BEH (cust body . opt-env)
      ;(seq (print 'op-lambda) (debug-print (list* cust body opt-env)))
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND cust            ; apply
          (CREATE (appl-beh (CREATE (oper-beh body)) (car opt-env))))
      ))))
(define oper-beh
  (lambda (body)
    (BEH (cust arg . opt-env)
      ;(seq (print 'oper-beh) (debug-print (list* cust arg opt-env)))
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND body            ; apply
          (list cust (CREATE (bound-beh arg (car opt-env)))))
      ))))
(define appl-beh
  (lambda (oper senv)
    (BEH (cust param . opt-env)
      ;(seq (print 'appl-beh) (debug-print (list* cust param opt-env)))
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND param           ; apply
          (list (CREATE (k-apply-beh cust oper senv)) (car opt-env)))
      ))))
(define k-apply-beh
  (lambda (cust oper env)
    (BEH arg
      ;(seq (print 'k-apply-beh) (debug-print arg))
      (SEND oper
        (list cust arg env)))))

(define comb-beh
  (lambda (comb param)
    (BEH (cust env)           ; eval
      ;(seq (print 'comb-beh) (debug-print (list comb param)))
      (SEND comb
        (list (CREATE (k-call-beh cust param env)) env)))))
(define k-call-beh
  (lambda (cust param denv)
    (BEH oper
      ;(seq (print 'k-call-beh) (debug-print oper))
      (SEND oper
        (list cust param denv)))))

;
; testcase: ((lambda (var 1)) (const 42))
;
(define a-test-eval
  (CREATE
    (BEH _
      ;(seq (print 'empty-env) (debug-print empty-env))
      ;(seq (print 'op-lambda) (debug-print op-lambda))
      (define var-1 (CREATE (var-beh 1)))
      ;(seq (print 'var-1) (debug-print var-1))
      (define const-42 (CREATE (const-beh 42)))
      ;(seq (print 'const-42) (debug-print const-42))
      (define fn-id (CREATE (comb-beh op-lambda var-1)))
      ;(seq (print 'fn-id) (debug-print fn-id))
      (define expr
        (CREATE (comb-beh
          fn-id
          const-42)))
      ;(SEND const-42 (list a-printer empty-env))  ; eval
      ;(SEND var-1 (list a-printer empty-env))  ; eval
      ;(SEND fn-id (list a-printer empty-env))  ; eval
      (SEND expr (list a-printer empty-env))  ; eval
    )))

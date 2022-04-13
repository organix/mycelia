;;;
;;; Lambda Calculus
;;; w/ De Bruijn indexed variables
;;;

; lookup: (cust index)
(define a-empty
  (CREATE
    (BEH (cust _)
      (SEND cust #undefined))))
(define bound-beh
  (lambda (value next)
    (BEH (cust index)
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
      (SEND cust value))))
(define var-beh
  (lambda (index)
    (BEH (cust env)           ; eval
      (SEND env (list cust index)))))

; apply: (cust param env)
(define a-lambda              ; (lambda <body>)
  (CREATE
    (BEH (cust body . opt-env)
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND cust            ; apply
          (CREATE (appl-beh (CREATE (oper-beh body)) (car opt-env))))
      ))))
(define oper-beh
  (lambda (body)
    (BEH (cust arg . opt-env)
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND body            ; apply
          (list cust (CREATE (bound-beh arg (car opt-env)))))
      ))))
(define appl-beh
  (lambda (oper senv)
    (BEH (cust param . opt-env)
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND param           ; apply
          (list (CREATE (k-apply-beh cust oper senv)) (car opt-env)))
      ))))
(define k-apply-beh
  (lambda (cust oper env)
    (BEH (arg)
      (SEND oper
        (list cust arg env)))))

(define comb-beh
  (lambda (comb param)
    (BEH (cust env)           ; eval
      (SEND comb
        (list (CREATE (k-call-beh cust param env)) env)))))
(define k-call-beh
  (lambda (cust param denv)
    (BEH (oper)
      (SEND oper
        (list cust param denv)))))

;
; test-case: ((lambda (var 0)) (const 42))
;

(define var-0
  (CREATE (var-beh 0)))
(define const-42
  (CREATE (const-beh 42)))
(define expr
  (CREATE (comb-beh
    (CREATE (comb-beh
      a-lambda var-0))
    const-42)))
(SEND expr (list a-printer a-empty))

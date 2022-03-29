;;
;; actor.scm (meta-actor facilities)
;;

(define sink-beh (BEH _))
(define a-sink (CREATE sink-beh))

(define a-printer
  (CREATE
    (BEH msg
      (seq (print msg) (newline))
    )))

(define fwd-beh
  (lambda (cust)
    (BEH msg
      (SEND cust msg)
    )))

(define once-beh
  (lambda (cust)
    (BEH msg
      (SEND cust msg)
      (BECOME sink-beh)
    )))

(define label-beh
  (lambda (cust label)
    (BEH msg
      (SEND cust (cons label msg))
    )))

(define tag-beh
  (lambda (cust)
    (BEH msg
      (SEND cust (cons SELF msg))
    )))

(define a-testcase
  (CREATE
    (BEH _
      ;(seq (print 'SELF) (debug-print SELF) (newline))
      (define a-fwd (CREATE (fwd-beh a-printer)))
      (define a-label (CREATE (label-beh a-fwd 'tag)))
      (define a-once (CREATE (once-beh a-label)))
      (SEND a-fwd '(1 2 3))
      (SEND a-once '(a b c))
      (SEND a-once '(x y z))
    )))

;(define a-testfail (CREATE (BEH _ (SEND a-printer 'foo) (FAIL 420) (SEND a-printer 'bar))))

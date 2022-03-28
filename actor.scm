;;
;; actor.scm (meta-actor facilities)
;;

(define sink-beh (BEH _))
(define sink (CREATE sink-beh))
(define fwd-beh
  (lambda (cust)
    (BEH msg
      (SEND cust msg))))
(define once-beh
  (lambda (cust)
    (BEH msg
      (SEND cust msg)
      (BECOME sink-beh))))
(define label-beh
  (lambda (cust label)
    (BEH msg
      (SEND cust (cons label msg)))))
(define tag-beh
  (lambda (cust)
    (BEH msg
      (SEND cust (cons SELF msg)))))

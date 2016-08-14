;; lisp envirnoment function/debugging

(de filter (p l)
  (cond ((null? l) l)
        ((p (car l)) (cons (car l) (filter p (cdr l))))
        (t (filter p (cdr l))) ) )
  
(de intern (s) (read s))

(define *TR)
(de trace (f)
  (if (func? f) (trace (funame f))
    (set! *TR (cons f *TR))))

(de untrace (f)
  (if (func? f) (untrace (funame f))
     (set! *TR (filter (lambda (x) (not (eq f x))) *TR))))

;; lisp envirnoment functions
;; consider moving to C as it takes no heap space!

(define intern read)

; tail recursive "efficent" recurse
(de reverse (l a)
  (if (null? l) a
     (reverse (cdr l) (cons (car l) a))))

; fixes: (append2 '(a) 'b) => (a b)
(de fixend (a) 
  (cond ((null? a) a)
        ((atom? a) (cons a))
        (t a)))

(de append2 (a b)
  (cond ((null? a) (fixend b))
        ((null? b) a)
        ((atom? a) (cons a (fixend b)))
        (t (cons (car a) (append2 (cdr a) b)))))

(de append L
  (if (null? L) nil
     (append2 (car L) (apply append (cdr L)))))

;; sorting
(de merge (a b lt)
  (cond ((null? a) b)
        ((null? b) a)
        ((lt (car a) (car b))
         (cons (car a) (merge (cdr a) b lt)) )
        (t
         (cons (car b) (merge a (cdr b) lt)) ) ) )

;; tracing functions

(define *TR)
(de trace (f)
  (if (func? f) (trace (funame f))
    (set! *TR (cons f *TR))))

(de untrace (f)
  (if (func? f) (untrace (funame f))
     (set! *TR (filter (lambda (x) (not (eq f x))) *TR))))

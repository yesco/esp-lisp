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

(de merge (a b cmp)
  (cond ((null? a) b)
        ((null? b) a)
        ((cmp (car a) (car b)) (cons (car a) (merge (cdr a) b cmp)))
        (t (cons (car b) (merge a (cdr b) cmp)))))

(define sort)

(de qsort (a L cmp)
  (if (null? L) (cons a)
    (append
      (sort (filter (lambda (x) (cmp x a)) L) cmp)
      (list a)
      (sort (filter (lambda (x) (not (cmp x a))) L) cmp))))

(de sort (L f)
  (if (null? L) nil
    (qsort (car L) (cdr L) (or f <))))

;; tracing functions

(define *TR)
(de trace (f)
  (if (func? f) (trace (funame f))
    (set! *TR (cons f *TR))))

(de untrace (f)
  (if (func? f) (untrace (funame f))
     (set! *TR (filter (lambda (x) (not (eq f x))) *TR))))


;; lisp envirnoment functions

;; tracing functions
(define *TR)
(de trace (f)
  (if (func? f) (trace (funame f))
    (set! *TR (cons f *TR))))

(de untrace (f)
  (if (func? f) (untrace (funame f))
     (set! *TR (filter (lambda (x) (not (eq f x))) *TR))))

;; misc

(define intern read)

;; consider moving some to C as it takes no heap space!

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

(de drop (xs n) (nthcdr n xs))

(de take (xs n)
  (cond ((null? xs) nil)
        ((eq n 0) nil)
        (t (cons (car xs) (take (cdr xs) (- n 1))))))

;; sorting
(de merge (a b <)
  (cond ((null? a) b)
        ((null? b) a)
        ((< (car a) (car b))
         (cons (car a) (merge (cdr a) b <)) )
        (t
         (cons (car b) (merge a (cdr b) <)) ) ) )

(de sort (L <)
  (cond ((null? L) nil)
        ((null? (cdr L)) L)
        (t (let* ((l (/ (length L) 2))
                  (a (take L l))
                  (b (drop L l)))
             (merge (sort a <) (sort b <) <)))))

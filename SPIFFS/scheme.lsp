;; Scheme compatibly layer

; string functions
(define string< <)
(define string> >)
(define string= =)

; symbol functions
(define string->symbol intern)
(define symbol->string concat)
(define string-append concat)
(define string-concatenate concat)
(define symbol?< string<)

; misc/test
(define else t)

(define display princ)
(define newline terpri)

(define eq? eq)
(define eqv? eq)
(define equal? equal)
(define pair? cons?)

; list functions

(define (cadr x) (car (cdr x)))
(define (cddr x) (cdr (cdr x)))
(define (caar x) (car (car x)))
(define (cdar x) (cdr (car x)))

(define (caadr x) (car (car (cdr x))))
(define (caddr x) (car (cdr (cdr x))))
(define (caaar x) (car (car (car x))))
(define (cadar x) (car (cdr (car x))))

(define (cdadr x) (cdr (car (cdr x))))
(define (cdddr x) (cdr (cdr (cdr x))))
(define (cdaar x) (cdr (car (car x))))
(define (cddar x) (cdr (cdr (car x))))

(de sort (l lt)
  (cond ((null? l) l)
        (t (let* ((ab (split l lt (car l)))
                  (a (sort (car ab)))
                  (b (sort (cdr ab))) )
              (merge a b lt)))))

(de merge (a b lt)
  (cond ((null? a) b)
        ((null? b) a)
        ((lt (car a) (car b))
         (cons (car a) (merge (cdr a) b lt)) )
        (t
         (cons (car b) (merge a (cdr b) lt)) ) ) )

(de list-ref (clist i) (nth i clist))

(de drop (xs n)
  (if (eq n 0) xs
    (drop (cdr xs) (- n 1))))

(de take (xs n)
  (cond ((null? xs) nil)
        ((eq n 0) nil)
        (t (cons (car xs) (take (cdr xs) (- n 1))))))

; IO

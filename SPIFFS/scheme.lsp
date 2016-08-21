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
(define symbol< string<)

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

(de list-ref (clist i) (nth i clist))

(de or-list (L)
  (cond ((atom? L) L)
        ((null? (car L)) (or-list (cdr L)))
        (t (car L))))

; (map list '(1) '(a b c) '(d e f g h i))
(de maphlp (f L)
  (if (not (or-list L)) nil
     (cons (apply f (mapcar car L)) (maphlp f (mapcar cdr L)))))

(de map fL (maphlp (car fL) (cdr fL)))

; IO

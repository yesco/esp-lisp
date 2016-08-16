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

; list functions
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


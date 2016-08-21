(define fac0 
  (lambda (f)
    (lambda (n)
      (if (= n 0)
          1
        (* n (f (- n 1)))))))

(define Y (lambda (f) ((lambda (x) (x x))
                       (lambda (x) (f (lambda (y) ((x x) y)))))))
(define fac (Y fac0))

(fac 10)


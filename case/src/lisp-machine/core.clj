(ns lisp-machine.core
  (:require [scad-clj.model :refer :all]
            [scad-clj.scad :refer :all]))

(defn print-scad [filename shape]
  (spit (str filename ".scad")
        (write-scad
         (color [0.6 0.6 0.6] shape))))


(print-scad
 "back"
 (difference
  (cube 65 37 35)
  (translate [0 0 1.25] (cube 60 32 35))))

(print-scad
 "front"
 (binding [*center* false]
   (union
    (translate [6 25 4] (cube 50 10 2))
    (difference
     ; Base
     (cube 65 37 5)

     ; Input hole
     (translate [40 7 -1] (cube 14 16 10 ))
     (translate [32 7 5] (rotate (/ pi 7) [0 1 0] (cube 17 16 10 )))

     ; Venting holes
     (apply union
            (map #(translate [7 (* % 4) 3] (cube 20 2 5 ))
                 (range 1 6)))

     ; Bevels
     (translate [-21 -2 -20] (rotate (/ pi 4) [0 -1 0] (cube 40 40 10 )))
     (translate [43 -2 20] (rotate (/ pi 4) [0 1 0] (cube 40 40 10 )))))))


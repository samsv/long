(comment "match x\n"
         "| 1 do x\n"
         "| 2 do 2 * x\n"
         "| 3 do 9 * x\n"
         "end";
         )
(do
  (= $1 x)
  (|
   (if (is-number? $1)
     (if (== $1 1)
       (do x)
       (if (== $1 2)
         (do (* 2 x))
         (if (== $1 3)
           (do (* 9 x))
           $fail)))
     $fail)
   (match-fail $1)))

(comment
  "match x\n"
  "| 1 do 1\n"
  "| 2 do 2 * x\n"
  "| a do a\n"
  "end";
  )
(do
  (= $1 x)
  (|
   (|
    (if (is-number? $1)
      (if (== $1 1)
        (do 1)
        (if (== $1 2)
          (do (* 2 x))
          $fail))
      $fail)
    (do
      (= a $1)
      (do a)))
   (match-fail $1)))

(comment
  "match x\n"
  "| 1 do 1\n"
  "| a do a\n"
  "| 2 do 2 * x\n"
  "end";
  )
(do
  (= $1 x)
  (|
   (|
    (if (is-number? $1)
      (if (== $1 1)
        (do 1)
        $fail)
      $fail)
    (|
     (do (= a $1)
         (do a))
     (if (is-number? $1)
       (if (== $1 2)
         (do (* 2 x))
         $fail)
       $fail)))
   (match-fail $1)))


(do
  (= $1 x)
  (|
   (if (is-number? $1)
     (if
       (== $1 1)
       (do 1)
       $fail)
     $fail)
   (|
    (do
      (= a $1)
      (do a))
    (|
     (if
       (is-number? $1)
       (if
         (== $1 2)
         (do
           (* 2 x)) $fail) $fail)
     (match-fail $1)))))

(comment
  "match x \n"
  "| 1 do -1\n"
  "|\"hello\" do 1\n"
  "| 2 do 2\n"
  "end";
  )
(do
  (= $1 x)
  (|
   (if
     (is-number? $1)
     (if
       (== $1 1)
       (do -1)
       (if
         (== $1 2)
         (do 2) $fail))
     (if
       (is-str? $1)
       (if
         (== $1 "hello")
         (do 1) $fail) $fail))
   (match-fail $1)))


(comment
  "match x \n"
  "| 1 do 5\n"
  "|\"hello\" do 1\n"
  "| 2 do 2\n"
  "|\"world\" do 1\n"
  "end";
  )
(do
  (= $1 x)
  (|
   (if
     (is-number? $1)
     (if
       (== $1 1)
       (do 5)
       (if
         (== $1 2)
         (do 2)
         $fail))
     (if
       (is-str? $1)
       (if
         (== $1 "hello")
         (do 1)
         (if
           (== $1 "world")
           (do 1)
           $fail))
       $fail))
   (match-fail $1)))

(do
  (= $1 x)
  (|
   (if
     (is-str? $1)
     (if
       (== $1 "hello")
       (do 1)
       (if
         (== $1 "world")
         (do 1)
         $fail))
     (if
       (is-number? $1)
       (if
         (== $1 1)
         (do 5)
         (if
           (== $1 2)
           (do 2)
           $fail))
       $fail))
   (match-fail $1)))

(comment
    "match x \n"
    "| (1, 2) do 1\n"
    "| (1, 2, 3) do 2\n"
    "| (1, 4) do 3\n"
    "end";
  )
(do
  (= $1 x)
  (|
    (if
      (is-tuple? $1 2)
      (do
        (= $5
           (nth $1 0))
        (do
          (= $6
             (nth $1 1))
          (if
            (is-number? $5)
            (if
              (== $5 1)
              (if
                (is-number? $6)
                (if
                  (== $6 2)
                  (do 1)
                  (if
                    (== $6 4)
                    (do 3) $fail)) $fail) $fail) $fail)))
      (if
        (is-tuple? $1 3)
        (do
          (= $2
             (nth $1 0))
          (do
            (= $3
               (nth $1 1))
            (do
              (= $4
                 (nth $1 2))
              (if
                (is-number? $2)
                (if
                  (== $2 1)
                  (if
                    (is-number? $3)
                    (if
                      (== $3 2)
                      (if
                        (is-number? $4)
                        (if
                          (== $4 3)
                          (do 2) $fail) $fail) $fail) $fail) $fail) $fail)))) $fail))
    (match-fail $1)))

(|
  (if
    (is-tuple? x 2)
    (do
      (= $1
         (nth x 0))
      (= $2
         (nth x 1))
      (|
        (if
          (is-number? $1)
          (if
            (== $1 1)
            (|
              (if
                (is-number? $2)
                (if
                  (== $2 2)
                  (do 1) $fail) $fail) $fail)
            (if
              (== $1 1)
              (|
                (if
                  (is-number? $2)
                  (if
                    (== $2 4)
                    (do 3) $fail) $fail) $fail) $fail)) $fail) $fail))
    (if
      (is-tuple? x 3)
      (do
        (= $3
           (nth x 0))
        (= $4
           (nth x 1))
        (= $5
           (nth x 2))
        (|
          (if
            (is-number? $3)
            (if
              (== $3 1)
              (|
                (if
                  (is-number? $4)
                  (if
                    (== $4 2)
                    (|
                      (if
                        (is-number? $5)
                        (if
                          (== $5 3)
                          (do 2) $fail) $fail) $fail) $fail) $fail) $fail) $fail) $fail) $fail)) $fail))
  (match-fail x))

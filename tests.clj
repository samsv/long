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
  "| 1 do \n"
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
        (do )
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
  "| 1 do \n"
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
        (do 0)
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
  (if
    (is-number? $1)
    (if
      (== $1 1)
      (do 0)
      $fail)
    (if
      ( $1)
      (if
        (== $1 a)
        (do a) $fail)
      (if
        (is-number? $1)
        (if
          (== $1 2)
          (do
            (* 2 x))
          $fail)
        $fail))))

(comment
  "match x \n"
  "| 1 do \n"
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
       (do 0)
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
  "| 1 do \n"
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
       (do )
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
        (do 0)
        (if
          (== $1 2)
          (do 2)
          $fail))
      $fail)))

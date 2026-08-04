# Pattern Matching

The pattern matching algorithm used is from the book `THE IMPLEMENTATION OF FUNCTIONAL PROGRAMMING LANGUAGES` by `Simon L Peyton Jones`.
This document details the algorithms and implementation from the book. Basic knowledge of lambda calculus is necessary.

## Pattern Matching in lambda calculus
Consider the match expression
```elixir
match x
| p1 = e1
| p2 = e2
| p3 = e3
...
end
```
Where `pi` is the `i`-th pattern and `ei` is it `i`-th expression associated with the pattern.
We can translate it to lambda calculus as
```
\x.(
      ((\P1'.E1')x)
    | ((\P2'.E2')x)
    | ((\P3'.E3')x)
    ...
    | nil
)
```
Where `\x.E` denotes a function which takes `x` as an argument (think of `\` as part a of λ) and has as a body the expression `E`.
`Pi'` is the pattern `pi` translated into lambda calculus and `Ei'` is the expression `ei` translated into lambda calculus.
`(\Pi'.Ei')x` means `apply the function \Pi'.Ei' to x`. We can think of the function `\P.E` as `match the passed argument against P.
If the pattern matches, bind P's variables and return E, otherwise return FAIL`. FAIL is a sentinel internal to the match machinery:
no `lóng` expression can evaluate to it, which keeps "the clause did not match" distinct from "the clause matched and its body
returned an error value". The infix shortcut operator `|` can be defined as
```
a | b = a,      if a != FAIL
FAIL | b = b
FAIL x = FAIL
```
That is, `a | b` returns `a`, if `a` is a non FAIL value, otherwise returns `b`. The last rule makes a partial application that already
FAILed keep FAILing, which the multiple argument translation below relies on.

The `nil` on the last alternative makes a match with no matching pattern evaluate to nil, like an `if` without an `else`.

For multiple arguments, i.e.
```elixir
match (x, y)
| (p11, p12) = e1
| (p21, p22) = e2
| (p31, p32) = e3
...
end
```
we translate into
```
\x.\y.(
      ((\P11'.\P12'.E1') x y)
    | ((\P21'.\P22'.E2') x y)
    | ((\P31'.\P32'.E3') x y)
    ...
    | nil
)
```

Example: `xor`
```ml
xor =
    match (a, b)
    | (false, y) = y
    | (true, true) = false
    | (true, false) = true
    end
```
Translates as
```
\a.\b.(
      ((\FALSE.\y.y) a b)
    | ((\TRUE.\TRUE.FALSE) a b)
    | ((\TRUE.\FALSE.TRUE) a b)
    | nil
)
```
We can also define guards using lambda calculus. Consider the function
```ml
fun first_neg_or_last(lst)
| [x, ..xs] when x < 0 = x
| [x, ..[]] = x
| [x, ..xs] = first_neg_or_last(xs)
end
```
We can translate it to
```
first_neg_or_last = \v.(
      ( (\(cons x xs).(if (< x 0) x FAIL) v)  )
    | ( (\(cons x []).x) v)
    | ( (\(cons x xs).first_neg_or_last xs) v )
    | nil
)
```

TODO: LHS pattern matches.

## Case and Match
Suppose a new lambda calculus function, `case`. It performs a simple pattern match on the type of a variable. I.e. suppose a pattern match for
a list variable. Case would then be
```
\x.case x of
    CONS head tail => (E1 head tail)
    NIL => E2
```
where `E1` may be a more complex pattern match of the list head and tail. We will use the `case` function as an optimization of the `|` operator for some
patterns.

We will also introduce a `match` function, to facilitate reading the sequential lambda expression (not to be confused with the `match` `lóng` function).
A lambda expression in the form
```
      ((\P11'...\P1N'.E1') u1 ... un)
    | ((\P21'...\P2N'.E2') u1 ... un)
    ...
    | ((\PM1'...\PMN'.EM') u1 ... un)
    | nil
```
can be translated to
```
match
    [ u1, ..., un ]
    [ ( [P11', ..., P1N'], E1' ),
      ...
      ( [PM1', ..., PMN'], EM' ) ]
    nil
```
where `match` takes 3 arguments. The first is the list of variables, the second an array of pairs, where the first element is an array of conditions for
each variable and the second is the expression associated with the pattern list. The last argument is the default value to be returned if all patterns fail.

## Pattern Matching Rules
Let's consider a function `f`, in the form
```
fun fn(f, lst1, lst2)
| (f, [], ys) = A(f, ys)
| (f, [x, ..xs], []) = B(f, x, xs)
| (f, [x, ..xs], [y, ..ys]) = C(f, x, xs, y, ys)
end
```
where `A`, `B` and `C` are defined elsewhere. We may translate it into lambda calculus
```
fn =
\u1.\u2.\u3.(
      ((\f.\NIL.\ys.A f ys) u1 u2 u3)
    | ((\f.\(CONS x xs).\NIL.B f x xs) u1 u2 u3)
    | ((\f.\(CONS x xs).\(CONS y ys).C f x xs y ys) u1 u2 u3)
    | nil
)
```
we can then convert it into the `match` function.
```
fn =
\u1.\u2.\u3. match
    [ u1, u2, u3 ]
    [ ( [f, NIL, ys], (A f ys) ),
      ( [f, CONS x xs, NIL], (B f x xs) ),
      ( [f, CONS x xs, CONS y ys], (C f x xs y ys) ) ]
    nil
```
Now, we can start compiling the expression following some simple rules.

### The Variable Rule
Notice how `u1` always matches a variable. We can thus remove `u1` from the pattern match and substitute all occurences of `f` in the expressions with `u1`.
Although all variables have the same name, this rule would work even if we renamed `f` in each pattern matching case to different names. Thus, the
function becomes:
```
fn =
\u1.\u2.\u3. match
    [ u2, u3 ]
    [ ( [NIL, ys], (A u1 ys) ),
      ( [CONS x xs, NIL], (B u1 x xs) ),
      ( [CONS x xs, CONS y ys], (C u1 x xs y ys) ) ]
    nil
```

### The Constructor Rule
Now, all cases for `u2` begins with a constructor. We can thus use the `case` function defined earlier to match the constructor, i.e.
```
fn =
\u1.\u2.\u3. case u2 of
    NIL => match
        [u3]
        [ ( [ys], (A u1 ys) ) ]
        nil
    CONS u4 u5 => match
        [u4, u5, u3]
        [ ( [x, xs, NIL], (B u1 x xs) ),
          ( [x, xs, (CONS y ys)], (C u1 x xs y ys) ) ]
        nil
```
Notice how `u4` and `u5` were introduced so we may perform a pattern match under the values of the constructor. We can then apply the variable rule again
to end with
```
fn =
\u1.\u2.\u3. case u2 of
    NIL => match
        []
        [ ( [], (A u1 u3) ) ]
        nil
    CONS u4 u5 => match
        [u3]
        [ ( [NIL], (B u1 u4 u5) ),
          ( [(CONS y ys)], (C u1 u4 u5 y ys) ) ]
        nil
```
To apply the constructor rule, we may reorder the list of patterns to group the same constructors together, as long as their relative order does not
change. This is legal because two patterns starting with different constructors can never match the same value, so only the relative order of same
constructor patterns matters.

A constructor with no patterns in the group gets the match default as its case arm: if all patterns match `CONS` and none match `NIL`, the `NIL` arm
is the default expression.

### Constant Patterns
Literal patterns (numbers, strings, `true`, `false`, `nil`) follow the same scheme as constructors: the book treats each literal as a constructor of
a type with infinitely many constructors, so a `case` over them becomes an equality test per literal present in the group, with every other value
going to the default. The `xor` example above matches `true`/`false` this way.

### The Empty Rule
The empty rule is the base case of the compilation: once every variable has been consumed, only expressions remain, and the match call
```
match []
      [ ([], E1),
        ...
        ([], EM) ]
      def
```
is equal to `E1 | ... | EM | def`. The `|` chain is what lets guards work: a guarded expression can still FAIL, falling through to the next
expression and finally to the default. When there is a single row whose expression cannot FAIL (no guard), this collapses to just `E1`, which
is the case in our example. As such, we may further optimize our function to
```
fn =
\u1.\u2.\u3. case u2 of
    NIL => A u1 u3
    CONS u4 u5 => match
        [u3]
        [ ( [NIL], (B u1 u4 u5) ),
          ( [(CONS y ys)], (C u1 u4 u5 y ys) ) ]
        nil
```
With this, we may finish the compilation by applying the variable, constructor and empty rule into the remaining match case to obtain the following
```
fn =
\u1.\u2.\u3. case u2 of
    NIL => A u1 u3
    CONS u4 u5 => case u3 of
        NIL => B u1 u4 u5
        CONS u6 u7 => C u1 u4 u5 u6 u7
```

We are still missing one rule, however.

### The Mixture Rule
Consider the following case:
```
fun fn(f, lst1, lst2)
| (f, [], ys) = A(f, ys)
| (f, xs, []) = B(f, xs)
| (f, [x, ..xs], [y, ..ys]) = C(f, x, xs, y, ys)
end
```
Converting it into a match expression yields
```
fn =
\u1.\u2.\u3. match
    [ u2, u3 ]
    [ ( [NIL, ys], (A u1 ys) ) ]
    ( match
        [u2, u3]
        [ ( [xs, NIL], (B u1 xs) ) ]
        ( match
            [ u2, u3 ]
            [ ( [CONS x xs, CONS y ys], (C u1 x xs y ys) ) ]
            nil ) )
```

The rule for conversion is: First, we try to group the constructors; once we hit a pattern starting with a variable, we finish this group
and group the variable cases; then, once we hit a constructor again, we group the constructors. We repeat this pattern until completion and then
try to compile the match expression.

## Record and Map Patterns
Suppose the following match expression
```elixir
match x
| {x: 5, y: a} = ...
| {y: 0, z: 0} = ...
| {z: 0} = ...
| {w: 1} = ...
end
```
We may transform it into the following matrix
```
  x  y  z  w
[ 5  a  _  _ ]
[ _  0  0  _ ]
[ _  _  0  _ ]
[ _  _  _  1 ]
```
Now, we match as if the expression was
```elixir
match (x, y, z, w)
| (5, a, _, _) = ...
| (_, 0, 0, _) = ...
| (_, _, 0, _) = ...
| (_, _, _, 1) = ...
end
```
Unlike a real tuple, though, the columns are fallible lookups fetched lazily: a cell only performs its lookup when tested,
and each cell means one of three things
- A literal cell (`5`, `0`, `1`): the key must be present and its value equal to the literal. A missing key FAILs the row,
  falling straight to the next alternative.
- A variable cell (`a`): the key must be present and its value is bound to the variable. It exists to be bound, so it is not
  a wildcard: a missing key FAILs the row.
- A `_` cell marks a key the pattern does not mention: it is discarded, never looked up, and matches records with or without
  that key.

Because a variable cell carries a presence test, the variable rule applies only to `_` columns; mentioned variables compile
to a lookup that binds on success and FAILs the row otherwise.

Map patterns follow the same schema.

## Column reordering
We may reorder the columns of a tuple pattern match without changing the final result. Reordering the columns may lead to a smaller decision tree. Let's take
the example
```
fun fn(f, lst1, lst2)
| (f, [], ys) = A(f, ys)
| (f, xs, []) = B(f, xs)
| (f, [x, ..xs], [y, ..ys]) = C(f, x, xs, y, ys)
end
```
and transform it into our lambda calculus representation, using the variable rule to optimize out the `f` pattern.
```clojure
fn =
\u1.\u2.\u3. match
    [ u2, u3 ]
    [ ( [NIL, ys], (A u1 ys) ) ]
    ( match
        [u2, u3]
        [ ( [xs, NIL], (B u1 xs) ) ]
        ( match
            [ u2, u3 ]
            [ ( [CONS x xs, CONS y ys], (C u1 x xs y ys) ) ]
            nil ) )
```
Notice how we must match on `[u2, u3]` twice. Now, let's allow us to reorder the columns following a simple heuristic.
First, we will score each column and evaluate the columns with the highest score first. The way we score a column is:
- Each non variable pattern gives a column 1 point;
- Any pattern below a variable pattern does not give any points.
Then, let's come back to our lambda calculus, right after optimizing out `f`:
```clojure
fn =
\u1.\u2.\u3. match
    [ u2, u3 ]
    [ ( [NIL, ys], (A u1 ys) ),
      ( [xs, NIL], (B u1 xs) ),
      ( [CONS x xs, CONS y ys], (C u1 x xs y ys) ) ]
    nil
```
Now, let's apply our heuristic and build the scoring matrix from the pattern matrix
```
[NIL,       ys       ]       [ 1, 0 ] # variable on ys, everything below it is 0
[xs,        NIL      ]  =>   [ 0, 0 ] # variable on xs, everything below it is 0
[CONS x xs, CONS y ys]       [ 0, 0 ]
```
This means that we do not reorder it now. We may compile this to
```clojure
fn =
\u1.\u2.\u3. match
    [ u2, u3 ]
    [ ( [NIL, ys], (A u1 ys) ) ]
    ( match
        [u2, u3]
        [ ( [xs, NIL], (B u1 xs) ),
          ( [CONS x xs, CONS y ys], (C u1 x xs y ys) ) ]
        nil )
```
Transforming it into a `case`
```clojure
fn =
\u1.\u2.\u3. case u2 of
    NIL => (A u1 u3)
    else =>
        ( match
            [u2, u3]
            [ ( [xs, NIL], (B u1 xs) ),
              ( [CONS x xs, CONS y ys], (C u1 x xs y ys) ) ]
            nil )
```
Notice how we're using a else as a form to match anything that's not `NIL`. Now, we still have a `match` over `u2` and `u3`. Let's make the scoring
matrix again
```
[xs,        NIL      ]   =   [ 0, 1 ] # xs is a variable pattern
[CONS x xs, CONS y ys]       [ 0, 1 ]
```
Now, the second column scores one point in each row, for a total of 2 points. This means that we may switch the columns `u2` and `u3`.

```clojure
fn =
\u1.\u2.\u3. case u2 of
    NIL => (A u1 u3)
    else =>
        ( match
            [u3, u2]
            [ ( [NIL, xs], (B u1 xs) ),
              ( [CONS y ys, CONS x xs], (C u1 x xs y ys) ) ]
            nil )
```
Converting it into a `case`
```clojure
fn =
\u1.\u2.\u3. case u2 of
    NIL => (A u1 u3)
    else =>
        case u3 of
            NIL => (B u1 u2)
            CONS u4 u5 =>
                ( match
                    [u2],
                    [ ( [CONS x xs], (C u1 x xs u4 u5) ) ]
                    nil )
```
Then, we convert the final `match`
```clojure
fn =
\u1.\u2.\u3. case u2 of
    NIL => (A u1 u3)
    else =>
        case u3 of
            NIL => (B u1 u2)
            CONS u4 u5 => case u2 of
                    CONS u6 u7 => (C u1 u6 u7 u4 u5)
                    else => nil
```
Notice how we are checking on `u2` twice and `u3` only once. Without any reordering we would check on `u3` twice as well.

A smart enough compiler may automatically detect that u2 can only be `CONS` and further compile it to
```clojure
fn =
\u1.\u2.\u3. case u2 of
    NIL => (A u1 u3)
    CONS u6 u7 =>
        case u3 of
            NIL => (B u1 u2)
            CONS u4 u5 => (C u1 u6 u7 u4 u5)
```

## Lambda Calculus to S Expressions

`Lóng` uses S Expressions to represent intermediate code. Luckily, converting lambda calculus to S Expressions is quite easy. The match function becomes:
```clojure
(match
    (list u1 ... un)
    (list
        (tuple
            (list p11 ... p1N) E1)
            ...
        (tuple
            (list pM1 ... pMN) EM))
    default)
```
`case` can be translated into a series of `if`
```clojure
(if
    (is-nil? x) expr1
    (if
        (is-cons? x) expr2))
```
or a specialized `cond`, to facilitate reading.
```clojure
(cond x
    (is-nil?) expr1
    (is-cons?) expr2
    default)
```
A trailing expression with no test is the `cond` default arm, the `else` of `case`: it evaluates when no test passes, and every
`cond` the match compiler generates carries one. Note that `cond`, the predicates and the `head`/`tail` accessors used below are
new internal forms this compilation introduces: the compiler does not know them yet and will need to emit them.

Let's convert the whole `fn` example from the column reordering section:
```
fun fn(f, lst1, lst2)
| (f, [], ys) = A(f, ys)
| (f, xs, []) = B(f, xs)
| (f, [x, ..xs], [y, ..ys]) = C(f, x, xs, y, ys)
end
```
After the variable rule removes `f`, the match becomes the s expression
```clojure
(match
    (list u2 u3)
    (list
        (tuple (list NIL ys) (A u1 ys))
        (tuple (list xs NIL) (B u1 xs))
        (tuple (list (CONS x xs) (CONS y ys)) (C u1 x xs y ys)))
    nil)
```
The mixture rule splits the rows at the variable pattern `xs`, chaining the groups through the default argument
```clojure
(match
    (list u2 u3)
    (list
        (tuple (list NIL ys) (A u1 ys)))
    (match
        (list u2 u3)
        (list
            (tuple (list xs NIL) (B u1 xs))
            (tuple (list (CONS x xs) (CONS y ys)) (C u1 x xs y ys)))
        nil))
```
The first group compiles through the constructor, variable and empty rules into a `cond` over `u2`, with the second group as
its default arm
```clojure
(cond u2
    (is-nil?) (A u1 u3)
    (match
        (list u2 u3)
        (list
            (tuple (list xs NIL) (B u1 xs))
            (tuple (list (CONS x xs) (CONS y ys)) (C u1 x xs y ys)))
        nil))
```
The scoring heuristic swaps the remaining columns so `u3` is tested first
```clojure
(cond u2
    (is-nil?) (A u1 u3)
    (match
        (list u3 u2)
        (list
            (tuple (list NIL xs) (B u1 xs))
            (tuple (list (CONS y ys) (CONS x xs)) (C u1 x xs y ys)))
        nil))
```
Applying the constructor rule to `u3` yields a `cond`. The `CONS` bindings become ordinary assignments over internal `head` and
`tail` accessors, so everything after the tests is plain `lóng` code
```clojure
(cond u2
    (is-nil?) (A u1 u3)
    (cond u3
        (is-nil?) (B u1 u2)
        (is-cons?) (do
            (= u4 (head u3))
            (= u5 (tail u3))
            (match
                (list u2)
                (list
                    (tuple (list (CONS x xs)) (C u1 x xs u4 u5)))
                nil))
        nil))
```
One last application of the constructor and empty rules on `u2` finishes the compilation
```clojure
(cond u2
    (is-nil?) (A u1 u3)
    (cond u3
        (is-nil?) (B u1 u2)
        (is-cons?) (do
            (= u4 (head u3))
            (= u5 (tail u3))
            (cond u2
                (is-cons?) (do
                    (= u6 (head u2))
                    (= u7 (tail u2))
                    (C u1 u6 u7 u4 u5))
                nil))
        nil))
```
Every test is an `is-nil?`/`is-cons?` check, every binding an assignment, and every FAIL a fall into a `cond` default: the
pattern match is gone. The bodies are ordinary `lóng` s expressions the compiler already handles; `cond`, the predicates and
`head`/`tail` are the new internal forms it must learn to emit.

## Sources:
[THE IMPLEMENTATION OF FUNCTIONAL PROGRAMMING LANGUAGES](https://www.microsoft.com/en-us/research/wp-content/uploads/1987/01/slpj-book-1987.pdf)
[Compiling Pattern Matching to good Decision Trees](https://www.cs.tufts.edu/~nr/cs257/archive/luc-maranget/jun08.pdf)
[Clojure's core.match](https://github.com/clojure/core.match/wiki/Overview)

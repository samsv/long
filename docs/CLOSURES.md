# Closures

Functions are defined as their own `vm`: a function is a `vm_t` with its `name` set. Argument names only exist at compile time.
Functions are not values and are not reference counted: compiled functions are plain `vm_t`s owned by the enclosing chunk's
`functions` array, referenced by index from the bytecode. A closure points at the function and owns the captured values, and the
closure is the reference counted object:

```c
typedef struct {
    vm_t* function;
    array_t(value_t) upvalues;
} closure_t;
```

where an `array_t` is
```c
#define array_t(type) type ## _array_t
#define create_array_t(type) \
typedef struct {      \
    type* values;     \
    size_t len;       \
    size_t capacity;  \
} array_t(type);
```

An `upvalue` is a value captured from outside of the function scope. The concept of `upvalue` is taken from the [lua language](https://www.lua.org/pil/27.3.3.html).
This means that closures may capture values from parent scopes during compilation. Suppose the function
```
fun f(x) =
    fun g(y) =
        x + y
    end

    g
end

g = f(5)
g(6) # 11
```

`f` must be compiled to something similar to
```
get_global 0 # adds x to the stack
load_closure 0 1 # closure of functions[0] with one upvalue
set_global 1 # set g as global
pop
get_global 1 # return g
```
Notice that the `g` stored in globals must not be the function itself, otherwise we would not be able to properly call `f` twice to
capture two different values of `x`. `load_closure` creates a fresh `closure_t` on every execution, pointing at the function in the
chunk's `functions` array. If `g` had no upvalues, then `load_closure 0 0` would create a closure with 0 upvalues.

A function's own name is registered in its globals table right after its arguments. When calling a closure, we create a new `vm_t`
for the call: the arguments are moved into its globals array, then a borrow of the callee itself is appended, filling the name's
slot. The vm reads upvalues directly from the closure's array, without copying or borrowing them, which is safe because the callee
borrow outlives the call. A reference to the function's own name compiles to a plain `get_global`, so recursion is an ordinary
`call`, and no self reference is ever stored in the closure itself.

The bare `function` pointer is safe because code is static: chunks form a tree rooted in the top level chunk, so a function lives
until program teardown, and `vm_t.deinit` releases the stack, locals and globals before the chunk, so every closure dies before any
code does. Only values are reference counted. If code ever becomes unloadable (a repl, hot reload), counts on code return.

There is, however also the issue of identifying that `x` must be captured. We could just identify that `x` does not exist on the current scope and try to get it
from the enclosing function. Another way to make dealing with the problem of closures easier is to explicitly declare which values are closed over, such as
`C++`'s lambdas. I.e
```
fun f(x) =
    fun g[x](y) =
        fun h[x,y]() =
            x + y
        end

        h
    end

    g
end
```
Now we know which values must be defined before the function starts the compilation process and abort earlier. This also makes it explicit which values we
are closing over. There is, however, one remaining issue with mutual recursive functions. Suppose the following
```
fun is_even[is_odd](x) =
    if x == 0 do true
    else is_odd(x - 1)
    end
end

fun is_odd[is_even](x) =
    if x == 0 do false
    else is_even(x - 1)
    end
end
```

This will fail to compile, as `is_odd` is not defined when `is_even` is compiled. As the functions are mutually recursive, nothing can be done with the current
approach. We may then define the following syntax for mutually recursive functions
```
fun
| is_even(x) =
    if x == 0 do true
    else is_odd(x - 1)
    end
| is_odd(x) =
    if x == 0 do false
    else is_even(x - 1)
    end
end
```
Now, we may automatically add `is_even` and `is_odd` to each others upvalues array. This, however, creates a cycle, meaning that neither, `is_even` nor `is_odd`
will ever be freed.

To fix it, we may treat `is_even` and `is_odd` as a group of functions. The group holds a non-owning view of the members, which are
plain `vm_t`s owned by the chunk like any other function, and owns the captured values. Only the group cell is reference counted,
shared by the sibling members of one execution:
```c
typedef struct {
    // non-owning view of the chunk's storage, in block order
    array_t(vm_t) members;
    // every member's captured upvalues, concatenated; each member's offset is
    // baked into its get_upvalue operands at compile time
    array_t(value_t) upvalues;
} closure_group_t;

typedef struct {
    rc_t(closure_group_t) group;
    size_t index;
} closure_member_t;
```

Calling a `closure_member_t` works like calling a closure: a new `vm_t`, arguments moved in, the callee borrow filling the member's
own name slot, and upvalues read directly from the group's array. `get_upvalue k` pushes a borrow of the group's `upvalues[k]`,
with `k` baked in at compile time. `get_member j` rebuilds the sibling `closure_member_t` from the group plus the index `j`, and
calling it is a regular `call`. As such, no cycle can form: the group holds only functions and captured values, which reference
nothing back, and rebuilt members are released when the call returns.

A flat function
```
fun f(x) =
    1
end
```
Would compile to a plain `closure_t` via `load_closure`; groups only exist for `fun | ... end` blocks.

So, when compiling the function group for `is_even`, `is_odd`, we'd get the following bytecode:

```
# push captured values here
create_group 0 2 0 # group over functions[0..2) with 0 upvalues; push one member per function, member 0 on top
set_global 0 # set is_even member as global
pop
set_global 1 # set is_odd member as global
pop
```

A block's members are compiled into consecutive slots of the chunk's `functions` array, so `create_group` takes the index of the
first member, the member count and the upvalue count; it pops only the captured values and the group's members view is a slice of
the chunk's array.

`is_even` body would compile as
```
get_global 0 # load x
load_constant 0 # load 0
equals # compare both
jump_if_false 0x05 # offset to jump
load_constant 1 # load true
jump 0x08 # finish
get_global 0
load_constant 2 # load 1
sub
get_member 1 # load is_odd. x is at slot 0, is_even at slot 1
call 1 # call is_odd
```

## Intermediary Representation
The language intermediary representation is a dialect of lisp. A function is defined as
```cl
(fn fn_name
    (arg1, arg2, arg3) ; args
    body)
```
for closures, we can expand it to
```cl
(fn fn_name
    (val_1, val_2) ; upvalues
    (arg1, arg2, arg3) ; args
    body)
```
And for a function group
```cl
(fn
    (fn_name_1
        (val_1, val_2) ; upvalues 1
        (arg1, arg2, arg3) ; args 1
        body_1)
    (fn_name_2
        (val_1, val_3) ; upvalues 2
        (arg1, arg2, arg3) ; args 2
        body_2)
)
```

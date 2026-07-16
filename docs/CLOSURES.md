# Closures

Functions are defined as their own `vm`. A function struct is
```c
typedef struct {
    char* name;
    vm_t vm;
    array_t(char*) args;
} function_t;
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
load_constant 0 # load g
create_closure 1 # create closure with one upvalue
set_global 1 # set g as global
pop
get_global 1 # return g
```
Notice that the `g` from globals must not be the same `g` from `f`'s constants, otherwise we would not be able to properly call `f` twice to capture
two different values of `x`. If `g` had no upvalues, then a closure with 0 upvalues would be created.

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

To fix it, we may treat `is_even` and `is_odd` as a group of functions, with the following structure
```c
typedef struct {
    // upvalues[0 .. n_group_members-1]: rc_t(function_t), in block order
    // upvalues[n_group_members ..]:     every member's captured upvalues, concatenated;
    //                                   each member's offset is baked into its
    //                                   get_upvalue operands at compile time
    rc_t(array_t(value_t)) upvalues;
    size_t index;
} closure_member_t;
```

Now, when calling a `closure_member_t`, we create a new `vm_t` for the call. The arguments are moved into its globals array and the handle is kept alive
until the call returns. The vm reads upvalues directly from the shared array, without copying or borrowing them. This is safe because the handle outlives the
call and a group is never modified after `create_group`.

`get_upvalue k` pushes a borrow of `upvalues[k]`, with `k` baked in at compile time. `get_member j` rebuilds the sibling `closure_member_t` from the shared
array plus the index `j`, and calling it is a regular `call`. A member may rebuild itself, which enables recursion. As such, no cycle can form: the array
holds only functions and captured values, which reference nothing back, and rebuilt members are released when the call returns.

A flat function
```
fun f(x) =
    1
end
```
Would compile to a `closure_member_t` with only itself at the group and no upvalues.

So, when compiling the function group for `is_even`, `is_odd`, we'd get the following bytecode:

```
# load upvalues here
load_constant 1 # load is_odd
load_constant 0 # load is_even
# now is_even, is_odd and upvalues are on the stack
create_group 2 0 # create a function group of size 2, with 0 upvalues
set_global 0 # set is_even member as global
pop
set_global 1 # set is_odd member as global
pop
```

`is_even` body would compile as
```
get_global 0 # load x
load_constant 0 # load 0
eq # compare both
jump_if_false 0x05 # offset to jump
load_constant 1 # load true
jump 0x08 # finish
get_global 0
load_constant 2 # load 1
sub
get_member 1 # load is_odd. is_even is at slot 0
call 1 # call is_odd
```

With that, our old function `f`
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

will be compiled to something similar to
```
get_global 0 # adds x to the stack
load_constant 0 # load g
create_group 1 1 # create closure with one upvalue and self
set_global 1 # set g as global
pop
get_global 1 # return g
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

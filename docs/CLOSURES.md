# Closures

Functions are defined as their own `vm`. A function struct is
```c
typedef struct {
    char* name;
    vm_t vm;
    array_t(value_t) upvalues;
    array_t(char*) args;
} fuction_t;
```

where an `array_t` is
```c
#define array_t(type) \
typedef struct {      \
    type* values;     \
    size_t len;       \
    size_t capacity;  \
} type ## _array_t;
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
load_constant 0 # load g
set_global 0 # set it as a global
pop # pop from stack
get_global 1 # adds x to the stack
get_global 0 # adds g to the stack
add_upvalue 1 # pops g and x from the stack, leaves stack empty
get_global 0 # return g
```
Notice that the `g` from globals must not be the same `g` from `f`'s constants, otherwise we would not be able to properly call `f` twice to capture
two different values of `x`.

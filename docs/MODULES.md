# Modules

Each `lóng` file is a module which can be imported by another `lóng` file. The compiler takes care of name mangling
such that no two values from different modules colide. Imports can not be circular and they must be at the top level
of each file.

## Syntax
Given an example file
```elixir
# m1.long
x = 1
y = 2

fun add_x[x](y) # module closure
    x + y
```
and the main script
```elixir
# main.long
import m1("m1.lox")

x = 5

m1::add_x(5)
|> println() # 6

println(m1::x == x) #false
y = m1::y # y = 2
```

A file may not access other file imports. Consider
```elixir
# m2.long
y = 2
```
```elixir
# m1.long
import m2("m2.long")

fun add_y(x) # module closure
    x + m2::y
```
```elixir
# main.long
import m1("m1.lox")

m1::add_y(5)
|> println() # 7

y = m1::y # error: y is not defined
y = m1::m2::y # error: y is not defined
```

To access the values from `m2`, main would need to import it first.

## Compiler Name Resolution
The compiler performs name mangling when a file is imported, such that all import values becomes global variables
in the main file. Consider the 3 scripts
```elixir
# m2.long
y = 2
```
```elixir
# m1.long
import m2("m2.long")

fun add_y(x) # module closure
    x + m2::y
```
```elixir
# main.long
import m1("m1.lox")
import m2("m2.lox")

y = m2::y

m1::add_y(y)
|> println() # 4
```
The compiler will start compiling `main.long`. It will then see the import keyword and start the import process for `m1`,
marking the compilation of `m1` as in progress. It then sees the `m2.long` import and marks the import as in progress.
Inside `m2`, it sees `y = 2`. The variable name `y` will be mangled in the compiler to `$FULL_PATH/m2.long$y`. It then finishes
`m2` and marks it as completed and it's variable names are cached. Compilation comes back to `m1`, which compiles `add_y` as
`$FULL_PATH/m1.long$add_y`. Compilation comes back to main.

The compiler sees the `m2` import and uses its cached value to lookup its variable names. Then, `y = m2::y` will be translated by
the compiler as `y = $FULL_PATH/m2.long$y` and `m1::add_y(y)` to `$FULL_PATH/m1.long$add_y(y)`.

`$FULL_PATH` is full path of the folder containing the file. E.g. `home/user/folder/m1.long`.

## Implementation
The compiler will need a few new structure
```c
typedef hashmap_t module_variables_t; // maps the mangled name to the compilers global array

typedef struct compile_queue_t {
    const char* module_name;
    struct compile_queue_t* next;
} compile_queue_t;

typedef struct {
    transient_hashmap_t compiled_modules; // holds as keys the full path name of the module to the compiled module_t
    compile_queue_t* compile_queue; // the modules queue to compile
} module_map_t;
```
Then, inside the `compiler_t` struct
```c
typedef struct {
    module_map_t modules;
    // maps module names from `import name("module.long")` to its compiled_modules key. e.g. name -> $FULL_PATH/module.long
    // whenever a new module enters the compile queue a new transient hashmap must be created and replace the old one
    // in the compiler. After compilation is completed, the old var_to_modules map may be restored.
    transient_hash_map_t var_to_modules;
    // rest of the struct
    // ...
} compiler_t;
```

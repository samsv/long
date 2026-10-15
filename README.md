# Lóng 龙 Lang

Lóng 龙 is an embeddable, scripting, functional programming language written in C.

### Key Features
- Small language (release static library is just 428K)
- Embeddable in any C program
- Functional with persistent data structures
- Pattern Matching

## The Language
Long's syntax is Elixir inspired. It features pattern matching, functions as first order citizens, and everything is an expression.

```
import my_file("my_file.long") # you can import other files from long

fun one() 1 # functions are just and expression

fun two() do # use a do block if you wish to write multi line functions
    t = 2
    t # the last expression is returned
end

fun fib(n) # you can perform pattern matching on function arguments
| (0) do 0
| (1) do 1 # all patterns need the `do` keyword afterwards
| (n) do fib(n - 1) + fib(n - 2)
end

fun fib2(n)
| (n) when n < 2 do n # you can also use guards
| (n) do fib(n - 1) + fib(n - 2)
end

10
|> fib() # pipe operator
|> println()

xs = [1, 2, 3, 4, 5] # lists are linked lists backed by a dense array, similar to an unrolled linked list

ret = for x in xs do # for returns the result of the last iteration
    2 * x
end

doubled_xs = map x in xs do # map is a builtin like a loop
    2 * x
end

mapf x in xs do # we also have a mapf, short for map and filter
    if (tripled = 3 * x) < ret do # the `=` binds the value of 3 * x to tripled and returns the value
        nil # ignore
    else
        tripled # return tripled
    end
end

fun fizz_buzz(n) do
    ret = match n
    | n when n % 3 == 0 and n % 5 == 0 do "fizzbuzz"
    | n when n % 3 == 0 do "fizz"
    | n when n % 5 == 0 do "buzz"
    | n do n
    end

    println(ret)

    if n > 1 do
        fizz_buzz(n - 1) #tail recursion
    else
        nil
    end
end

fizz_buzz(100)

match my_file::do_stuff()
| (:ok, res) do println("Ok") # we also have atoms and tuple
| (:error, reason) do print_vals(["Not ok: ", reason])
end

d = %{ "hello": "world" } # we also have dictionaries
person = { name: "Dragon", age: 4000 } # and records

print_vals([persor.name, " ", person.age])
```

## Building
### Binary
Run `make` to create a binary in `build/long`. Once built, you can pass a file as an argument to the compiled binary. Example
```
build/long my_file.long
```

### Static Library
To create a static library run `make static`, then clone this repository as an dependency for your project. You can include the `compiler.h`
and `vm.h` headers in your application to start running lóng from your application.

## Embedding
Take the sample code on how embedding is done
```c
#define SV_IMPLEMENTATION
#include "long/src/compiler.h"
#include "long/src/value.h"
#include "long/src/std/allocator_std.h"

int main(void)
{
    // the allocator we will be using
    const sv_allocator_t* a = &sv_gpa;

    // path of the scripts to compile
    const char** scripts = { "my_script_1.long", "my_script_2.long" };

    // native functions we wish to insert in the interpreter
    #define NATIVES_COUNT 1
    native_fn_t natives[NATIVES_COUNT] = {
        { .arity = 1, .name = "long_fn_name", .fn = my_func },
    };

    // compiler options
    compile_opts_t compiler_opts = COMPILER_DEFAULT_OPTS;
    compiler_opts.native_funs = natives;
    compiler_opts.native_count = NATIVES_COUNT;

    ctx_t ctx = {
        .alloc = *a,
        .logger = sv_std_logger,
    };

    // compile files and run
    vm_t vm = compile_files(scripts, size, compiler_opts, &ctx);
    sv_opt_t(error_t) err = vm_run(&vm);

    // got an error
    if (err.is_some) {
        printf("Error: msg %.*s", (int)err.value.msg.size, err.value.msg.chars);
        exit(1);
    }

    // call a function from the script and get its result
    // we first define the args
    value_t args[2] = {
        { .kind = VALUE_NUMBER, .number = 1 },
        { .kind = VALUE_NUMBER, .number = 2 },
    };

    value_t ret;
    const char* script_path = "my_script_1.long";

    // then we have two options: the first is to call the function by name
    {
        ret = vm_call_name(&vm, args, 2, sv_str_init("my_fun"), script_path);
    }

    // option 2 is to get the function index and then calling it. This is faster if you want to call the same function many times
    {
        // get the function index in the vm globals table
        sv_opt_t(uint32_t) fn_idx = globals_get(vm.globals_names_to_index, sv_str_init("my_fun"), script_path, a);
        // and we then call it
        ret = vm_call(&vm, fn_idx.value, args, 2);
    }

    if (IS_ERR(ret)) {
        error_t merr = AS_ERR(ret);
        printf("Error: msg %.*s", (int)merr.msg.size, merr.msg.chars);
        exit(1);
    }

    return 0;
}
```

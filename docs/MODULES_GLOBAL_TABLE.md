# Modules — Global Table

One file is one module, and one module is one namespace. This document describes a design where
the namespace exists only in the compiler: at runtime there are no modules, only the flat global
table `lóng` already has.

## Declaring and Using a Module

A module is a file. It declares nothing — every top level binding it makes is part of its public
surface, and the file's name is the module's name.

```elixir
# vec.long
fun add(a, b)
    {x: a.x + b.x, y: a.y + b.y}
end

zero = {x: 0, y: 0}
```

Another file uses it by importing the path and reaching through the module name.

```elixir
# main.long
import "vec"

v = vec.add({x: 1, y: 2}, vec.zero)
```

`import` is already a reserved special function in the scanner and parser, so the syntax slot
exists; only the resolution behind it is new.

## Names Are Mangled, Not Scoped

The compiler keeps one global table for the whole program, exactly as it does today. A module's
top level binding does not get a scope of its own — it gets a decorated name.

```elixir
# vec.long declares
add   ->  vec$add
zero  ->  vec$zero
```

`$` is the separator because the scanner rejects it in identifiers, the same reason the pattern
match lowering uses it for its temporaries. A user cannot write `vec$add`, so a mangled name can
never collide with a source name, and two modules may both define `add` without either shadowing
the other.

Resolution then falls out of the lookup chain the compiler already walks — locals, upvalues,
globals, members — with two rules layered on top:

- Inside module `vec`, an unqualified `add` is tried as `vec$add` before anything else, so a
  module sees its own names without qualification.
- A qualified `vec.add` resolves to `vec$add` when `vec` names an imported module. Because the
  set of imported modules is known once resolution finishes, the compiler can decide this before
  it ever reaches the record field path, and `vec.add` and `point.x` never become ambiguous.

A local binding named `vec` wins over the module. Shadowing a module is allowed; capturing one
silently is not.

## Compilation Order

The compiler keeps a registry keyed by resolved path, with each entry in one of three states:
unvisited, in progress, or done. Compiling an import walks the registry depth first and compiles
the dependency to completion before returning to the importer, so every name a module refers to
already has a global slot by the time that module's own body is compiled.

An import that reaches a module already marked *in progress* is a cycle, and is rejected. Two
modules that need each other are one module that has not been split yet, and the error says so
rather than attempting a fixpoint.

Because every module ends up in one compilation unit, module bodies are concatenated into the
top level chunk in dependency order. There is no per module initialisation step at runtime —
`vec`'s top level statements are simply the statements that run before `main`'s.

## What the Runtime Sees

Nothing. `OP_GET_GLOBAL` reads an index into one `value_arr`, and `vec.add` compiles to the same
instruction as any other global read:

```
main.long                bytecode
v = vec.add(...)   ->    get_global 7      # 7 is vec$add
                         ...
                         call 2
```

There is no module object, no lookup, no indirection. A cross module call costs exactly what a
same module call costs.

The same is true of records. Field names are interned once for the whole program, so a record
built in `vec` and read in `main` agrees on field ids by construction, and `record_key_names`
covers every module's names in one table.

## Limits

Both index spaces this design leans on are a single byte.

`OP_GET_GLOBAL` and `OP_SET_GLOBAL` take a one byte index, so the program may declare 256 globals
in total — every module's top level bindings share that budget, whether or not the importer uses
them. Unlike the field id table, which reports an error past 255, the global table currently
truncates the index silently. Before this design ships, `globals_add` needs the bounds check that
`record_field_id` already has, and the indices themselves want an `EXTENDED_ARG` prefix so the
budget can grow past a byte.

Record field ids are also capped at 256 for the whole program, and a module system pushes more
distinct field names into that one table than a single file ever would.

## Errors

| condition | error |
|---|---|
| imported path does not resolve to a readable file | compile error naming the path and the importing line |
| import cycle | compile error naming both modules |
| two top level bindings with the same name in one module | `C_ERR_REDEFINED`, as today, now scoped to the module |
| qualified name whose module was never imported | `C_ERR_UNDEFINED_VARIABLE` naming the module |
| global budget exhausted | compile error, once the bounds check exists |

## Worked Example

```elixir
# vec.long
zero = {x: 0, y: 0}
fun add(a, b) {x: a.x + b.x, y: a.y + b.y} end

# main.long
import "vec"
p = vec.add(vec.zero, {x: 3, y: 4})
p.x
```

After resolution the compiler holds one global table:

```
0  print          # natives
1  println
2  print_vals
3  vec$zero
4  vec$add
5  main$p
```

and one field table shared by both files:

```
0  x
1  y
```

The emitted program is the concatenation of `vec`'s top level and `main`'s, in that order. No
instruction in it mentions a module.

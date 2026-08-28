# Modules — Modules as Values

One file is one module, and one module is one namespace. This document describes a design where a
module is an ordinary runtime value: the file's body is a function, its top level bindings are
that function's locals, and its namespace is the record the function returns.

## Declaring and Using a Module

A module is a file. Every top level binding it makes is part of the record it produces, and the
file's name is the name that record is bound to.

```elixir
# vec.long
fun add(a, b)
    {x: a.x + b.x, y: a.y + b.y}
end

zero = {x: 0, y: 0}
```

Another file imports the path and reaches into the record.

```elixir
# main.long
import "vec"

v = vec.add({x: 1, y: 2}, vec.zero)
```

`import` is already a reserved special function in the scanner and parser, so the syntax slot
exists; only the behaviour behind it is new.

## A Module Is a Function

The compiler wraps each file's body in a function of no arguments, and appends a record of its
top level names as the return value. The file above compiles as though it had been written:

```elixir
vec = (fun ()
    fun add(a, b) {x: a.x + b.x, y: a.y + b.y} end
    zero = {x: 0, y: 0}
    {add: add, zero: zero}
end)()
```

Everything follows from that. `add` and `zero` are locals of the module function, reached by
`OP_GET_LOCAL` within the module and never entered into the global table. `vec` is one global
holding a record. `vec.add` is a record field read — the same `OP_RECORD_GET` a user writes when
reaching into any other record.

Because the body is a real function, closures over module level bindings work without a special
case: `add` capturing `zero` is an ordinary upvalue capture, and closure creation copies the
captured values into the closure, so the module function's frame may go away afterwards.

## Resolution

Inside a module, names resolve through the chain the compiler already walks — locals, upvalues,
globals, members. A module's own top level names are locals of its body function, so they are
found at the first rung with no new rule.

A qualified `vec.add` needs no new rule either. `vec` is a global holding a record; `.add` is
field access. The compiler does not have to decide whether a dotted name is a module reference or
a record read, because there is no difference between them.

That equivalence is the design's centre of gravity, and it has consequences worth being explicit
about:

- A module may be passed to a function, stored in a list, or returned. `fun use(m) m.add(...) end`
  accepts any record with an `add` field, module or not.
- A module may be rebound. `vec = something_else` is legal and does what it says.
- There is no compile time guarantee that `vec.add` exists. A misspelled export is a runtime
  `VM_ERR_FIELD_NOT_FOUND`, not a compile error, exactly as for any other record.

## Compilation Order

The compiler keeps a registry keyed by resolved path, with each entry unvisited, in progress, or
done. Compiling an import walks depth first and compiles the dependency to completion first, so
the record a module returns exists before any importer reads a field from it.

An import that reaches a module already *in progress* is a cycle, and is rejected. Two modules
that need each other are one module that has not been split yet.

Each module's body function is invoked once, at program start, in dependency order, and the
resulting record is stored in the module's global. With a call frame stack this is a frame push
per module rather than anything special.

## What the Runtime Sees

A record per module, and one global per module holding it.

```
main.long                bytecode
v = vec.add(...)   ->    get_global 3      # vec, the module record
                         record_get 0      # add
                         ...
                         call 2
```

A cross module call costs one extra field read compared with a same module call — a binary search
over a small sorted array, which is what record access costs anywhere else in the language.

Records built in one module and read in another agree on field ids because field names are
interned once for the whole program, and `record_key_names` covers every module's names.

## Limits

The budgets this design leans on are the ones records and locals already use.

Each module's top level is one function's local frame, so a module may declare 255 top level
bindings — `OP_GET_LOCAL` takes a one byte index. This is a per module budget rather than a
program wide one, so adding modules does not consume it.

Export names live in the program wide field id table, which is capped at 256 and reports an error
past 255. A module system pushes more distinct field names into that table than a single file
would, and an `EXTENDED_ARG` prefix on the field index is the way to grow it.

The global table holds one entry per module plus the natives, so it stays small no matter how many
modules a program has.

## Errors

| condition | error |
|---|---|
| imported path does not resolve to a readable file | compile error naming the path and the importing line |
| import cycle | compile error naming both modules |
| two top level bindings with the same name in one module | `C_ERR_REDEFINED`, scoped to the module function |
| reading an export that does not exist | `VM_ERR_FIELD_NOT_FOUND` at runtime |
| module name used before its import | `C_ERR_UNDEFINED_VARIABLE` |

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

The global table holds the natives, one entry for `vec`, and one for `main`'s own top level:

```
0  print          # natives
1  println
2  print_vals
3  vec            # the record returned by vec's body
4  p
```

The field table is shared:

```
0  x
1  y
2  add
3  zero
```

At run time `vec` holds

```elixir
[(2, <fun add>), (3, [(0, 0), (1, 0)])]   # sorted by field id, as records always are
```

and `vec.add` is the binary search that any field access performs.

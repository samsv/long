# Named Tuples

`Lóng` uses named tuples to express custom types. For example, a `user` type could be declared as the tuple `{id: userid, name: username}`.

## Field Access
Tuple's fields may be accessed using the dot syntax. For a `user` tuple, one could access the id using `user.id`. A naive tuple implementation
would store it as an immutable dictionary, with the field names as string keys. That would mean, however, a comparison byte by byte of a field
name for each access, as well as a hash function computation + table lookup.

Considering that tuples are generally small, we may use a simple array with linear scan instead of a hashmap. We can also avoid string comparison
by interning all tuple field names in the compiler and assigning an ID to each. As such, consider
```elixir
vec2 = {x: 10, y: 20}
vec3 = {x: 10, z: 30, y: 20}

user1 = {id: 1, name: "john"}
user2 = {id: 2, name: "maria"}
```

In memory, it would look like this
```elixir
vec2 = [(0, 10), (1, 20)] # x is associated with 0 and y with 1 in the compiler
vec3 = [(0, 10), (1, 20), (2, 30)] # z gets assigned with id 2. x and y use the old id. The compiler reorders the fields in memory.

user1 = [(3, 1), (4, "john")] # id and name gets the constant 3 and 4, respectively.
user2 = [(3, 2), (4, "maria")]
```
Notice that the compiler reorders fields to make it sorted by each field id. To make large tuples also efficient (more than 10 elements),
a binary search may be used and elements may be capped at 255 elements.

When accessing a field the compiler must emit the `GET_FIELD` operation with the next byte in the bytecode being the index. An
`EXTENDED_ARG` operation may be implemented to allow users to define more than 255 unique tuple fields.

Field ids must also be defined in the compiler when finding a new field access. Consider the following
```elixir
fun add(v1, v2) =
    x = v1.x + v2.x # field x must be created here
    y = v1.y + v2.y # field y must be created here
    {x: x, y: y}
end

add({x: 1, y: 2}, {x: 3, y: 4})
```
The field ids must be defined before any tuple is created. It also means that, when compiling new code, special metadata would be needed to
interact with code compiled previously (e.g. from incremental compilation or bytecode generated from libraries as a cache to not recompile
everything again). At the moment, however, this would not be a problem.

## Printing
A tuple must be printed as it's declared. i.e.
```elixir
x = {x: 10, y: 20}
print(x) # prints {x: 10, y: 20}
```
although field reordering may take effect
```elixir
x = {x: 10, y: 20}
y = {y: 200, x: 100}
print(x) # prints {x: 10, y: 20}
print(y) # prints {x: 100, y: 200}
```
this means that a mapping between field ids and its names must be kept in the VM. This is trivially achievable by storing it in an array where
each index corresponds to its respective id. Because the printing native functions will need access to this mapping, a special VM context parameter
may be defined to pass special information to the functions, such as the table, allocator and a logger.


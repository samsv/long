# Linked Dictionary

An immutable dictionary backed by two linked lists, acting as a sparse set. The sparse set acts as the hash table array. All insert, update and delete operations create a new sparse set, unless specified. The dictionary also has a `child`, that is, another hash map which may contain other key value pairs different from the original.

### Sparse Sets
A sparse set consists of a dense linked list and a sparse standard array. Suppose that we have a set with 5 items. The memory layout would be
```
Dense
-------------------------
| 5 || 1 || 4 || 8 || 9 |
-------------------------

Sparse
-----------------------------------------------------------------------
| na || 1  || na || na ||  2 ||  0 || na || na ||  3 || 4 || na || na |
-----------------------------------------------------------------------
```

An item is in the set iff the two arrays point at each other at the given example. That is:
```c
sparse[i] < count && dense[sparse[i]] == i;
```
For example, to check if the element at index 8 is set (skipping the length check), then
```c
dense[sparse[8]] == 8
// get sparse[8]
dense[3] == 8
// get dense[3]
8 == 8
true
```

Now, suppose we want to find if an element is at index 6:
```c
dense[sparse[6]] == 6
// get sparse[6], suppose the value is 3
dense[3] == 6
// get dense[3]
8 == 6
false
```

## Hash Map Get
A get function takes a key and returns the element from the hashmap. To perform the `get` operation, we must first hash the key and check where on the sparse array it's located. If it is, then we must check the key values. If they are the same, we found our match. Otherwise, we check the next index, until we hit an index which is not on the map. In a C like psedocode:
```c
uint64_t i = hash(key) % set.len;
uint64_t start = i;
// check if element exists at index
kv_t* kv = NULL;
while (set.get(&kv, i)) {
    // check if the key is the one we are looking for
    if (kv.key == key) return {.kv = kv, .index = i};
    // if not, check the next element
    i = (i + 1) % set.len;
    if (start == i) break;
}
return {.kv = kv, .index = i};
```

## Hash Map Insert/Update
The insert and update operations in a hash map are generally two different behaviors of the same function. If a value is already in the map, it is updated, if not, it's inserted. Before we define which operation to perform, we must determine if the key is in the map. If it is, we must update it; if it's not, must add it. In a C like pseudocode
```c
kv_t* {kv, index} = map.get(key);
if (kv.is_empty) map.insert(key, value, index);
else map.update(key, value, index);
```

### Insert
Let's implement the insert operation first. Let's consider first that there is space in the dense arrays to append data. Inserting would mean setting the value in the sparse set and appending the corresponding data to it.
```c
set.add(key, value, index);
```
That would create a new dense and sparse vectors. For the dense vector, the prepend operation is performed. For the sparse vector, we mutate it in place to point to the newly added element in the dense vector. This is safely done because when an old map which shares the structure with the new one tries reading the item from the sparse vector, it will point to an element outside of its dense vector.

Now, consider the case where we can't just prepend the element to the dense list. This means that another hashmap, with the same predecessor, has already been created. We then may follow two paths:
- If the length of the hashmap is less than a constant `N`, we may copy the shared part of the dense array and the sparse array and prepend our data;
- If the length of the hashmap is bigger than `N`, then we may create a new hashmap with the updated value, and adding the original hashmap as its child. If a value is not on the parent, then it's searched in the child;

There is also another case, which is the case where the dictionary load factor is above a given load factor threshold. In this case, we may create a new hashmap with a bigger sparse array and a dense array with the updated indexes, in a process similar to a standard hashmap resize.

### Update
The update operation may follow two paths, analogous to the insert operation:
- If the length of the hashmap is less than a constant `N`, we may copy the dense array with the updated the value.
- If the length of the hashmap is bigger than `N`, then we may create a new hashmap with the updated value, and adding the original hashmap as its child. If a value is not on the parent, then it's searched in the child.

## Delete
To delete a value, we perform the update operation but for a special null value (e.g. -1, `NULL`, etc). This is analogous to the tombstone approach.

## Lookup Cost and Compaction
Within a single map the dense and sparse arrays are kept flat, so reading `dense[sparse[i]]` is `O(1)`. Divergence between versions is absorbed by the `child` chain rather than by fragmenting the arrays: a `get` first probes the current map and only falls through to the `child` (and its `child`, and so on) when the key is absent. A lookup is therefore `O(1)` for keys held by the most recent map, and `O(d)` in the worst case, where `d` is the number of children in the chain.

To keep `d` bounded we introduce a `MAX_CHILDREN` parameter, analogous to the load factor. Each map carries a cached `depth` (`parent.depth = child.depth + 1`; a freshly copied flat map has `depth = 0`), so the check is `O(1)`. Only the large-map child path increments `depth`; the small-map copy path produces a flat map and resets it.

When either threshold is crossed (the load factor exceeds its limit, or `depth` exceeds `MAX_CHILDREN`) we rebuild the map in a single `flatten` pass. `flatten` walks the chain from the most recent map down to the oldest, inserting each key into a fresh dense and sparse pair sized for the live element count. Keys already seen are skipped, so the newest value wins, and a newer tombstone suppresses an older value and is then dropped. This reclaims tombstones and collapses the child chain in the same pass, restoring `O(1)` lookups.

`MAX_CHILDREN` should be a small constant (in the same spirit as a `0.75` load factor). There is an inherent tension: a `flatten` costs `O(n)`, so a small `MAX_CHILDREN` keeps lookups near `O(1)` but makes update-heavy workloads rebuild often (`O(n / MAX_CHILDREN)` amortized per write), while a large one amortizes rebuilds away at the cost of `O(n)` lookups. A workload that constantly hits the cap is better served by a different backing (e.g. a HAMT, with uniform `O(log n)` reads and writes).


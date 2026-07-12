# Array Linked List

A data structure where linked list nodes are stored in linked continuous arrays buckets. Each bucket would have the following structure:

```c
typedef struct {
    elem_t* elems;
    size_t len;
    size_t capacity;
} bucket_t;
```

A linked list head has the following structure:

```c
typedef struct {
    bucket_t* bucket;
    size_t start_index;
    size_t len;
    bucket_linked_list_t* tail;
} bucket_linked_list_t;
```

## Appends

Suppose a linked list, `l1`, with the element `e1`. The memory layout of the list data would be:

```
------
| e1 |
------
```

The head of `l1` would point to `e1`

Then, if a new value is added, a new linked list, `l2`, would be created, with memory layout

```
------------
| e1 || e2 |
------------
```

After `n` inserts:
```
------------------------------------
| e1 || e2 || e3 || e4 || .. || en |
------------------------------------
```

Now, consider the linked list `l4`, which points to `e4`. If we prepend a new element to it, `ej1`, then we'd have the following layout:
```
bucket_1
-------
| ej1 |
-------

bucket_0
------------------------------------
| e1 || e2 || e3 || e4 || .. || en |
------------------------------------
```

The linked list structure would be
```c
bucket_t bucket_0 = {
    .elems = [e1, e2, e3, e4, ..., en],
    .len = n,
};

bucket_linked_list_t l4 = {
    .bucket = bucket_0,
    .start_index = 3,
    .len = 4,
    .tail = NULL;
};

bucket_t bucket_1 = {
    .elems = [ej1],
    .len = 1,
};

bucket_linked_list_t l_new = {
    .bucket = bucket_1,
    .start_index = 0,
    .len = 1,
    .tail = &l4;
};
```

## Insert and update
Inserts and updates can be done two fold.

### Case 1
Updating a value in the linked list needs to clone the whole list until the element being updated and inserting and then pointing the tail to the next element of the list. Inserting a value is analogous, but with a new node inserted. Suppose we want to update the element `e3` to `e3_new`. Our memory layout would be

```
bucket_1
----------------------------
| e3_new || e4 || .. || en |
----------------------------

bucket_0
------------------------------------
| e1 || e2 || e3 || e4 || .. || en |
------------------------------------
```

With the tail pointing to `e2`.

If a new element `e2.5` is inserted into the list
```
bucket_2
--------------------------------
| e2.5 || e3 || e4 || .. || en |
--------------------------------

bucket_1
----------------------------
| e3_new || e4 || .. || en |
----------------------------

bucket_0
------------------------------------
| e1 || e2 || e3 || e4 || .. || en |
------------------------------------
```

With the tail also pointing to `e2`.

### Case 3
To minimize the amount of cloning, one can implement insertion by splitting the list into 3. Suppose we want to update the element `e3` to `e3_new`. Our memory layout would be

```
bucket_1
----------
| e3_new |
----------

bucket_0
------------------------------------
| e1 || e2 || e3 || e4 || .. || en |
------------------------------------
```

With the following list nodes
```c
// og bucket.
bucket_t bucket_0 = {
    .elems = [e1, e2, e3, e4, ..., en],
    .len = n,
};

bucket_linked_list_t og_list = {
    .bucket = bucket_0,
    .start_index = n - 1,
    .len = n,
    .tail = NULL;
};

// after inserting, we create the following structures.
bucket_t bucket_new = {
    .elems = [e3_new],
    .len = 1,
};

// a view to the slice [e1, e2].
bucket_linked_list_t l_tail = {
    .bucket = bucket_0,
    .start_index = 1,
    .len = 2,
    .tail = NULL;
};

// the updated element.
bucket_linked_list_t l_update = {
    .bucket = bucket_new,
    .start_index = 0,
    .len = 1,
    .tail = &l_tail;
};

// the new list returned. A view from [e4, .., en] and tail pointing to the updated element.
bucket_linked_list_t l_new = {
    .bucket = bucket_0,
    .start_index = n - 1,
    .len = n - 3,
    .tail = &l_update;
}
```

Inserting works in a similar manner, `l_tail` would be
```c
// a view to the slice [e1, e2, e3].
bucket_linked_list_t l_tail = {
    .bucket = bucket_0,
    .start_index = 2,
    .len = 3,
    .tail = NULL;
};
```

If we want to insert a new value, `e_new` after `l_update`, our memory would become:

```
bucket_1
-------------------
| e3_new || e_new |
-------------------

bucket_0
------------------------------------
| e1 || e2 || e3 || e4 || .. || en |
------------------------------------
```

We would then create two new linked list nodes.
```c
bucket_t bucket_new = {
    .elems = [e3_new, e_new],
    .len = 2,
};

// a view to the slice [e1, e2].
bucket_linked_list_t l_tail = {
    .bucket = bucket_0,
    .start_index = 1,
    .len = 2,
    .tail = NULL;
};

// the updated element.
bucket_linked_list_t l_update = {
    .bucket = bucket_new,
    .start_index = 1,
    .len = 2,
    .tail = &l_tail;
};

// the new list returned. A view from [e4, .., en] and tail pointing to the updated element.
bucket_linked_list_t l_new = {
    .bucket = bucket_0,
    .start_index = n-1,
    .len = n - 3,
    .tail = &l_update;
}
```
This makes inserts and updates faster, at the price of a more jagged list.

## Removal
Removing the head element is as simple as decrementing the `start_index` if it is bigger than `0`. If `start_index` is `0`, then the new head will be the list tail. Removing an element in the middle of the list creates two other lists with a view into the original. Suppose we want to remove the element `e_3`.
```c
bucket_t bucket_0 = {
    .elems = [e1, e2, e3, e4, ..., en],
    .len = n,
};

bucket_linked_list_t og_list = {
    .bucket = bucket_0,
    .start_index = n - 1,
    .len = n,
    .tail = NULL;
};

// tail of the list with the removed element
bucket_linked_list_t tail = {
    .bucket = bucket_0,
    .start_index = 1,
    .len = 2,
    .tail = NULL;
};

// new list with the removed element
bucket_linked_list_t l_new = {
    .bucket = bucket_0,
    .start_index = n - 1,
    .len = n - 3,
    .tail = &tail;
};
```

## Traversal
Traversal is done backwards from the starting index. After the index hits `start_index + 1 - len`, then we jump to the tail element. Once tail is `null`, the list is completed.

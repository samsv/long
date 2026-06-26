const std = @import("std");
const RC = @import("ref_counter.zig").RC;

/// Persistent, immutable, reference-counted list of shared array buckets.
/// Each `LL` node views a window of a bucket and links to a tail node, so derived
/// versions share structure. Index 0 is the head; iteration runs head to tail.
pub fn List(comptime T: type) type {
    return struct {
        list: RC(LL),

        const Self = @This();

        pub const Bucket = std.ArrayList(T);

        fn deinitBucket(b: *Bucket, gpa: std.mem.Allocator) void {
            const TypeInfo = switch (@typeInfo(T)) {
                .pointer => |info| info.child,
                else => T,
            };

            if (std.meta.hasFn(TypeInfo, "deinit")) for (b.items) |*v|
                v.deinit(gpa);
        }

        /// Drop this reference. The underlying nodes/buckets are freed only once their
        /// last reference is released.
        pub fn deinit(self: *Self, gpa: std.mem.Allocator) void {
            self.list.deinit(gpa);
        }

        pub const LL = struct {
            /// The bucket where data is located.
            bucket: RC(Bucket),
            /// The linked list tail.
            node_tail: ?Self,
            /// The bucket index where the LL starts. Counts backwards.
            start_index: usize,
            /// This linked list node length.
            len: usize,

            /// Release this node's bucket, then recurse into the tail. With refcounting
            /// this tears down the whole chain when it becomes unshared.
            pub fn deinit(ll: *LL, gpa: std.mem.Allocator) void {
                ll.bucket.deinitWithCb(gpa, deinitBucket);

                if (ll.node_tail) |*t|
                    t.deinit(gpa);
            }
        };

        /// If an array has less than `copy_threshold` items, then a cloned LL will be used
        const copy_threshold = 32;

        /// Create a list that takes ownership of `values` (no copy). `values` is freed on error.
        pub fn initOwned(gpa: std.mem.Allocator, values: []T) !Self {
            var bucket = Bucket.fromOwnedSlice(values);
            var bucket_ref = RC(Bucket).init(gpa, bucket) catch |e| {
                bucket.deinit(gpa);
                return e;
            };
            defer bucket_ref.deinit(gpa);

            return initFromBucket(gpa, bucket_ref, null, bucket.items.len - 1, bucket.items.len);
        }

        /// Build a single detached `LL` node from a copy of `values` (the caller wraps it in an `RC`).
        pub fn initRaw(gpa: std.mem.Allocator, values: []const T) !LL {
            var bucket: Bucket = .empty;
            try bucket.appendSlice(gpa, values);
            errdefer bucket.deinit(gpa);

            const bucket_ref = try RC(Bucket).init(gpa, bucket);
            const ll: LL = .{
                .bucket = bucket_ref,
                .node_tail = null,
                .start_index = if (bucket.items.len > 0) bucket.items.len - 1 else 0,
                .len = bucket.items.len,
            };

            return ll;
        }

        /// Build a node over `bucket` for the `len` elements ending at `start_index`
        /// (counting backwards), chained before `ll_tail`.
        /// Borrows `bucket`; takes ownership of the passed `ll_tail` reference.
        fn initFromBucket(
            gpa: std.mem.Allocator,
            bucket: RC(Bucket),
            ll_tail: ?Self,
            start_index: usize,
            len: usize,
        ) !Self {
            var b = bucket.borrow() catch unreachable;
            errdefer b.deinit(gpa);

            return .{
                .list = try RC(LL).init(gpa, .{
                    .bucket = b,
                    .node_tail = ll_tail,
                    .start_index = start_index,
                    .len = len,
                }),
            };
        }

        /// Create a list from a copy of `values`.
        pub fn init(gpa: std.mem.Allocator, values: []const T) !Self {
            var ll = try initRaw(gpa, values);
            errdefer ll.deinit(gpa);
            return .{ .list = try RC(LL).init(gpa, ll) };
        }

        /// Create a list from a copy of `values`, chained in front of `ll_tail`.
        /// The tail is borrowed; an empty `ll_tail` is dropped (no link).
        pub fn initWithTail(gpa: std.mem.Allocator, values: []const T, ll_tail: *Self) !Self {
            var ll = try initRaw(gpa, values);
            errdefer ll.deinit(gpa);

            ll.node_tail = if (ll_tail.list.getUnwrap().len > 0)
                .{ .list = try ll_tail.list.borrow() }
            else
                null;

            return .{ .list = try RC(LL).init(gpa, ll) };
        }

        /// Clone an optional node reference, incrementing its refcount; null stays null.
        pub fn borrow(node: ?Self) ?Self {
            return if (node) |t|
                .{ .list = t.list.borrow() catch unreachable }
            else
                null;
        }

        /// Allocate a fresh bucket holding `values` with `capacity` reserved.
        /// The returned ref starts at refcount 0; the first `borrow` makes its sole owner.
        fn createBucketWithCapacity(gpa: std.mem.Allocator, values: []const T, capacity: usize) !RC(Bucket) {
            var new_bucket: Bucket = .empty;
            try new_bucket.ensureTotalCapacity(gpa, capacity);
            errdefer new_bucket.deinit(gpa);

            new_bucket.appendSliceAssumeCapacity(values);

            var bucket = try RC(Bucket).init(gpa, new_bucket);
            bucket.inner.?.count = 0;
            return bucket;
        }

        /// `createBucketWithCapacity` with capacity exactly `values.len`.
        fn createBucket(gpa: std.mem.Allocator, values: []const T) !RC(Bucket) {
            return createBucketWithCapacity(gpa, values, values.len);
        }

        /// Wrap an already-filled `bucket_ref` as a new head node in front of `ll`'s
        /// (borrowed) tail.
        fn newHead(ll: *LL, gpa: std.mem.Allocator, bucket_ref: RC(Bucket)) !Self {
            var node_tail = borrow(ll.node_tail);
            errdefer if (node_tail) |*t| t.deinit(gpa);
            return initFromBucket(gpa, bucket_ref, node_tail, bucket_ref.getUnwrap().items.len - 1, ll.len + 1);
        }

        /// Element at index `i` (head is 0), or null if `i` is out of range.
        pub fn get(self: Self, i: usize) ?T {
            return if (self.getPtr(i)) |v| v.* else null;
        }

        /// Pointer to the element at index `i` inside the shared bucket, or null if out of
        /// range. Recurses into the tail for indices past this node.
        pub fn getPtr(self: Self, i: usize) ?*T {
            const list = self.list.getUnwrap();
            return if (i < list.len)
                &list.bucket.getUnwrap().items[list.start_index - i]
            else if (list.node_tail) |t|
                t.getPtr(i - list.len)
            else
                null;
        }

        /// Return a new list with `item` at the head. Appends in place when this node owns
        /// the bucket's end; otherwise clones the window (< `copy_threshold`) or chains a
        /// new node. The original list is unchanged.
        pub fn append(self: *Self, gpa: std.mem.Allocator, item: T) !Self {
            const ll = self.list.getPtr() catch unreachable;
            var bucket_ref = ll.bucket;

            var bucket = bucket_ref.getPtrUnwrap();
            if (bucket.items.len > 0 and bucket.items.len - 1 != ll.start_index) {
                // someone has already added data to the bucket
                if (copy_threshold <= ll.len) {
                    return initWithTail(gpa, &[1]T{item}, self);
                }

                const items = bucket_ref.getUnwrap().items;
                const copy_slice = items[ll.start_index + 1 - ll.len .. ll.start_index + 1];
                bucket_ref = try createBucketWithCapacity(gpa, copy_slice, copy_slice.len + 1);
                bucket_ref.getPtrUnwrap().appendAssumeCapacity(item);
                return newHead(ll, gpa, bucket_ref);
            }

            // we have space to append to the bucket
            try bucket.append(gpa, item);
            errdefer _ = bucket.pop();

            return newHead(ll, gpa, bucket_ref);
        }

        /// Append `item` in place, mutating this node. Only valid on a uniquely-owned list
        /// whose `hasSpaceAtHead` is true — used to build a fresh list cheaply.
        pub fn appendMut(self: *Self, gpa: std.mem.Allocator, item: T) !void {
            const ll = self.list.getPtr() catch unreachable;
            var bucket_ref = ll.bucket;

            var bucket = bucket_ref.getPtrUnwrap();
            try bucket.append(gpa, item);
            errdefer _ = bucket.pop();

            ll.len += 1;
            ll.start_index = bucket.items.len - 1;
        }

        /// Return a new list with `item` inserted at index `idx`. `idx == 0` prepends;
        /// `idx == len` appends after this node; `error.IndexOutOfRange` if past the end.
        pub fn insert_at(self: *Self, gpa: std.mem.Allocator, idx: usize, item: T) !Self {
            if (idx == 0) return append(self, gpa, item);

            const ll = self.list.getUnwrap();
            if (idx > ll.len) {
                var node_tail = ll.node_tail orelse return error.IndexOutOfRange;
                var tail_ll = try insert_at(&node_tail, gpa, idx - ll.len, item);
                errdefer tail_ll.deinit(gpa);
                return initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, ll.len);
            }

            var head_node: ?Self = borrow(ll.node_tail);
            errdefer if (head_node) |*t| t.deinit(gpa);
            if (idx < ll.len)
                head_node = try initFromBucket(gpa, ll.bucket, head_node, ll.start_index - idx, ll.len - idx);
            head_node = try initFromBucket(gpa, try createBucket(gpa, &[1]T{item}), head_node, 0, 1);
            return initFromBucket(gpa, ll.bucket, head_node, ll.start_index, idx);
        }

        /// Return a new list with the element at index `idx` replaced by `item`.
        /// `error.IndexOutOfRange` if `idx` is past the end.
        pub fn update(self: *Self, gpa: std.mem.Allocator, idx: usize, item: T) !Self {
            const ll = self.list.getUnwrap();

            if (idx >= ll.len) {
                var node_tail = ll.node_tail orelse return error.IndexOutOfRange;
                var tail_ll = try update(&node_tail, gpa, idx - ll.len, item);
                errdefer tail_ll.deinit(gpa);
                return initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, ll.len);
            }

            var head_node: ?Self = borrow(ll.node_tail);
            errdefer if (head_node) |*t| t.deinit(gpa);
            if (ll.len > idx + 1)
                head_node = try initFromBucket(gpa, ll.bucket, head_node, ll.start_index - idx - 1, ll.len - idx - 1);
            head_node = try initFromBucket(gpa, try createBucket(gpa, &[1]T{item}), head_node, 0, 1);
            if (idx == 0) return head_node.?;
            return initFromBucket(gpa, ll.bucket, head_node, ll.start_index, idx);
        }

        /// Return a new list with the element at index `idx` removed.
        /// `error.IndexOutOfRange` if `idx` is past the end.
        pub fn delete_at(self: *Self, gpa: std.mem.Allocator, idx: usize) !Self {
            const ll = self.list.getUnwrap();

            if (idx == 0 and ll.len > 0)
                return try tail(self, gpa) orelse init(gpa, &[_]T{});

            if (idx >= ll.len) {
                var ll_tail = ll.node_tail orelse return error.IndexOutOfRange;

                var tail_ll = try delete_at(&ll_tail, gpa, idx - ll.len);
                if (tail_ll.list.getPtrUnwrap().len == 0) {
                    defer tail_ll.deinit(gpa);
                    return initFromBucket(gpa, ll.bucket, null, ll.start_index, ll.len);
                }

                errdefer tail_ll.deinit(gpa);
                return initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, ll.len);
            }

            var head_node: ?Self = borrow(ll.node_tail);
            errdefer if (head_node) |*t| t.deinit(gpa);
            if (ll.len > idx + 1)
                head_node = try initFromBucket(gpa, ll.bucket, head_node, ll.start_index - idx - 1, ll.len - idx - 1);
            return initFromBucket(gpa, ll.bucket, head_node, ll.start_index, idx);
        }

        /// Head element, or null if the list is empty.
        pub fn head(self: Self) ?T {
            const ll = self.list.getUnwrap();
            return if (ll.len == 0) null else ll.bucket.getUnwrap().items[ll.start_index];
        }

        /// Return the list without its head, or null if the list is empty.
        pub fn tail(self: *Self, gpa: std.mem.Allocator) !?Self {
            const ll = self.list.getUnwrap();
            if (ll.len == 0) return null;

            var node_tail = borrow(ll.node_tail);
            if (ll.len == 1) return node_tail;
            errdefer if (node_tail) |*t| t.deinit(gpa);
            return try initFromBucket(gpa, ll.bucket, node_tail, ll.start_index - 1, ll.len - 1);
        }

        /// Total number of elements across every node.
        pub fn count(self: Self) usize {
            const list = self.list.getUnwrap();
            return list.len + if (list.node_tail) |t| t.count() else 0;
        }

        /// Whether `appendMut` can append in place — true when the bucket is empty or this
        /// node's head is the bucket's last element.
        pub fn hasSpaceAtHead(self: Self) bool {
            const ll = self.list.getPtrUnwrap();
            const bucket = ll.bucket.getPtrUnwrap();
            return bucket.items.len == 0 or bucket.items.len - 1 == ll.start_index;
        }

        /// Borrowing forward iterator over the elements; release with `Iterator.deinit`.
        pub fn iter(self: *Self) Iterator {
            return Iterator.init(self);
        }

        /// Non-borrowing forward iterator; valid only while the list outlives it
        /// (never call `Iterator.deinit` on it).
        pub fn iterNoBorrow(self: Self) Iterator {
            return Iterator.initNoBorrow(self);
        }

        /// Forward iterator yielding elements head to tail across nodes.
        pub const Iterator = struct {
            root: Self,
            ll: ?Self,
            current: usize,

            /// Iterate `ll` without taking a reference. Do not pair with `deinit`.
            pub fn initNoBorrow(ll: Self) Iterator {
                const current_ll = if (ll.list.getUnwrap().len > 0) ll else null;
                return .{
                    .root = ll,
                    .ll = current_ll,
                    .current = ll.list.getUnwrap().start_index,
                };
            }

            /// Iterate `ll`, borrowing it; release with `deinit`.
            pub fn init(ll: *Self) Iterator {
                const b = Self{ .list = ll.list.borrow() catch unreachable };
                const current_ll = if (b.list.getUnwrap().len > 0) b else null;
                return .{
                    .root = b,
                    .ll = current_ll,
                    .current = ll.list.getUnwrap().start_index,
                };
            }

            /// Next element, or null when exhausted; walks within a node then into its tail.
            pub fn next(iterator: *Iterator) ?T {
                const ll = (iterator.ll orelse return null).list.getUnwrap();
                const item = ll.bucket.getUnwrap().items[iterator.current];

                if (iterator.current == ll.start_index + 1 - ll.len) {
                    const ll_tail = ll.node_tail;
                    iterator.ll = ll_tail;
                    if (ll_tail) |t|
                        iterator.current = t.list.getUnwrap().start_index;
                } else {
                    iterator.current -= 1;
                }

                return item;
            }

            /// Release the reference taken by `init`.
            pub fn deinit(iterator: *Iterator, gpa: std.mem.Allocator) void {
                iterator.root.deinit(gpa);
            }
        };

        /// True if the list's elements equal `slice` in order.
        /// `T` must support `==` (a scalar/comparable type); structs or slices won't compile.
        pub fn equalsSlice(ll: Self, slice: []const T) bool {
            var iterator = Iterator.initNoBorrow(ll);

            var i: usize = 0;
            while (iterator.next()) |v| : (i += 1) {
                if (v != slice[i]) return false;
            }

            return i == slice.len;
        }
    };
}

test {
    _ = @import("list_tests.zig");
}

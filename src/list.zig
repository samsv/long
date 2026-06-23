const std = @import("std");
const RC = @import("ref_counter.zig").RC;

pub fn List(comptime T: type) type {
    return struct {
        list: RC(LL),

        const Self = @This();

        pub const Bucket = std.ArrayList(T);

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

            pub fn deinit(ll: *LL, gpa: std.mem.Allocator) void {
                ll.bucket.deinit(gpa);

                if (ll.node_tail) |*t|
                    t.deinit(gpa);
            }
        };

        /// If an array has less than `copy_threshold` items, then a cloned LL will be used
        const copy_threshold = 32;

        pub fn initOwned(gpa: std.mem.Allocator, values: []T) !Self {
            var bucket = Bucket.fromOwnedSlice(values);
            var bucket_ref = RC(Bucket).init(gpa, bucket) catch |e| {
                bucket.deinit(gpa);
                return e;
            };
            defer bucket_ref.deinit(gpa);

            return initFromBucket(gpa, bucket_ref, null, bucket.items.len - 1, bucket.items.len);
        }

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

        pub fn init(gpa: std.mem.Allocator, values: []const T) !Self {
            var ll = try initRaw(gpa, values);
            errdefer ll.deinit(gpa);
            return .{ .list = try RC(LL).init(gpa, ll) };
        }

        pub fn initWithTail(gpa: std.mem.Allocator, values: []const T, ll_tail: *Self) !Self {
            var ll = try initRaw(gpa, values);
            errdefer ll.deinit(gpa);

            ll.node_tail = if (ll_tail.list.getUnwrap().len > 0)
                .{ .list = try ll_tail.list.borrow() }
            else
                null;

            return .{ .list = try RC(LL).init(gpa, ll) };
        }

        pub fn borrow(node: ?Self) ?Self {
            return if (node) |t|
                .{ .list = t.list.borrow() catch unreachable }
            else
                null;
        }

        fn createBucketWithCapacity(gpa: std.mem.Allocator, values: []const T, capacity: usize) !RC(Bucket) {
            var new_bucket: Bucket = .empty;
            try new_bucket.ensureTotalCapacity(gpa, capacity);
            errdefer new_bucket.deinit(gpa);

            new_bucket.appendSliceAssumeCapacity(values);

            var bucket = try RC(Bucket).init(gpa, new_bucket);
            bucket.value.?.count = 0;
            return bucket;
        }

        fn createBucket(gpa: std.mem.Allocator, values: []const T) !RC(Bucket) {
            return createBucketWithCapacity(gpa, values, values.len);
        }

        fn newHead(ll: *LL, gpa: std.mem.Allocator, bucket_ref: RC(Bucket)) !Self {
            var node_tail = borrow(ll.node_tail);
            errdefer if (node_tail) |*t| t.deinit(gpa);
            return initFromBucket(gpa, bucket_ref, node_tail, bucket_ref.getUnwrap().items.len - 1, ll.len + 1);
        }

        pub fn get(self: Self, i: usize) ?T {
            return if (self.getPtr(i)) |v| v.* else null;
        }

        pub fn getPtr(self: Self, i: usize) ?*T {
            const list = self.list.getUnwrap();
            return if (i < list.len)
                &list.bucket.getUnwrap().items[list.start_index - i]
            else if (list.node_tail) |t|
                t.getPtr(i - list.len)
            else
                null;
        }

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

        pub fn appendMut(self: *Self, gpa: std.mem.Allocator, item: T) !void {
            const ll = self.list.getPtr() catch unreachable;
            var bucket_ref = ll.bucket;

            var bucket = bucket_ref.getPtrUnwrap();
            try bucket.append(gpa, item);
            errdefer _ = bucket.pop();

            ll.len += 1;
            ll.start_index = bucket.items.len - 1;
        }

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

        pub fn head(self: Self) ?T {
            const ll = self.list.getUnwrap();
            return if (ll.len == 0) null else ll.bucket.getUnwrap().items[ll.start_index];
        }

        pub fn tail(self: *Self, gpa: std.mem.Allocator) !?Self {
            const ll = self.list.getUnwrap();
            if (ll.len == 0) return null;

            var node_tail = borrow(ll.node_tail);
            if (ll.len == 1) return node_tail;
            errdefer if (node_tail) |*t| t.deinit(gpa);
            return try initFromBucket(gpa, ll.bucket, node_tail, ll.start_index - 1, ll.len - 1);
        }

        pub fn count(self: Self) usize {
            const list = self.list.getUnwrap();
            return list.len + if (list.node_tail) |t| t.count() else 0;
        }

        pub fn hasSpaceAtHead(self: Self) bool {
            const ll = self.list.getPtrUnwrap();
            const bucket = ll.bucket.getPtrUnwrap();
            return bucket.items.len == 0 or bucket.items.len - 1 == ll.start_index;
        }

        pub fn iter(self: *Self) Iterator {
            return Iterator.init(self);
        }

        pub fn iterNoBorrow(self: Self) Iterator {
            return Iterator.initNoBorrow(self);
        }

        pub const Iterator = struct {
            root: Self,
            ll: ?Self,
            current: usize,

            pub fn initNoBorrow(ll: Self) Iterator {
                const current_ll = if (ll.list.getUnwrap().len > 0) ll else null;
                return .{
                    .root = ll,
                    .ll = current_ll,
                    .current = ll.list.getUnwrap().start_index,
                };
            }

            pub fn init(ll: *Self) Iterator {
                const b = Self{ .list = ll.list.borrow() catch unreachable };
                const current_ll = if (b.list.getUnwrap().len > 0) b else null;
                return .{
                    .root = b,
                    .ll = current_ll,
                    .current = ll.list.getUnwrap().start_index,
                };
            }

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

            pub fn deinit(iterator: *Iterator, gpa: std.mem.Allocator) void {
                iterator.root.deinit(gpa);
            }
        };

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

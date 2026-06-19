const std = @import("std");
const RC = @import("ref_counter.zig").RC;

pub fn List(comptime T: type) type {
    return struct {
        pub const Bucket = std.ArrayList(T);

        pub const LL = struct {
            /// The bucket where data is located.
            bucket: RC(Bucket),
            /// The linked list tail.
            node_tail: ?RC(LL),
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
            ll_tail: ?RC(LL),
            start_index: usize,
            len: usize,
        ) !RC(LL) {
            var b = bucket.borrow() catch unreachable;
            errdefer b.deinit(gpa);

            return try RC(LL).init(gpa, .{
                .bucket = b,
                .node_tail = ll_tail,
                .start_index = start_index,
                .len = len,
            });
        }

        pub fn init(gpa: std.mem.Allocator, values: []const T) !RC(LL) {
            var ll = try initRaw(gpa, values);
            errdefer ll.deinit(gpa);
            return try RC(LL).init(gpa, ll);
        }

        pub fn initWithTail(gpa: std.mem.Allocator, values: []const T, ll_tail: *RC(LL)) !RC(LL) {
            var ll = try initRaw(gpa, values);
            errdefer ll.deinit(gpa);

            ll.node_tail = if (ll_tail.getUnwrap().len > 0)
                try ll_tail.borrow()
            else
                null;

            return try RC(LL).init(gpa, ll);
        }

        fn borrow(ll_ref: ?RC(LL)) ?RC(LL) {
            return if (ll_ref) |t|
                t.borrow() catch unreachable
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

        fn appendAssumeCapacity(ll: *LL, gpa: std.mem.Allocator, bucket_ref: RC(Bucket)) !RC(LL) {
            var node_tail = borrow(ll.node_tail);
            errdefer if (node_tail) |*t| t.deinit(gpa);
            return try initFromBucket(gpa, bucket_ref, node_tail, bucket_ref.getUnwrap().items.len - 1, ll.len + 1);
        }

        pub fn append(ll_ref: *RC(LL), gpa: std.mem.Allocator, item: T) !RC(LL) {
            const ll = ll_ref.getPtr() catch unreachable;
            var bucket_ref = ll.bucket;

            var bucket = bucket_ref.getPtrUnwrap();
            if (bucket.items.len > 0 and bucket.items.len - 1 != ll.start_index) {
                // someone has already added data to the bucket
                if (copy_threshold <= ll.len) {
                    return try initWithTail(gpa, &[1]T{item}, ll_ref);
                }

                const items = bucket_ref.getUnwrap().items;
                const copy_slice = items[ll.start_index + 1 - ll.len .. ll.start_index + 1];
                bucket_ref = try createBucketWithCapacity(gpa, copy_slice, copy_slice.len + 1);
                bucket_ref.getPtrUnwrap().appendAssumeCapacity(item);
                return try appendAssumeCapacity(ll, gpa, bucket_ref);
            }

            // we have space to append to the bucket
            try bucket.append(gpa, item);
            errdefer _ = bucket.pop();

            return try appendAssumeCapacity(ll, gpa, bucket_ref);
        }

        pub fn insert_at(ll_ref: *RC(LL), gpa: std.mem.Allocator, idx: usize, item: T) !RC(LL) {
            if (idx == 0) return append(ll_ref, gpa, item);

            const ll = ll_ref.getUnwrap();
            if (idx > ll.len) {
                var node_tail = ll.node_tail orelse return error.IndexOutOfRange;
                var tail_ll = try insert_at(&node_tail, gpa, idx - ll.len, item);
                errdefer tail_ll.deinit(gpa);
                return try initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, ll.len);
            }

            var head_node: ?RC(LL) = borrow(ll.node_tail);
            errdefer if (head_node) |*t| t.deinit(gpa);
            if (idx < ll.len)
                head_node = try initFromBucket(gpa, ll.bucket, head_node, ll.start_index - idx, ll.len - idx);
            head_node = try initFromBucket(gpa, try createBucket(gpa, &[1]T{item}), head_node, 0, 1);
            return try initFromBucket(gpa, ll.bucket, head_node, ll.start_index, idx);
        }

        pub fn head(ll_ref: RC(LL)) ?T {
            const ll = ll_ref.getUnwrap();
            return if (ll.len == 0) null else ll.bucket.getUnwrap().items[ll.start_index];
        }

        pub fn tail(ll_ref: *RC(LL), gpa: std.mem.Allocator) !?RC(LL) {
            const ll = ll_ref.getUnwrap();
            if (ll.len == 0) return null;

            var node_tail = borrow(ll.node_tail);
            if (ll.len == 1) return node_tail;
            errdefer if (node_tail) |*t| t.deinit(gpa);
            return try initFromBucket(gpa, ll.bucket, node_tail, ll.start_index - 1, ll.len - 1);
        }

        pub fn delete_at(ll_ref: *RC(LL), gpa: std.mem.Allocator, idx: usize) !RC(LL) {
            const ll = ll_ref.getUnwrap();

            if (idx == 0 and ll.len > 0)
                return try tail(ll_ref, gpa) orelse init(gpa, &[_]T{});

            if (idx >= ll.len) {
                var ll_tail = ll.node_tail orelse return error.IndexOutOfRange;

                var tail_ll = try delete_at(&ll_tail, gpa, idx - ll.len);
                errdefer tail_ll.deinit(gpa);

                if (tail_ll.getPtrUnwrap().len == 0) {
                    defer tail_ll.deinit(gpa);
                    var node_tail = borrow(tail_ll.getUnwrap().node_tail);
                    errdefer if (node_tail) |*t| t.deinit(gpa);
                    return try initFromBucket(gpa, ll.bucket, node_tail, ll.start_index, ll.len);
                }

                return try initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, ll.len);
            }

            var head_node: ?RC(LL) = borrow(ll.node_tail);
            errdefer if (head_node) |*t| t.deinit(gpa);
            if (ll.len > idx + 1)
                head_node = try initFromBucket(gpa, ll.bucket, head_node, ll.start_index - idx - 1, ll.len - idx - 1);
            return try initFromBucket(gpa, ll.bucket, head_node, ll.start_index, idx);
        }

        pub const Iterator = struct {
            root: RC(LL),
            ll: ?RC(LL),
            current: usize,

            pub fn initNoBorrow(ll: RC(LL)) Iterator {
                const current_ll = if (ll.getUnwrap().len > 0) ll else null;
                return .{
                    .root = ll,
                    .ll = current_ll,
                    .current = ll.getUnwrap().start_index,
                };
            }

            pub fn init(ll: *RC(LL)) Iterator {
                const b = ll.borrow() catch unreachable;
                const current_ll = if (b.getUnwrap().len > 0) b else null;
                return .{
                    .root = b,
                    .ll = current_ll,
                    .current = ll.getUnwrap().start_index,
                };
            }

            pub fn next(iter: *Iterator) ?T {
                const ll = (iter.ll orelse return null).getUnwrap();
                const item = ll.bucket.getUnwrap().items[iter.current];

                if (iter.current == ll.start_index + 1 - ll.len) {
                    const ll_tail = ll.node_tail;
                    iter.ll = ll_tail;
                    if (ll_tail) |t|
                        iter.current = t.getUnwrap().start_index;
                } else {
                    iter.current -= 1;
                }

                return item;
            }

            pub fn deinit(iter: *Iterator, gpa: std.mem.Allocator) void {
                iter.root.deinit(gpa);
            }
        };

        fn equalsSlice(ll: RC(LL), slice: []const T) bool {
            var iter = Iterator.initNoBorrow(ll);

            var i: usize = 0;
            while (iter.next()) |v| : (i += 1) {
                if (v != slice[i]) return false;
            }

            return i == slice.len;
        }
    };
}

test "Append" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var empty = try MyList.init(gpa, &[_]u32{});
    defer empty.deinit(gpa);

    var single = try MyList.init(gpa, &[_]u32{7});
    defer single.deinit(gpa);

    var new_list_0 = try MyList.append(&list, gpa, 4);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.append(&new_list_0, gpa, 6);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try MyList.append(&list, gpa, 5);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try MyList.append(&list, gpa, 9);
    defer new_list_3.deinit(gpa);

    var new_list_4 = try MyList.append(&empty, gpa, 1);
    defer new_list_4.deinit(gpa);

    var new_list_5 = try MyList.append(&single, gpa, 8);
    defer new_list_5.deinit(gpa);

    try std.testing.expect(MyList.equalsSlice(new_list_0, &[_]u32{ 4, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_1, &[_]u32{ 6, 4, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_2, &[_]u32{ 5, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_3, &[_]u32{ 9, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_4, &[_]u32{1}));
    try std.testing.expect(MyList.equalsSlice(new_list_5, &[_]u32{ 8, 7 }));
}

test "Insert" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var new_list_0 = try MyList.insert_at(&list, gpa, 0, 9);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.insert_at(&list, gpa, 1, 9);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try MyList.insert_at(&list, gpa, 2, 9);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try MyList.insert_at(&new_list_2, gpa, 3, 7);
    defer new_list_3.deinit(gpa);

    var new_list_4 = try MyList.insert_at(&new_list_2, gpa, 1, 5);
    defer new_list_4.deinit(gpa);

    var new_list_5 = try MyList.insert_at(&list, gpa, 3, 0);
    defer new_list_5.deinit(gpa);

    try std.testing.expect(MyList.equalsSlice(new_list_0, &[_]u32{ 9, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_1, &[_]u32{ 3, 9, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_2, &[_]u32{ 3, 2, 9, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_3, &[_]u32{ 3, 2, 9, 7, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_4, &[_]u32{ 3, 5, 2, 9, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_5, &[_]u32{ 3, 2, 1, 0 }));
}

test "remove" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3, 4, 5 });
    defer list.deinit(gpa);

    var single = try MyList.init(gpa, &[_]u32{7});
    defer single.deinit(gpa);

    var head = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer head.deinit(gpa);

    var head_list = try MyList.initWithTail(gpa, &[_]u32{9}, &head);
    defer head_list.deinit(gpa);

    var tail_node = try MyList.init(gpa, &[_]u32{9});
    defer tail_node.deinit(gpa);

    var tail_list = try MyList.initWithTail(gpa, &[_]u32{ 1, 2, 3 }, &tail_node);
    defer tail_list.deinit(gpa);

    var new_list_0 = try MyList.delete_at(&list, gpa, 0);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.delete_at(&list, gpa, 1);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try MyList.delete_at(&list, gpa, 2);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try MyList.delete_at(&list, gpa, 3);
    defer new_list_3.deinit(gpa);

    var new_list_4 = try MyList.delete_at(&new_list_2, gpa, 2);
    defer new_list_4.deinit(gpa);

    var new_list_5 = try MyList.delete_at(&list, gpa, 4);
    defer new_list_5.deinit(gpa);

    var new_list_6 = try MyList.delete_at(&head_list, gpa, 0);
    defer new_list_6.deinit(gpa);

    var new_list_7 = try MyList.delete_at(&tail_list, gpa, 3);
    defer new_list_7.deinit(gpa);

    var new_list_8 = try MyList.delete_at(&single, gpa, 0);
    defer new_list_8.deinit(gpa);

    try std.testing.expect(MyList.equalsSlice(new_list_0, &[_]u32{ 4, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_1, &[_]u32{ 5, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_2, &[_]u32{ 5, 4, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_3, &[_]u32{ 5, 4, 3, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_4, &[_]u32{ 5, 4, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_5, &[_]u32{ 5, 4, 3, 2 }));
    try std.testing.expect(MyList.equalsSlice(new_list_6, &[_]u32{ 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_7, &[_]u32{ 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_8, &[_]u32{}));
}

test "Empty" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{});
    defer list.deinit(gpa);

    var new_list_0 = try MyList.append(&list, gpa, 1);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.insert_at(&list, gpa, 0, 5);
    defer new_list_1.deinit(gpa);

    try std.testing.expect(MyList.equalsSlice(list, &[_]u32{}));
    try std.testing.expect(MyList.equalsSlice(new_list_0, &[_]u32{1}));
    try std.testing.expect(MyList.equalsSlice(new_list_1, &[_]u32{5}));
    try std.testing.expectError(error.IndexOutOfRange, MyList.insert_at(&list, gpa, 2, 9));
    try std.testing.expectError(error.IndexOutOfRange, MyList.delete_at(&list, gpa, 0));
}

test "tail" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var empty = try MyList.init(gpa, &[_]u32{});
    defer empty.deinit(gpa);

    var list = try MyList.initWithTail(gpa, &[_]u32{ 1, 2, 3 }, &empty);
    defer list.deinit(gpa);

    const empty_tail = try MyList.tail(&empty, gpa);

    try std.testing.expect(MyList.equalsSlice(list, &[_]u32{ 3, 2, 1 }));
    try std.testing.expect(empty_tail == null);
}

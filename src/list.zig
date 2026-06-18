const std = @import("std");
const RefCounter = @import("ref_counter.zig").RefCounter;

pub fn List(comptime T: type) type {
    return struct {
        pub const Bucket = std.ArrayList(T);

        pub const LL = struct {
            /// The bucket where data is located.
            bucket: RefCounter(Bucket).Ref,
            /// The linked list tail.
            node_tail: ?Ref,
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

        const Ref = RefCounter(LL).Ref;
        /// If an array has less than `copy_threshold` items, then a cloned LL will be used
        const copy_threshold = 32;

        pub fn initRaw(gpa: std.mem.Allocator, values: []const T) !LL {
            var bucket: Bucket = .empty;
            try bucket.appendSlice(gpa, values);

            const bucket_ref = try RefCounter(Bucket).init(gpa, bucket);
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
            bucket: RefCounter(Bucket).Ref,
            ll_tail: ?Ref,
            start_index: usize,
            len: usize,
        ) !Ref {
            return try RefCounter(LL).init(gpa, .{
                .bucket = bucket.borrow() catch unreachable,
                .node_tail = ll_tail,
                .start_index = start_index,
                .len = len,
            });
        }

        pub fn init(gpa: std.mem.Allocator, values: []const T) !Ref {
            const ll = try initRaw(gpa, values);
            return RefCounter(LL).init(gpa, ll);
        }

        pub fn initWithTail(gpa: std.mem.Allocator, values: []const T, ll_tail: *Ref) !Ref {
            var ll = try initRaw(gpa, values);
            ll.node_tail = try ll_tail.borrow();
            return RefCounter(LL).init(gpa, ll);
        }

        fn createBucket(gpa: std.mem.Allocator, values: []const T) !RefCounter(Bucket).Ref {
            var new_bucket: Bucket = .empty;
            try new_bucket.ensureTotalCapacity(gpa, values.len);
            new_bucket.appendSliceAssumeCapacity(values);

            var bucket = try RefCounter(Bucket).init(gpa, new_bucket);
            bucket.value.?.count = 0;
            return bucket;
        }

        pub fn append(ll_ref: *Ref, gpa: std.mem.Allocator, item: T) !Ref {
            const ll = ll_ref.getPtr() catch unreachable;
            var bucket_ref = ll.bucket;

            const bucket = bucket_ref.getUnwrap();
            if (bucket.items.len > 0 and bucket.items.len - 1 != ll.start_index) {
                // someone has already added data to the bucket
                if (copy_threshold <= ll.len) {
                    return try initWithTail(gpa, &[1]T{item}, ll_ref);
                }

                const items = bucket_ref.getUnwrap().items;
                bucket_ref = try createBucket(gpa, items[ll.start_index + 1 - ll.len .. ll.start_index + 1]);
            }

            // we have space to append to the bucket
            try bucket_ref.getPtrUnwrap().append(gpa, item);
            const new_ll: LL = .{
                .bucket = bucket_ref.borrow() catch unreachable,
                .node_tail = if (ll.node_tail) |t| t.borrow() catch unreachable else null,
                .start_index = bucket_ref.getUnwrap().items.len - 1,
                .len = ll.len + 1,
            };
            return RefCounter(LL).init(gpa, new_ll);
        }

        pub fn insert_at(ll_ref: *Ref, gpa: std.mem.Allocator, idx: usize, item: T) !Ref {
            if (idx == 0) return append(ll_ref, gpa, item);

            const ll = ll_ref.getUnwrap();
            if (idx >= ll.len) {
                var ll_tail = ll.node_tail orelse return error.IndexOutOfRange;

                const tail_ll = try insert_at(&ll_tail, gpa, idx - ll.len, item);
                return try initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, ll.len);
            }

            const ll_tail = if (ll.node_tail) |t| t.borrow() catch unreachable else null;
            const tail_ll = try initFromBucket(gpa, ll.bucket, ll_tail, ll.start_index - idx, ll.len - idx);
            const item_ll = try initFromBucket(gpa, try createBucket(gpa, &[1]T{item}), tail_ll, 0, 1);
            const head_ll = try initFromBucket(gpa, ll.bucket, item_ll, ll.start_index, idx);
            return head_ll;
        }

        pub fn tail(ll_ref: *Ref, gpa: std.mem.Allocator) !?Ref {
            const ll = ll_ref.getUnwrap();
            const node_tail = if (ll.node_tail) |t| t.borrow() catch unreachable else null;
            return if (ll.len == 1)
                node_tail
            else
                try initFromBucket(gpa, ll.bucket, node_tail, ll.start_index - 1, ll.len - 1);
        }

        pub fn delete_at(ll_ref: *Ref, gpa: std.mem.Allocator, idx: usize) !Ref {
            if (idx == 0) return try tail(ll_ref, gpa) orelse error.IndexOutOfRange;

            const ll = ll_ref.getUnwrap();
            if (idx >= ll.len) {
                var ll_tail = ll.node_tail orelse return error.IndexOutOfRange;

                const tail_ll = try delete_at(&ll_tail, gpa, idx - ll.len);
                return try initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, ll.len);
            }

            const node_tail = if (ll.node_tail) |t| t.borrow() catch unreachable else null;
            const tail_ll = if (ll.len > idx)
                try initFromBucket(gpa, ll.bucket, node_tail, ll.start_index - idx - 1, ll.len - idx - 1)
            else
                node_tail;
            const head_ll = try initFromBucket(gpa, ll.bucket, tail_ll, ll.start_index, idx);

            return head_ll;
        }

        pub const Iterator = struct {
            root: Ref,
            ll: ?Ref,
            current: usize,

            pub fn initNoBorrow(ll: Ref) Iterator {
                return .{
                    .root = ll,
                    .ll = ll,
                    .current = ll.getUnwrap().start_index,
                };
            }

            pub fn init(ll: *Ref) Iterator {
                const b = ll.borrow() catch unreachable;
                const current_ll = if (b.getUnwrap().len == 0) null else b;
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
                var r = &iter.root;
                while ((r.getPtr() catch unreachable).node_tail) |*t| {
                    r.deinit(gpa);
                    r = t;
                }
                r.deinit(gpa);
            }
        };

        fn equalsSlice(ll: Ref, slice: []const T) bool {
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

    var new_list_0 = try MyList.append(&list, gpa, 4);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.append(&list, gpa, 5);
    defer new_list_1.deinit(gpa);

    try std.testing.expect(MyList.equalsSlice(new_list_0, &[_]u32{ 4, 3, 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_1, &[_]u32{ 5, 3, 2, 1 }));
}

test "Insert" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var new_list_0 = try MyList.insert_at(&list, gpa, 2, 4);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.insert_at(&new_list_0, gpa, 2, 5);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try MyList.insert_at(&new_list_1, gpa, 0, 8);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try MyList.insert_at(&new_list_1, gpa, 0, 7);
    defer new_list_3.deinit(gpa);

    try std.testing.expect(MyList.equalsSlice(new_list_0, &[_]u32{ 3, 2, 4, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_1, &[_]u32{ 3, 2, 5, 4, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_2, &[_]u32{ 8, 3, 2, 5, 4, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_3, &[_]u32{ 7, 3, 2, 5, 4, 1 }));
}

test "remove" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var new_list_0 = try MyList.delete_at(&list, gpa, 0);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.delete_at(&list, gpa, 1);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try MyList.delete_at(&new_list_1, gpa, 0);
    defer new_list_2.deinit(gpa);

    try std.testing.expect(MyList.equalsSlice(new_list_0, &[_]u32{ 2, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_1, &[_]u32{ 3, 1 }));
    try std.testing.expect(MyList.equalsSlice(new_list_2, &[_]u32{ 1 }));
}

const std = @import("std");
const RefCounter = @import("ref_counter.zig").RefCounter;

pub fn List(comptime T: type) type {
    return struct {
        pub const Bucket = std.ArrayList(T);

        pub const LL = struct {
            bucket: RefCounter(Bucket).Ref,
            tail: ?Ref,
            start_index: usize,
            len: usize,

            pub fn deinit(ll: *LL, gpa: std.mem.Allocator) void {
                ll.bucket.deinit(gpa);

                if (ll.tail) |*tail|
                    tail.deinit(gpa);
            }
        };

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
                return .{
                    .root = b,
                    .ll = b,
                    .current = ll.getUnwrap().start_index,
                };
            }

            pub fn next(iter: *Iterator) ?T {
                const ll = (iter.ll orelse return null).getUnwrap();
                const item = ll.bucket.getUnwrap().items[iter.current];

                if (iter.current == ll.start_index + 1 - ll.len) {
                    const tail = ll.tail;
                    iter.ll = tail;
                    if (tail) |t|
                        iter.current = t.getUnwrap().start_index;
                } else {
                    iter.current -= 1;
                }

                return item;
            }

            pub fn deinit(iter: *Iterator, gpa: std.mem.Allocator) void {
                var r = &iter.root;
                while ((r.getPtr() catch unreachable).tail) |*tail| {
                    r.deinit(gpa);
                    r = tail;
                }
                r.deinit(gpa);
            }
        };

        const Ref = RefCounter(LL).Ref;

        pub fn initRaw(gpa: std.mem.Allocator, values: []const T) !LL {
            var bucket: Bucket = .empty;
            try bucket.appendSlice(gpa, values);

            const bucket_ref = try RefCounter(Bucket).init(gpa, bucket);
            const ll: LL = .{
                .bucket = bucket_ref,
                .tail = null,
                .start_index = if (bucket.items.len > 0) bucket.items.len - 1 else 0,
                .len = bucket.items.len,
            };

            return ll;
        }

        pub fn init(gpa: std.mem.Allocator, values: []const T) !Ref {
            const ll = try initRaw(gpa, values);
            return RefCounter(LL).init(gpa, ll);
        }

        pub fn initWithTail(gpa: std.mem.Allocator, values: []const T, tail: *Ref) !Ref {
            var ll = try initRaw(gpa, values);
            ll.tail = try tail.borrow();
            return RefCounter(LL).init(gpa, ll);
        }

        pub fn append(ll_ref: *Ref, gpa: std.mem.Allocator, item: T) !Ref {
            var ll = ll_ref.getPtr() catch unreachable;
            var bucket = ll.bucket.getPtr() catch unreachable;

            if (bucket.items.len - 1 != ll.start_index) {
                // someone has already added data to the bucket
                return try initWithTail(gpa, &[1]T{item}, ll_ref);
            }

            // we have space to append to the bucket
            try bucket.append(gpa, item);
            const new_ll: LL = .{
                .bucket = ll.bucket.borrow() catch unreachable,
                .tail = null,
                .start_index = ll.start_index + 1,
                .len = ll.len + 1,
            };
            return RefCounter(LL).init(gpa, new_ll);
        }
    };
}

test "Append" {
    const MyList = List(u32);

    const equalsSlice = struct {
        pub fn f(ll: MyList.Ref, slice: []const u32) bool {
            var iter = MyList.Iterator.initNoBorrow(ll);

            var i: usize = 0;
            while (iter.next()) |v| : (i += 1) {
                if (v != slice[i]) return false;
            }

            return i == slice.len;
        }
    }.f;
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var new_list_0 = try MyList.append(&list, gpa, 4);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try MyList.append(&list, gpa, 5);
    defer new_list_1.deinit(gpa);

    try std.testing.expect(equalsSlice(new_list_0, &[_]u32{ 4, 3, 2, 1 }));
    try std.testing.expect(equalsSlice(new_list_1, &[_]u32{ 5, 3, 2, 1 }));
}

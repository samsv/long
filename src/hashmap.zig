const std = @import("std");
const List = @import("list.zig").List;
const RC = @import("ref_counter.zig").RC;

/// A Sparse for private use inside the immutable hashmap
fn SparseSet(comptime T: type) type {
    return struct {
        dense: List(Item),
        sparse: Sparse,

        const Self = @This();
        const Sparse = RC(std.ArrayList(?usize));

        pub const Item = struct {
            sparse_index: usize,
            value: T,
        };

        pub fn init(gpa: std.mem.Allocator, values: []const Item, size: usize) !Self {
            var dense = try List(Item).init(gpa, values);
            errdefer dense.deinit(gpa);

            var sparse_arr = try std.ArrayList(?usize).initCapacity(gpa, size);
            sparse_arr.appendNTimesAssumeCapacity(null, size);
            for (values, 0..) |v, i|
                sparse_arr.items[v.sparse_index] = i;

            errdefer sparse_arr.deinit(gpa);

            return .{ .sparse = try Sparse.init(gpa, sparse_arr), .dense = dense };
        }

        pub fn deinit(self: *Self, gpa: std.mem.Allocator) void {
            self.dense.deinit(gpa);
            self.sparse.deinit(gpa);
        }

        fn denseGet(self: Self, index: usize) ?Item {
            const c = self.dense.count();
            if (index >= c) return null;
            return self.dense.get(c - index - 1);
        }

        pub fn get(self: Self, index: usize) ?T {
            const i = self.sparse.getUnwrap().items[index] orelse return null;
            const item = self.denseGet(i) orelse return null;
            return if (item.sparse_index == index) item.value else null;
        }

        pub fn insert(self: *Self, gpa: std.mem.Allocator, item: Item) !Self {
            if (!self.dense.hasSpaceAtHead()) return error.NoSpaceOnDense;

            self.sparse.getPtrUnwrap().items[item.sparse_index] = self.dense.count();
            var new_sparse = try self.sparse.borrow();
            errdefer new_sparse.deinit(gpa);

            const new_dense = try self.dense.append(gpa, item);

            return .{
                .sparse = new_sparse,
                .dense = new_dense,
            };
        }
    };
}

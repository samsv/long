const std = @import("std");
const List = @import("list.zig").List;
const RC = @import("ref_counter.zig").RC;

pub fn Ctx(comptime K: type) type {
    return struct {
        hash: *const fn (K) u32,
        eql: *const fn (K, K) bool,
    };
}

pub fn HashMap(comptime K: type, comptime V: type, comptime ctx: Ctx(K)) type {
    return struct {
        map: RC(Map),

        const Self = @This();

        const max_load_percentage = 75;
        const max_copy_size = 32;
        const max_depth = 5;

        pub const KV = struct {
            key: K,
            value: ?V,
        };

        const Item = struct {
            item: SparseSet.Item,
            depth: u8,
        };

        const GetResult = union(enum) {
            item: Item,
            empty: usize,
            full,
        };

        pub const Map = struct {
            set: SparseSet,
            child: ?Self,
            depth: u8,

            const GetRet = struct {
                kv: KV,
                depth: u8,
            };

            fn init(set: SparseSet, child: ?Self) Map {
                return .{
                    .set = set,
                    .child = borrow(child),
                    .depth = if (child) |c| c.depth() + 1 else 0,
                };
            }

            fn initCapacity(gpa: std.mem.Allocator, values: []const KV, size: u32, child: ?Self) !Map {
                const set = try SparseSet.init(gpa, &[0]SparseSet.Item{}, size);

                var map = Map.init(set, child);
                errdefer map.deinit(gpa);

                for (values) |v|
                    try map.insertMut(gpa, v.key, v.value);

                return map;
            }

            fn grow(parent_map: *Map, gpa: std.mem.Allocator, key: K, value: ?V) anyerror!Map {
                const size = capacityForSize(@intCast(parent_map.physicalCount()));
                var map = try Map.initCapacity(gpa, &[0]KV{}, size, null);

                errdefer map.deinit(gpa);

                if (value) |v|
                    try map.insertMut(gpa, key, v);

                var itr = Iterator.initNoBorrow(parent_map.*);
                while (itr.next()) |kv|
                    try map.insertMut(gpa, kv.key, kv.value);

                return map;
            }

            fn insert(map: *Map, gpa: std.mem.Allocator, key: K, value: ?V, index: usize) !Map {
                if (map.set.len() * 100 / map.set.capacity() >= max_load_percentage) {
                    return map.grow(gpa, key, value);
                }

                const new_set = try map.set.insert(
                    gpa,
                    SparseSet.Item{
                        .sparse_index = index,
                        .value = KV{
                            .key = key,
                            .value = value,
                        },
                    },
                );

                return Map.init(new_set, map.child);
            }

            fn insertMut(map: *Map, gpa: std.mem.Allocator, key: K, value: ?V) !void {
                switch (map.getHashed(key, ctx.hash(key), null, 0)) {
                    .empty => |index| try map.set.insertMut(gpa, .{
                        .sparse_index = index,
                        .value = .{ .key = key, .value = value },
                    }),
                    .item => {},
                    .full => return error.FullMap,
                }
            }

            fn putIfNotExists(map: *Map, gpa: std.mem.Allocator, key: K, value: ?V) !Map {
                const hash = ctx.hash(key);
                return switch (map.getHashed(key, hash, null, 0)) {
                    .empty => |index| map.insert(gpa, key, value, index),
                    .item => Map.init(map.set.borrow(), map.child),
                    .full => error.FullMap,
                };
            }

            fn update(map: *Map, gpa: std.mem.Allocator, key: K, value: ?V, index: usize) !Map {
                const new_set = try map.set.update(gpa, .{
                    .sparse_index = index,
                    .value = .{ .key = key, .value = value },
                });
                return Map.init(new_set, map.child);
            }

            fn getHashed(map: Map, key: K, hash: u32, child: ?Self, curr_depth: u8) GetResult {
                const set = map.set;
                const set_capacity = set.capacity();
                var i = hash % set_capacity;
                const start = i;
                var found_tomb = false;
                const local: GetResult = while (set.get(i)) |item| {
                    if (ctx.eql(item.value.key, key)) {
                        if (item.value.value) |_|
                            break .{ .item = .{ .item = item, .depth = curr_depth } }
                        else
                            found_tomb = true;
                    }
                    i = (i + 1) % set_capacity;
                    if (start == i) break .full;
                } else .{ .empty = i };

                return switch (local) {
                    .item => local,
                    inline else => if (found_tomb)
                        local
                    else if (child) |c| blk: {
                        const c_map = c.map.getUnwrap();
                        break :blk c_map.getHashed(key, hash, c_map.child, curr_depth + 1);
                    } else local,
                };
            }

            pub fn get(map: Map, key: K) ?KV {
                const hash = ctx.hash(key);
                return switch (map.getHashed(key, hash, map.child, 0)) {
                    .item => |item| item.item.value,
                    .empty, .full => null,
                };
            }

            pub fn deinit(self: *Map, gpa: std.mem.Allocator) void {
                self.set.deinit(gpa);
                if (self.child) |*c| c.deinit(gpa);
            }

            pub fn physicalCount(self: Map) usize {
                return self.set.dense.count() + if (self.child) |c| c.physicalCount() else 0;
            }
        };

        pub fn borrow(self: ?Self) ?Self {
            return if (self) |s| .{ .map = s.map.borrow() catch unreachable } else null;
        }

        fn capacityForSize(size: u32) u32 {
            var new_cap: u32 = @intCast((@as(u64, size) * 100) / max_load_percentage + 1);
            new_cap = std.math.ceilPowerOfTwo(u32, new_cap) catch unreachable;
            return @max(new_cap, 8);
        }

        fn initWithChild(gpa: std.mem.Allocator, values: []const KV, child: ?Self) !Self {
            const size = capacityForSize(@intCast(values.len));
            var map = try Map.initCapacity(gpa, values, size, child);
            errdefer map.deinit(gpa);

            return .{
                .map = try RC(Map).init(gpa, map),
            };
        }

        pub fn init(gpa: std.mem.Allocator, values: []const KV) !Self {
            return initWithChild(gpa, values, null);
        }

        pub fn deinit(self: *Self, gpa: std.mem.Allocator) void {
            self.map.deinit(gpa);
        }

        pub fn get(self: Self, key: K) ?KV {
            const map = self.map.getUnwrap();
            return map.get(key);
        }

        pub fn update(self: *Self, gpa: std.mem.Allocator, key: K, value: ?V, index: usize) !Map {
            var map = self.map.getUnwrap();
            return if (map.physicalCount() <= max_copy_size)
                map.update(gpa, key, value, index)
            else if (map.depth >= max_depth)
                map.grow(gpa, key, value)
            else
                Map.initCapacity(
                    gpa,
                    &[1]KV{.{ .key = key, .value = value }},
                    capacityForSize(1),
                    self.*,
                );
        }

        pub fn put(self: *Self, gpa: std.mem.Allocator, key: K, value: V) !Self {
            var map = self.map.getUnwrap();
            const hash = ctx.hash(key);
            var new_map = try switch (map.getHashed(key, hash, null, 0)) {
                .empty => |index| map.insert(gpa, key, value, index) catch |err| switch (err) {
                    error.NoSpaceOnDense => if (map.depth >= max_depth or map.physicalCount() <= max_copy_size)
                        map.grow(gpa, key, value)
                    else
                        Map.initCapacity(gpa, &[1]KV{.{ .key = key, .value = value }}, capacityForSize(1), self.*),
                    else => return err,
                },
                .item => |item| self.update(gpa, key, value, item.item.sparse_index),
                .full => map.grow(gpa, key, value),
            };
            errdefer new_map.deinit(gpa);

            return .{ .map = try RC(Map).init(gpa, new_map) };
        }

        pub fn delete(self: *Self, gpa: std.mem.Allocator, key: K) !Self {
            var map = self.map.getUnwrap();
            const hash = ctx.hash(key);
            var new_map = try switch (map.getHashed(key, hash, map.child, 0)) {
                .empty, .full => Map.init(map.set.borrow(), map.child),
                .item => |item| self.update(gpa, key, null, item.item.sparse_index),
            };
            errdefer new_map.deinit(gpa);

            return .{ .map = try RC(Map).init(gpa, new_map) };
        }

        pub fn physicalCount(self: Self) usize {
            return self.map.getUnwrap().physicalCount();
        }

        pub fn count(self: Self) usize {
            var itr = Iterator.initNoBorrow(self.map.getUnwrap());
            var i: usize = 0;
            while (itr.next()) |_| : (i += 1) {}
            return i;
        }

        pub fn depth(self: Self) u8 {
            return self.map.getUnwrap().depth;
        }

        pub fn iter(self: *Self) Iterator {
            return Iterator.init(self);
        }

        pub fn iterNoBorrow(self: Self) Iterator {
            return Iterator.initNoBorrow(self);
        }

        const Iterator = struct {
            parent: Map,
            map: ?Map,
            index: usize,
            depth: u8,

            pub fn init(map: *Map) Iterator {
                return .{
                    .parent = borrow(map).?,
                    .map = map,
                    .index = 0,
                    .depth = 0,
                };
            }

            pub fn deinit(iterator: Iterator, gpa: std.mem.Allocator) void {
                iterator.parent.deinit(gpa);
            }

            pub fn initNoBorrow(map: Map) Iterator {
                return .{
                    .parent = map,
                    .map = map,
                    .index = 0,
                    .depth = 0,
                };
            }

            pub fn next(iterator: *Iterator) ?KV {
                const map = iterator.map orelse return null;
                const dense = map.set.dense;
                if (iterator.index >= dense.list.getUnwrap().len) {
                    iterator.map = if (map.child) |c| c.map.getUnwrap() else null;
                    iterator.index = 0;
                    iterator.depth += 1;
                    return iterator.next();
                }

                const kv = dense.get(iterator.index).?.value;
                iterator.index += 1;
                const hash = ctx.hash(kv.key);
                const item = switch (iterator.parent.getHashed(kv.key, hash, iterator.parent.child, 0)) {
                    .item => |item| item,
                    else => return iterator.next(),
                };
                return if (item.depth == iterator.depth)
                    item.item.value
                else
                    iterator.next();
            }
        };

        /// A Sparse for private use inside the immutable hashmap
        const SparseSet = struct {
            dense: List(SparseSet.Item),
            sparse: Sparse,

            const Sparse = RC(std.ArrayList(?usize));

            pub const Item = struct {
                sparse_index: usize,
                value: KV,
            };

            pub fn init(gpa: std.mem.Allocator, values: []const SparseSet.Item, size: usize) !SparseSet {
                var dense = try List(SparseSet.Item).init(gpa, values);
                errdefer dense.deinit(gpa);

                var sparse_arr = try std.ArrayList(?usize).initCapacity(gpa, size);
                errdefer sparse_arr.deinit(gpa);

                sparse_arr.appendNTimesAssumeCapacity(null, size);
                for (values, 0..) |v, i|
                    sparse_arr.items[v.sparse_index] = i;

                return .{ .sparse = try Sparse.init(gpa, sparse_arr), .dense = dense };
            }

            pub fn deinit(self: *SparseSet, gpa: std.mem.Allocator) void {
                self.dense.deinit(gpa);
                self.sparse.deinit(gpa);
            }

            pub fn len(self: SparseSet) usize {
                return self.dense.count();
            }

            pub fn capacity(self: SparseSet) usize {
                return self.sparse.getUnwrap().items.len;
            }

            fn denseGet(self: SparseSet, index: usize) ?SparseSet.Item {
                const c = self.dense.count();
                if (index >= c) return null;
                return self.dense.get(c - index - 1);
            }

            pub fn get(self: SparseSet, index: usize) ?SparseSet.Item {
                const i = self.sparse.getUnwrap().items[index] orelse return null;
                const item = self.denseGet(i) orelse return null;
                return if (item.sparse_index == index) item else null;
            }

            pub fn insert(self: *SparseSet, gpa: std.mem.Allocator, item: SparseSet.Item) !SparseSet {
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

            fn insertMut(self: *SparseSet, gpa: std.mem.Allocator, item: SparseSet.Item) !void {
                self.sparse.getPtrUnwrap().items[item.sparse_index] = self.dense.count();
                try self.dense.appendMut(gpa, item);
            }

            pub fn update(self: *SparseSet, gpa: std.mem.Allocator, item: SparseSet.Item) !SparseSet {
                const tail_offset = self.sparse.getUnwrap().items[item.sparse_index].?;

                const new_bucket = try gpa.dupe(SparseSet.Item, self.dense.list.getPtrUnwrap().bucket.getPtrUnwrap().items);
                new_bucket[tail_offset] = item;

                const new_dense = try List(SparseSet.Item).initOwned(gpa, new_bucket);
                const new_sparse = try self.sparse.borrow();
                return .{ .sparse = new_sparse, .dense = new_dense };
            }

            pub fn borrow(self: *SparseSet) SparseSet {
                return .{
                    .dense = self.dense.borrow().?,
                    .sparse = self.sparse.borrow() catch unreachable,
                };
            }
        };
    };
}

test {
    _ = @import("hashmap_tests.zig");
}

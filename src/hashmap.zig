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

        pub const KV = struct {
            key: K,
            value: V,
        };

        const GetResult = union(enum) {
            item: SparseSet.Item,
            empty: usize,
            full,
        };

        pub const Map = struct {
            set: SparseSet,
            child: ?Self,

            fn grow(parent_map: *Map, gpa: std.mem.Allocator, key: K, value: V) anyerror!Map {
                const size = capacityForSize(@intCast(parent_map.count()));
                var set = try SparseSet.init(gpa, &[0]SparseSet.Item{}, size);
                errdefer set.deinit(gpa);

                var map = Map{
                    .set = set,
                    .child = null,
                };
                defer map.deinit(gpa);

                var current_map: ?*Map = parent_map;
                while (current_map) |c_map| {
                    var iterator = c_map.set.dense.iterNoBorrow();
                    while (iterator.next()) |item| : (current_map = if (c_map.child) |c| c.map.getPtrUnwrap() else null) {
                        const new_map = try map.putIfNotExists(gpa, item.value.key, item.value.value);
                        map.deinit(gpa);
                        map = new_map;
                    }
                }
                const new_map = map.put(gpa, key, value);
                return new_map;
            }

            fn insert(map: *Map, gpa: std.mem.Allocator, key: K, value: V, index: usize) !Map {
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

                return .{
                    .set = new_set,
                    .child = borrow(map.child),
                };
            }

            fn put(map: *Map, gpa: std.mem.Allocator, key: K, value: V) !Map {
                const hash = ctx.hash(key);
                return switch (map.getHashed(key, hash, null)) {
                    .empty => |index| map.insert(gpa, key, value, index),
                    .item => @panic("TODO! update"),
                    .full => map.grow(gpa, key, value),
                };
            }

            fn putIfNotExists(map: *Map, gpa: std.mem.Allocator, key: K, value: V) !Map {
                const hash = ctx.hash(key);
                return switch (map.getHashed(key, hash, null)) {
                    .empty => |index| map.insert(gpa, key, value, index),
                    .item => .{ .set = map.set, .child = borrow(map.child) },
                    .full => error.FullMap,
                };
            }

            fn getHashed(map: Map, key: K, hash: u32, child: ?Self) GetResult {
                const set = map.set;
                const set_capacity = set.capacity();
                var i = hash % set_capacity;
                const start = i;
                const local: GetResult = while (set.get(i)) |item| {
                    if (ctx.eql(item.value.key, key)) break .{ .item = item };
                    i = (i + 1) % set_capacity;
                    if (start == i) break .full;
                } else .{ .empty = i };

                return switch (local) {
                    .item => local,
                    inline else => if (child) |c| blk: {
                        const c_map = c.map.getUnwrap();
                        break :blk c_map.getHashed(key, hash, c_map.child);
                    } else local,
                };
            }

            pub fn get(map: Map, key: K) ?KV {
                const hash = ctx.hash(key);
                return switch (map.getHashed(key, hash, map.child)) {
                    .item => |item| item.value,
                    .empty, .full => null,
                };
            }

            pub fn deinit(self: *Map, gpa: std.mem.Allocator) void {
                self.set.deinit(gpa);
                if (self.child) |*c| c.deinit(gpa);
            }

            pub fn count(self: Map) usize {
                return self.set.dense.count() + if (self.child) |c| c.count() else 0;
            }
        };

        fn borrow(self: ?Self) ?Self {
            return if (self) |s| .{ .map = s.map.borrow() catch unreachable } else null;
        }

        fn capacityForSize(size: u32) u32 {
            var new_cap: u32 = @intCast((@as(u64, size) * 100) / max_load_percentage + 1);
            new_cap = std.math.ceilPowerOfTwo(u32, new_cap) catch unreachable;
            return new_cap;
        }

        fn initWithChild(gpa: std.mem.Allocator, values: []const KV, child: ?Self) !Self {
            const size = capacityForSize(@intCast(values.len));
            var set = try SparseSet.init(gpa, &[0]SparseSet.Item{}, size);
            errdefer set.deinit(gpa);

            var map = Map{
                .set = set,
                .child = child,
            };
            errdefer map.deinit(gpa);

            for (values) |v| {
                const new_map = try map.put(gpa, v.key, v.value);
                map.deinit(gpa);
                map = new_map;
            }

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

        pub fn put(self: *Self, gpa: std.mem.Allocator, key: K, value: V) !Self {
            var map = self.map.getUnwrap();
            var new_map = try map.put(gpa, key, value);
            errdefer new_map.deinit(gpa);

            return .{ .map = try RC(Map).init(gpa, new_map) };
        }

        pub fn count(self: Self) usize {
            return self.map.getUnwrap().count();
        }

        /// A Sparse for private use inside the immutable hashmap
        const SparseSet = struct {
            dense: List(Item),
            sparse: Sparse,

            const Sparse = RC(std.ArrayList(?usize));

            pub const Item = struct {
                sparse_index: usize,
                value: KV,
            };

            pub fn init(gpa: std.mem.Allocator, values: []const Item, size: usize) !SparseSet {
                var dense = try List(Item).init(gpa, values);
                errdefer dense.deinit(gpa);

                var sparse_arr = try std.ArrayList(?usize).initCapacity(gpa, size);
                sparse_arr.appendNTimesAssumeCapacity(null, size);
                for (values, 0..) |v, i|
                    sparse_arr.items[v.sparse_index] = i;

                errdefer sparse_arr.deinit(gpa);

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

            fn denseGet(self: SparseSet, index: usize) ?Item {
                const c = self.dense.count();
                if (index >= c) return null;
                return self.dense.get(c - index - 1);
            }

            pub fn get(self: SparseSet, index: usize) ?Item {
                const i = self.sparse.getUnwrap().items[index] orelse return null;
                const item = self.denseGet(i) orelse return null;
                return if (item.sparse_index == index) item else null;
            }

            pub fn insert(self: *SparseSet, gpa: std.mem.Allocator, item: Item) !SparseSet {
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

            pub fn borrow(self: *SparseSet) SparseSet {
                return .{
                    .dense = self.dense.borrow(),
                    .sparse = self.sparse.borrow() catch unreachable,
                };
            }
        };
    };
}

fn strEql(a: []const u8, b: []const u8) bool {
    return std.mem.eql(u8, a, b);
}

fn strHash(a: []const u8) u32 {
    var h: u32 = 2166136261;
    for (a) |c| {
        h ^= c;
        h *%= 16777619;
    }
    return h;
}

test "Create" {
    const K = []const u8;
    const V = f32;
    const MyHashCtx = Ctx(K){
        .eql = strEql,
        .hash = strHash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var map = try Map.init(gpa, &[_]Map.KV{
        .{ .key = "key", .value = 0 },
    });
    defer map.deinit(gpa);

    const vs = &[_]Map.KV{
        .{ .key = "hello", .value = 1 },
        .{ .key = "world", .value = 2 },
        .{ .key = "man", .value = 3 },
        .{ .key = "man 2", .value = 4 },
        .{ .key = "man 3", .value = 5 },
        .{ .key = "man 4", .value = 6 },
    };

    var new_map = map.borrow().?;
    defer new_map.deinit(gpa);
    for (vs) |v| {
        const m_new_map = new_map.put(gpa, v.key, v.value) catch |err| {
            std.debug.print("key {s}: error {any}\n", .{ v.key, err });
            @panic("error");
        };
        new_map.deinit(gpa);
        new_map = m_new_map;
    }

    try std.testing.expectEqual(0, map.get("key").?.value);
    try std.testing.expectEqual(1, new_map.get("hello").?.value);
    try std.testing.expectEqual(2, new_map.get("world").?.value);
    try std.testing.expectEqual(null, map.get("man"));
    try std.testing.expectEqual(3, new_map.get("man").?.value);
}

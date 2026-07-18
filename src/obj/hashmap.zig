const std = @import("std");
const List = @import("list.zig").List;
const RC = @import("ref_counter.zig").RC;

fn Unwrap(comptime T: type) type {
    return switch (@typeInfo(T)) {
        .pointer => |info| info.child,
        else => T,
    };
}

fn borrowValue(v: anytype) @TypeOf(v) {
    if (comptime std.meta.hasFn(Unwrap(@TypeOf(v)), "borrow")) {
        var tmp = v;
        return tmp.borrow();
    }
    return v;
}

fn deinitValue(v: anytype, gpa: std.mem.Allocator) void {
    if (comptime std.meta.hasFn(Unwrap(std.meta.Child(@TypeOf(v))), "deinit")) v.deinit(gpa);
}

/// Hashing and equality context for key type `K`.
pub fn Ctx(comptime K: type) type {
    return struct {
        hash: *const fn (K) u32,
        eql: *const fn (K, K) bool,
    };
}

/// Persistent, immutable, reference-counted hash map. Built from layered `Map`s: an update
/// copies a small set, stacks a shadowing child layer, or flattens via `grow` once the chain
/// reaches `max_depth`. Reads walk layers; a newer layer (or a tombstone) shadows older ones.
pub fn HashMap(comptime K: type, comptime V: type, comptime ctx: Ctx(K)) type {
    return struct {
        map: RC(Map),

        const Self = @This();

        const max_load_percentage = 75;
        const max_copy_size = 32;
        const max_depth = 5;

        /// A key with its value; a null `value` marks a tombstone (deleted key).
        pub const KV = struct {
            key: K,
            value: ?V,

            pub fn borrow(self: *KV) KV {
                return .{
                    .key = borrowValue(self.key),
                    .value = if (self.value) |v| borrowValue(v) else null,
                };
            }

            pub fn deinit(self: *KV, gpa: std.mem.Allocator) void {
                deinitValue(&self.key, gpa);
                if (self.value) |*v| deinitValue(v, gpa);
            }
        };

        /// A located set item together with the layer depth it was found at.
        const Item = struct {
            item: SparseSet.Item,
            depth: u8,
        };

        /// Outcome of a probe: a live item, an empty insertion slot, or a full set.
        const GetResult = union(enum) {
            item: Item,
            empty: usize,
            full,
        };

        /// One layer of the map: a `set` plus an optional `child` layer it shadows.
        /// `depth` counts the child layers below this one (0 = no child).
        pub const Map = struct {
            set: SparseSet,
            child: ?Self,
            depth: u8,

            const GetRet = struct {
                kv: KV,
                depth: u8,
            };

            /// Build a layer from `set` and an optional `child` (borrowed); `depth` = child depth + 1.
            fn init(set: SparseSet, child: ?Self) Map {
                return .{
                    .set = set,
                    .child = borrow(child),
                    .depth = if (child) |c| c.depth() + 1 else 0,
                };
            }

            /// Build a layer whose set has capacity `size`, populated from `values` (deduped)
            /// via `insertMut`, over an optional `child`.
            fn initCapacity(gpa: std.mem.Allocator, values: []const KV, size: u32, child: ?Self) !Map {
                const set = try SparseSet.init(gpa, &[0]SparseSet.Item{}, size);

                var map = Map.init(set, child);
                errdefer map.deinit(gpa);

                for (values) |v|
                    try map.insertMut(gpa, v.key, v.value);

                return map;
            }

            /// Flatten `parent_map` plus the pending `key`/`value` into one new layer sized for
            /// its physical count. A null `value` deletes `key` (it is skipped while copying).
            fn grow(parent_map: *Map, gpa: std.mem.Allocator, key: K, value: ?V) anyerror!Map {
                const size = capacityForSize(@intCast(parent_map.physicalCount()));
                var map = try Map.initCapacity(gpa, &[0]KV{}, size, null);

                errdefer map.deinit(gpa);

                if (value) |v|
                    try map.insertMut(gpa, key, v);

                var itr = Iterator.initNoBorrow(parent_map.*);
                while (itr.next()) |kv| {
                    if (value == null and ctx.eql(kv.key, key)) continue;
                    try map.insertMut(gpa, kv.key, kv.value);
                }

                return map;
            }

            /// Return a new layer with `{key,value}` at probed slot `index`. Rehashes via `grow`
            /// once past `max_load_percentage`; `error.NoSpaceOnDense` if the dense can't grow.
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

            /// Insert `{key,value}` in place if `key` is absent (probe + skip existing); used to
            /// fill a freshly-owned set. `error.FullMap` if no slot is free.
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

            /// Return a new layer with the top-set entry at `index` replaced by `{key,value}`.
            fn update(map: *Map, gpa: std.mem.Allocator, key: K, value: ?V, index: usize) !Map {
                const new_set = try map.set.update(gpa, .{
                    .sparse_index = index,
                    .value = .{ .key = key, .value = value },
                });
                return Map.init(new_set, map.child);
            }

            /// Probe for `key` from `hash`, descending into `child` layers (incrementing
            /// `curr_depth`). Returns the live `.item` with the depth it was found at, the
            /// `.empty` insertion slot, or `.full`. A tombstone for `key` stops the descent
            /// (the key counts as deleted).
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

            /// Look up `key` across this layer and its children; null if absent or deleted.
            pub fn get(map: Map, key: K) ?KV {
                const hash = ctx.hash(key);
                return switch (map.getHashed(key, hash, map.child, 0)) {
                    .item => |item| item.item.value,
                    .empty, .full => null,
                };
            }

            /// Release this layer's set and recurse into the child.
            pub fn deinit(self: *Map, gpa: std.mem.Allocator) void {
                self.set.deinit(gpa);
                if (self.child) |*c| c.deinit(gpa);
            }

            /// Total stored entries across all layers, including tombstones and shadowed
            /// duplicates (>= the logical `count`).
            pub fn physicalCount(self: Map) usize {
                return self.set.dense.count() + if (self.child) |c| c.physicalCount() else 0;
            }
        };

        /// Clone an optional map reference (refcount++); null stays null.
        pub fn borrow(self: ?Self) ?Self {
            return if (self) |s| .{ .map = s.map.borrow() catch unreachable } else null;
        }

        /// Smallest power-of-two capacity (>= 8) that keeps `size` entries under `max_load_percentage`.
        fn capacityForSize(size: u32) u32 {
            var new_cap: u32 = @intCast((@as(u64, size) * 100) / max_load_percentage + 1);
            new_cap = std.math.ceilPowerOfTwo(u32, new_cap) catch unreachable;
            return @max(new_cap, 8);
        }

        /// Build a map from `values` over an optional `child` layer.
        fn initWithChild(gpa: std.mem.Allocator, values: []const KV, child: ?Self) !Self {
            const size = capacityForSize(@intCast(values.len));
            var map = try Map.initCapacity(gpa, values, size, child);
            errdefer map.deinit(gpa);

            return .{
                .map = try RC(Map).init(gpa, map),
            };
        }

        /// Create a map from `values`.
        pub fn init(gpa: std.mem.Allocator, values: []const KV) !Self {
            return initWithChild(gpa, values, null);
        }

        /// Drop this map reference.
        pub fn deinit(self: *Self, gpa: std.mem.Allocator) void {
            self.map.deinit(gpa);
        }

        /// Value for `key`, or null if absent/deleted.
        pub fn get(self: Self, key: K) ?KV {
            const map = self.map.getUnwrap();
            return map.get(key);
        }

        /// Apply `{key,value}` to the existing entry at `index`: in place (small map), as a new
        /// shadowing layer (medium), or by flattening (`grow`) at `max_depth`.
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

        /// Return a new map with `key` set to `value` (inserts if absent, updates otherwise).
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

        /// Return a new map with `key` removed (via a tombstone, or flattened away at `max_depth`).
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

        /// Total stored entries incl. tombstones and shadowed duplicates (>= `count`).
        pub fn physicalCount(self: Self) usize {
            return self.map.getUnwrap().physicalCount();
        }

        /// Number of live, unique keys, deduped across layers (computed by iterating).
        pub fn count(self: Self) usize {
            var itr = Iterator.initNoBorrow(self.map.getUnwrap());
            var i: usize = 0;
            while (itr.next()) |_| : (i += 1) {}
            return i;
        }

        /// Number of child layers below this map.
        pub fn depth(self: Self) u8 {
            return self.map.getUnwrap().depth;
        }

        /// Borrowing iterator over the live key/value pairs; release with `Iterator.deinit`.
        pub fn iter(self: *Self) Iterator {
            return Iterator.init(self);
        }

        /// Non-borrowing iterator over the live key/value pairs; valid while the map is alive.
        pub fn iterNoBorrow(self: Self) Iterator {
            return Iterator.initNoBorrow(self.map.getUnwrap());
        }

        /// Iterator over the map values.
        const Iterator = union(enum) {
            flat: FlatIterator,
            depth: DepthIterator,

            /// Borrowing iterator over `map`; release with `deinit`.
            pub fn init(self: *Self) Iterator {
                const root = borrow(self.*).?;
                const map = root.map.getUnwrap();
                return if (map.depth == 0)
                    .{ .flat = FlatIterator.init(root, map) }
                else
                    .{ .depth = DepthIterator.init(root, map) };
            }

            /// Release the borrowed map reference.
            pub fn deinit(itr: *Iterator, gpa: std.mem.Allocator) void {
                switch (itr.*) {
                    inline else => |*i| i.deinit(gpa),
                }
            }

            /// Iterator over `map` without taking a reference (do not call `deinit`).
            pub fn initNoBorrow(map: Map) Iterator {
                return if (map.depth == 0) .{ .flat = FlatIterator.initNoBorrow(map) } else .{ .depth = DepthIterator.initNoBorrow(map) };
            }

            /// Next live `KV`, or null when exhausted.
            pub fn next(itr: *Iterator) ?KV {
                return switch (itr.*) {
                    inline else => |*i| i.next(),
                };
            }
        };

        /// A special iterator for maps with depth 0.
        const FlatIterator = struct {
            root: ?Self,
            map: Map,
            index: usize,

            pub fn init(root: Self, map: Map) FlatIterator {
                return .{
                    .root = root,
                    .map = map,
                    .index = 0,
                };
            }

            pub fn deinit(iterator: *FlatIterator, gpa: std.mem.Allocator) void {
                if (iterator.root) |*r| r.deinit(gpa);
            }

            pub fn initNoBorrow(map: Map) FlatIterator {
                return .{
                    .root = null,
                    .map = map,
                    .index = 0,
                };
            }

            pub fn next(itr: *FlatIterator) ?KV {
                const dense = itr.map.set.dense;
                const item = dense.get(itr.index) orelse return null;
                var kv = item.value;
                itr.index += 1;
                if (kv.value == null) return itr.next();
                return if (itr.root != null) kv.borrow() else kv;
            }
        };

        /// Iterates the live, unique key/value pairs across all layers: the topmost non-null
        /// value for a key wins, and tombstones / shadowed entries are skipped.
        const DepthIterator = struct {
            root: ?Self,
            parent: Map,
            map: ?Map,
            index: usize,
            depth: u8,

            pub fn init(root: Self, map: Map) DepthIterator {
                return .{
                    .root = root,
                    .parent = map,
                    .map = map,
                    .index = 0,
                    .depth = 0,
                };
            }

            pub fn deinit(iterator: *DepthIterator, gpa: std.mem.Allocator) void {
                if (iterator.root) |*r| r.deinit(gpa);
            }

            pub fn initNoBorrow(map: Map) DepthIterator {
                return .{
                    .root = null,
                    .parent = map,
                    .map = map,
                    .index = 0,
                    .depth = 0,
                };
            }

            pub fn next(iterator: *DepthIterator) ?KV {
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
                if (item.depth != iterator.depth) return iterator.next();
                var out = item.item.value;
                return if (iterator.root != null) out.borrow() else out;
            }
        };

        /// Sparse-set hash table for one layer: `sparse` maps a hash slot to a dense index,
        /// and `dense` is a persistent list of items in insertion order.
        const SparseSet = struct {
            dense: List(SparseSet.Item),
            sparse: Sparse,

            const Sparse = RC(std.ArrayList(?usize));

            pub const Item = struct {
                sparse_index: usize,
                value: KV,

                pub fn borrow(self: *SparseSet.Item) SparseSet.Item {
                    return .{
                        .sparse_index = self.sparse_index,
                        .value = self.value.borrow(),
                    };
                }

                pub fn deinit(self: *SparseSet.Item, gpa: std.mem.Allocator) void {
                    self.value.deinit(gpa);
                }
            };

            /// Build a set with `size` sparse slots, populated from `values`.
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

            /// Release the dense list and the sparse array.
            pub fn deinit(self: *SparseSet, gpa: std.mem.Allocator) void {
                self.dense.deinit(gpa);
                self.sparse.deinit(gpa);
            }

            /// Number of dense entries (includes tombstones).
            pub fn len(self: SparseSet) usize {
                return self.dense.count();
            }

            /// Number of sparse slots (the table capacity).
            pub fn capacity(self: SparseSet) usize {
                return self.sparse.getUnwrap().items.len;
            }

            /// Item at dense position `index` (insertion order), or null if out of range.
            fn denseGet(self: SparseSet, index: usize) ?SparseSet.Item {
                const c = self.dense.count();
                if (index >= c) return null;
                return self.dense.get(c - index - 1);
            }

            /// Item occupying sparse slot `index`, or null if the slot is empty or stale.
            pub fn get(self: SparseSet, index: usize) ?SparseSet.Item {
                const i = self.sparse.getUnwrap().items[index] orelse return null;
                const item = self.denseGet(i) orelse return null;
                return if (item.sparse_index == index) item else null;
            }

            /// Persistent insert at `item.sparse_index`. `error.NoSpaceOnDense` if the dense
            /// head can't grow in place (caller rehashes or layers).
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

            /// In-place insert at `item.sparse_index`; only valid on a freshly-owned set.
            fn insertMut(self: *SparseSet, gpa: std.mem.Allocator, item: SparseSet.Item) !void {
                self.sparse.getPtrUnwrap().items[item.sparse_index] = self.dense.count();
                try self.dense.appendMut(gpa, item);
            }

            /// Persistent replace of the entry at `item.sparse_index` (copies the dense bucket).
            pub fn update(self: *SparseSet, gpa: std.mem.Allocator, item: SparseSet.Item) !SparseSet {
                const tail_offset = self.sparse.getUnwrap().items[item.sparse_index].?;

                const new_bucket = try gpa.alloc(SparseSet.Item, self.dense.count());
                var iterator = self.dense.iterNoBorrow();
                var i = new_bucket.len;
                while (iterator.next()) |v| {
                    i -= 1;
                    const val = if (i == tail_offset) item else v;
                    new_bucket[i] = borrowValue(val);
                }

                const new_dense = try List(SparseSet.Item).initOwned(gpa, new_bucket);
                const new_sparse = try self.sparse.borrow();
                return .{ .sparse = new_sparse, .dense = new_dense };
            }

            /// Clone the set, borrowing its dense and sparse references.
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

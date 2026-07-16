const std = @import("std");
const Ctx = @import("hashmap.zig").Ctx;
const HashMap = @import("hashmap.zig").HashMap;

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
        const m_new_map = new_map.put(gpa, v.key, v.value.?) catch |err| {
            std.debug.print("key {s}: error {any}\n", .{ v.key, err });
            @panic("error");
        };
        new_map.deinit(gpa);
        new_map = m_new_map;
    }

    try std.testing.expectEqual(0, map.get("key").?.value.?);
    try std.testing.expectEqual(1, new_map.get("hello").?.value.?);
    try std.testing.expectEqual(2, new_map.get("world").?.value.?);
    try std.testing.expectEqual(null, map.get("man"));
    try std.testing.expectEqual(3, new_map.get("man").?.value.?);
}

test "Update" {
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
        const m_new_map = new_map.put(gpa, v.key, v.value.?) catch |err| {
            std.debug.print("key {s}: error {any}\n", .{ v.key, err });
            @panic("error");
        };
        new_map.deinit(gpa);
        new_map = m_new_map;
    }

    const upvs = &[_]Map.KV{
        .{ .key = "hello", .value = 5 },
        .{ .key = "world", .value = 6 },
        .{ .key = "man", .value = 7 },
        .{ .key = "man 2", .value = 8 },
        .{ .key = "man 3", .value = 9 },
        .{ .key = "man 4", .value = 10 },
    };

    var upd_map = new_map.borrow().?;
    defer upd_map.deinit(gpa);
    for (upvs) |v| {
        const m_new_map = upd_map.put(gpa, v.key, v.value.?) catch |err| {
            std.debug.print("key {s}: error {any}\n", .{ v.key, err });
            @panic("error");
        };
        upd_map.deinit(gpa);
        upd_map = m_new_map;
    }

    try std.testing.expectEqual(0, map.get("key").?.value.?);
    try std.testing.expectEqual(1, new_map.get("hello").?.value.?);
    try std.testing.expectEqual(2, new_map.get("world").?.value.?);
    try std.testing.expectEqual(null, map.get("man"));
    try std.testing.expectEqual(3, new_map.get("man").?.value.?);

    try std.testing.expectEqual(0, map.get("key").?.value.?);
    try std.testing.expectEqual(5, upd_map.get("hello").?.value.?);
    try std.testing.expectEqual(6, upd_map.get("world").?.value.?);
    try std.testing.expectEqual(null, map.get("man"));
    try std.testing.expectEqual(7, upd_map.get("man").?.value.?);
}

fn u32Eql(a: u32, b: u32) bool {
    return a == b;
}

fn u32Hash(a: u32) u32 {
    return a;
}

test "Empty" {
    const K = []const u8;
    const V = f32;
    const MyHashCtx = Ctx(K){
        .eql = strEql,
        .hash = strHash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var map = try Map.init(gpa, &[_]Map.KV{});
    defer map.deinit(gpa);

    var new_map = try map.put(gpa, "hello", 1);
    defer new_map.deinit(gpa);

    try std.testing.expectEqual(null, map.get("hello"));
    try std.testing.expectEqual(1, new_map.get("hello").?.value.?);
    try std.testing.expectEqual(null, new_map.get("missing"));
}

test "large update" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var kvs: [40]Map.KV = undefined;
    for (&kvs, 0..) |*kv, i| kv.* = .{ .key = @intCast(i), .value = @intCast(i) };

    var map = try Map.init(gpa, &kvs);
    defer map.deinit(gpa);

    var updated = try map.put(gpa, 0, 999);
    defer updated.deinit(gpa);

    try std.testing.expectEqual(0, map.get(0).?.value.?);
    try std.testing.expectEqual(39, map.get(39).?.value.?);
    try std.testing.expectEqual(999, updated.get(0).?.value.?);
    try std.testing.expectEqual(39, updated.get(39).?.value.?);
    try std.testing.expectEqual(null, updated.get(100));
}

test "allocation failures" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);

    var fail_index: usize = 0;
    while (true) : (fail_index += 1) {
        var fa = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = fail_index });
        const gpa = fa.allocator();

        const completed = blk: {
            var small = Map.init(gpa, &[_]Map.KV{
                .{ .key = 1, .value = 1 },
                .{ .key = 2, .value = 2 },
            }) catch break :blk false;
            defer small.deinit(gpa);

            var grown = small.borrow().?;
            defer grown.deinit(gpa);
            for (3..9) |k| {
                const next = grown.put(gpa, @intCast(k), @intCast(k)) catch break :blk false;
                grown.deinit(gpa);
                grown = next;
            }

            var small_upd = grown.put(gpa, 1, 99) catch break :blk false;
            defer small_upd.deinit(gpa);

            var large_kvs: [40]Map.KV = undefined;
            for (&large_kvs, 0..) |*kv, i| kv.* = .{ .key = @intCast(i), .value = @intCast(i) };

            var large = Map.init(gpa, &large_kvs) catch break :blk false;
            defer large.deinit(gpa);

            var large_upd = large.put(gpa, 0, 99) catch break :blk false;
            defer large_upd.deinit(gpa);

            var empty = Map.init(gpa, &[_]Map.KV{}) catch break :blk false;
            defer empty.deinit(gpa);

            var filled = empty.put(gpa, 7, 7) catch break :blk false;
            defer filled.deinit(gpa);

            break :blk true;
        };

        if (completed) break;
    }
}

test "deep chain flatten" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var kvs: [40]Map.KV = undefined;
    for (&kvs, 0..) |*kv, i| kv.* = .{ .key = @intCast(i), .value = @intCast(i) };

    var map = try Map.init(gpa, &kvs);
    defer map.deinit(gpa);

    try std.testing.expectEqual(0, map.depth());

    const expected_depths = [_]u8{ 1, 2, 3, 4, 5, 0, 1, 2, 3 };

    var current = map.borrow().?;
    defer current.deinit(gpa);

    for (expected_depths, 0..) |expected, n| {
        const v: u32 = @intCast(1000 + n);
        const next = try current.put(gpa, 0, v);
        current.deinit(gpa);
        current = next;

        try std.testing.expectEqual(expected, current.depth());
        try std.testing.expectEqual(v, current.get(0).?.value.?);
        try std.testing.expectEqual(39, current.get(39).?.value.?);
    }
}

test "sibling insert fork" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var parent = try Map.init(gpa, &[_]Map.KV{
        .{ .key = 1, .value = 1 },
        .{ .key = 2, .value = 2 },
    });
    defer parent.deinit(gpa);

    var a = try parent.put(gpa, 10, 100);
    defer a.deinit(gpa);

    var b = try parent.put(gpa, 20, 200);
    defer b.deinit(gpa);

    try std.testing.expectEqual(0, a.depth());
    try std.testing.expectEqual(0, b.depth());

    try std.testing.expectEqual(100, a.get(10).?.value.?);
    try std.testing.expectEqual(1, a.get(1).?.value.?);
    try std.testing.expectEqual(null, a.get(20));

    try std.testing.expectEqual(200, b.get(20).?.value.?);
    try std.testing.expectEqual(2, b.get(2).?.value.?);
    try std.testing.expectEqual(null, b.get(10));
}

test "delete" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var map = try Map.init(gpa, &[_]Map.KV{
        .{ .key = 1, .value = 1 },
        .{ .key = 2, .value = 2 },
        .{ .key = 3, .value = 3 },
    });
    defer map.deinit(gpa);

    var deleted = try map.delete(gpa, 2);
    defer deleted.deinit(gpa);

    var missing = try map.delete(gpa, 99);
    defer missing.deinit(gpa);

    try std.testing.expectEqual(null, deleted.get(2));
    try std.testing.expectEqual(1, deleted.get(1).?.value.?);
    try std.testing.expectEqual(3, deleted.get(3).?.value.?);
    try std.testing.expectEqual(2, map.get(2).?.value.?);
    try std.testing.expectEqual(1, missing.get(1).?.value.?);
}

test "delete from child layer" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var kvs: [40]Map.KV = undefined;
    for (&kvs, 0..) |*kv, i| kv.* = .{ .key = @intCast(i), .value = @intCast(i) };

    var big = try Map.init(gpa, &kvs);
    defer big.deinit(gpa);

    var chained = try big.put(gpa, 0, 999);
    defer chained.deinit(gpa);

    var d = try chained.delete(gpa, 1);
    defer d.deinit(gpa);

    try std.testing.expectEqual(null, d.get(1));
    try std.testing.expectEqual(2, d.get(2).?.value.?);
    try std.testing.expectEqual(999, d.get(0).?.value.?);
}

test "count semantics" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var kvs: [40]Map.KV = undefined;
    for (&kvs, 0..) |*kv, i| kv.* = .{ .key = @intCast(i), .value = @intCast(i) };

    var big = try Map.init(gpa, &kvs);
    defer big.deinit(gpa);

    var chained = try big.put(gpa, 0, 999);
    defer chained.deinit(gpa);

    var del = try chained.delete(gpa, 1);
    defer del.deinit(gpa);

    try std.testing.expectEqual(40, big.count());
    try std.testing.expectEqual(40, chained.count());
    try std.testing.expectEqual(39, del.count());
    try std.testing.expect(del.physicalCount() > del.count());
}

test "count flat" {
    const K = u32;
    const V = u32;
    const MyHashCtx = Ctx(K){
        .eql = u32Eql,
        .hash = u32Hash,
    };

    const Map = HashMap(K, V, MyHashCtx);
    const gpa = std.testing.allocator;

    var m0 = try Map.init(gpa, &[_]Map.KV{});
    defer m0.deinit(gpa);

    var m1 = try m0.put(gpa, 1, 10);
    defer m1.deinit(gpa);

    var m2 = try m1.put(gpa, 2, 20);
    defer m2.deinit(gpa);

    var m3 = try m2.put(gpa, 1, 99);
    defer m3.deinit(gpa);

    var m4 = try m3.delete(gpa, 1);
    defer m4.deinit(gpa);

    var m5 = try m4.delete(gpa, 12345);
    defer m5.deinit(gpa);

    try std.testing.expectEqual(0, m0.count());
    try std.testing.expectEqual(1, m1.count());
    try std.testing.expectEqual(2, m2.count());
    try std.testing.expectEqual(2, m3.count());
    try std.testing.expectEqual(1, m4.count());
    try std.testing.expectEqual(1, m5.count());
}

const RC = @import("ref_counter.zig").RC;

const Payload = struct {
    data: []u8,

    pub fn deinit(self: *Payload, gpa: std.mem.Allocator) void {
        gpa.free(self.data);
    }
};

const Counted = struct {
    rc: RC(Payload),

    fn init(gpa: std.mem.Allocator, byte: u8) !Counted {
        const data = try gpa.alloc(u8, 1);
        errdefer gpa.free(data);
        data[0] = byte;
        return .{ .rc = try RC(Payload).init(gpa, .{ .data = data }) };
    }

    fn value(self: Counted) u8 {
        return self.rc.getUnwrap().data[0];
    }

    pub fn deinit(self: *Counted, gpa: std.mem.Allocator) void {
        self.rc.deinit(gpa);
    }

    pub fn borrow(self: *Counted) Counted {
        return .{ .rc = self.rc.borrow() catch unreachable };
    }
};

const CountedCtx = Ctx(u32){ .eql = u32Eql, .hash = u32Hash };
const CountedMap = HashMap(u32, Counted, CountedCtx);

test "Ref counted init borrows values" {
    const gpa = std.testing.allocator;

    var v1 = try Counted.init(gpa, 1);
    var v2 = try Counted.init(gpa, 2);
    var map = try CountedMap.init(gpa, &[_]CountedMap.KV{
        .{ .key = 1, .value = v1 },
        .{ .key = 2, .value = v2 },
    });
    v1.deinit(gpa);
    v2.deinit(gpa);

    try std.testing.expectEqual(@as(u8, 1), map.get(1).?.value.?.value());
    try std.testing.expectEqual(@as(u8, 2), map.get(2).?.value.?.value());
    try std.testing.expect(map.get(3) == null);

    map.deinit(gpa);
}

test "Ref counted put borrows and overwrites" {
    const gpa = std.testing.allocator;

    var empty = try CountedMap.init(gpa, &[_]CountedMap.KV{});
    defer empty.deinit(gpa);

    var a = try Counted.init(gpa, 10);
    var m1 = try empty.put(gpa, 1, a);
    a.deinit(gpa);
    defer m1.deinit(gpa);

    var b = try Counted.init(gpa, 20);
    var m2 = try m1.put(gpa, 1, b);
    b.deinit(gpa);
    defer m2.deinit(gpa);

    try std.testing.expectEqual(@as(u8, 10), m1.get(1).?.value.?.value());
    try std.testing.expectEqual(@as(u8, 20), m2.get(1).?.value.?.value());
}

test "Ref counted delete" {
    const gpa = std.testing.allocator;

    var v1 = try Counted.init(gpa, 1);
    var v2 = try Counted.init(gpa, 2);
    var map = try CountedMap.init(gpa, &[_]CountedMap.KV{
        .{ .key = 1, .value = v1 },
        .{ .key = 2, .value = v2 },
    });
    v1.deinit(gpa);
    v2.deinit(gpa);
    defer map.deinit(gpa);

    var deleted = try map.delete(gpa, 1);
    defer deleted.deinit(gpa);

    try std.testing.expect(deleted.get(1) == null);
    try std.testing.expectEqual(@as(u8, 2), deleted.get(2).?.value.?.value());
    try std.testing.expectEqual(@as(u8, 1), map.get(1).?.value.?.value());
}

test "Ref counted grow and layering" {
    const gpa = std.testing.allocator;

    var map = try CountedMap.init(gpa, &[_]CountedMap.KV{});
    defer map.deinit(gpa);

    var i: u32 = 0;
    while (i < 40) : (i += 1) {
        var v = try Counted.init(gpa, @intCast(i));
        const next = try map.put(gpa, i, v);
        v.deinit(gpa);
        map.deinit(gpa);
        map = next;
    }

    i = 0;
    while (i < 40) : (i += 1)
        try std.testing.expectEqual(@as(u8, @intCast(i)), map.get(i).?.value.?.value());
}

test "Ref counted flat iterator borrows entries" {
    const gpa = std.testing.allocator;

    var v1 = try Counted.init(gpa, 1);
    var v2 = try Counted.init(gpa, 2);
    var map = try CountedMap.init(gpa, &[_]CountedMap.KV{
        .{ .key = 1, .value = v1 },
        .{ .key = 2, .value = v2 },
    });
    v1.deinit(gpa);
    v2.deinit(gpa);

    var iterator = map.iter();
    map.deinit(gpa);

    var seen: usize = 0;
    var sum: usize = 0;
    while (iterator.next()) |kv| : (seen += 1) {
        var e = kv;
        sum += e.value.?.value();
        e.deinit(gpa);
    }
    iterator.deinit(gpa);

    try std.testing.expectEqual(@as(usize, 2), seen);
    try std.testing.expectEqual(@as(usize, 3), sum);
}

test "Ref counted depth iterator borrows entries" {
    const gpa = std.testing.allocator;

    var map = try CountedMap.init(gpa, &[_]CountedMap.KV{});
    defer map.deinit(gpa);

    var i: u32 = 0;
    while (i < 40) : (i += 1) {
        var v = try Counted.init(gpa, @intCast(i));
        const next = try map.put(gpa, i, v);
        v.deinit(gpa);
        map.deinit(gpa);
        map = next;
    }

    var ov = try Counted.init(gpa, 200);
    var layered = try map.put(gpa, 5, ov);
    ov.deinit(gpa);

    try std.testing.expect(layered.depth() > 0);

    var iterator = layered.iter();
    layered.deinit(gpa);

    var seen: usize = 0;
    var found_overwritten = false;
    while (iterator.next()) |kv| : (seen += 1) {
        var e = kv;
        if (e.key == 5) found_overwritten = e.value.?.value() == 200;
        e.deinit(gpa);
    }
    iterator.deinit(gpa);

    try std.testing.expectEqual(@as(usize, 40), seen);
    try std.testing.expect(found_overwritten);
}

test "Ref counted allocation failures" {
    var fail_index: usize = 0;
    while (true) : (fail_index += 1) {
        var fa = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = fail_index });
        const gpa = fa.allocator();

        const completed = blk: {
            var v1 = Counted.init(gpa, 1) catch break :blk false;
            defer v1.deinit(gpa);
            var v2 = Counted.init(gpa, 2) catch break :blk false;
            defer v2.deinit(gpa);

            var m0 = CountedMap.init(gpa, &[_]CountedMap.KV{
                .{ .key = 1, .value = v1 },
                .{ .key = 2, .value = v2 },
            }) catch break :blk false;
            defer m0.deinit(gpa);

            var v3 = Counted.init(gpa, 3) catch break :blk false;
            defer v3.deinit(gpa);
            var m1 = m0.put(gpa, 3, v3) catch break :blk false;
            defer m1.deinit(gpa);

            var v4 = Counted.init(gpa, 4) catch break :blk false;
            defer v4.deinit(gpa);
            var m2 = m1.put(gpa, 1, v4) catch break :blk false;
            defer m2.deinit(gpa);

            var m3 = m2.delete(gpa, 2) catch break :blk false;
            defer m3.deinit(gpa);

            var itr = m3.iter();
            defer itr.deinit(gpa);
            while (itr.next()) |kv| {
                var e = kv;
                e.deinit(gpa);
            }

            break :blk true;
        };

        if (completed) break;
    }
}

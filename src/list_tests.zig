const std = @import("std");
const List = @import("list.zig").List;

test "Get" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var new_list_0 = try list.append(gpa, 4);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try new_list_0.append(gpa, 6);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try list.append(gpa, 5);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try list.append(gpa, 9);
    defer new_list_3.deinit(gpa);

    try std.testing.expectEqual(list.get(0), 3);
    try std.testing.expectEqual(list.getPtr(0).?.*, 3);
    try std.testing.expectEqual(new_list_3.get(0), 9);
    try std.testing.expectEqual(list.get(2), 1);
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

    var new_list_0 = try list.append(gpa, 4);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try new_list_0.append(gpa, 6);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try list.append(gpa, 5);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try list.append(gpa, 9);
    defer new_list_3.deinit(gpa);

    var new_list_4 = try empty.append(gpa, 1);
    defer new_list_4.deinit(gpa);

    var new_list_5 = try single.append(gpa, 8);
    defer new_list_5.deinit(gpa);

    try std.testing.expect(new_list_0.equalsSlice(&[_]u32{ 4, 3, 2, 1 }));
    try std.testing.expect(new_list_1.equalsSlice(&[_]u32{ 6, 4, 3, 2, 1 }));
    try std.testing.expect(new_list_2.equalsSlice(&[_]u32{ 5, 3, 2, 1 }));
    try std.testing.expect(new_list_3.equalsSlice(&[_]u32{ 9, 3, 2, 1 }));
    try std.testing.expect(new_list_4.equalsSlice(&[_]u32{1}));
    try std.testing.expect(new_list_5.equalsSlice(&[_]u32{ 8, 7 }));
}

test "Insert" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var new_list_0 = try list.insert_at(gpa, 0, 9);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try list.insert_at(gpa, 1, 9);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try list.insert_at(gpa, 2, 9);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try new_list_2.insert_at(gpa, 3, 7);
    defer new_list_3.deinit(gpa);

    var new_list_4 = try new_list_2.insert_at(gpa, 1, 5);
    defer new_list_4.deinit(gpa);

    var new_list_5 = try list.insert_at(gpa, 3, 0);
    defer new_list_5.deinit(gpa);

    try std.testing.expect(new_list_0.equalsSlice(&[_]u32{ 9, 3, 2, 1 }));
    try std.testing.expect(new_list_1.equalsSlice(&[_]u32{ 3, 9, 2, 1 }));
    try std.testing.expect(new_list_2.equalsSlice(&[_]u32{ 3, 2, 9, 1 }));
    try std.testing.expect(new_list_3.equalsSlice(&[_]u32{ 3, 2, 9, 7, 1 }));
    try std.testing.expect(new_list_4.equalsSlice(&[_]u32{ 3, 5, 2, 9, 1 }));
    try std.testing.expect(new_list_5.equalsSlice(&[_]u32{ 3, 2, 1, 0 }));
}

test "update" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
    defer list.deinit(gpa);

    var tail_node = try MyList.init(gpa, &[_]u32{ 6, 7 });
    defer tail_node.deinit(gpa);

    var multi = try MyList.initWithTail(gpa, &[_]u32{ 1, 2, 3 }, &tail_node);
    defer multi.deinit(gpa);

    var new_list_0 = try list.update(gpa, 0, 9);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try list.update(gpa, 1, 9);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try list.update(gpa, 2, 9);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try new_list_2.update(gpa, 0, 5);
    defer new_list_3.deinit(gpa);

    var new_list_4 = try multi.update(gpa, 0, 5);
    defer new_list_4.deinit(gpa);

    var new_list_5 = try multi.update(gpa, 3, 8);
    defer new_list_5.deinit(gpa);

    var new_list_6 = try multi.update(gpa, 4, 8);
    defer new_list_6.deinit(gpa);

    try std.testing.expect(new_list_0.equalsSlice(&[_]u32{ 9, 2, 1 }));
    try std.testing.expect(new_list_1.equalsSlice(&[_]u32{ 3, 9, 1 }));
    try std.testing.expect(new_list_2.equalsSlice(&[_]u32{ 3, 2, 9 }));
    try std.testing.expect(new_list_3.equalsSlice(&[_]u32{ 5, 2, 9 }));
    try std.testing.expect(new_list_4.equalsSlice(&[_]u32{ 5, 2, 1, 7, 6 }));
    try std.testing.expect(new_list_5.equalsSlice(&[_]u32{ 3, 2, 1, 8, 6 }));
    try std.testing.expect(new_list_6.equalsSlice(&[_]u32{ 3, 2, 1, 7, 8 }));
    try std.testing.expectError(error.IndexOutOfRange, list.update(gpa, 3, 9));
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

    var new_list_0 = try list.delete_at(gpa, 0);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try list.delete_at(gpa, 1);
    defer new_list_1.deinit(gpa);

    var new_list_2 = try list.delete_at(gpa, 2);
    defer new_list_2.deinit(gpa);

    var new_list_3 = try list.delete_at(gpa, 3);
    defer new_list_3.deinit(gpa);

    var new_list_4 = try new_list_2.delete_at(gpa, 2);
    defer new_list_4.deinit(gpa);

    var new_list_5 = try list.delete_at(gpa, 4);
    defer new_list_5.deinit(gpa);

    var new_list_6 = try head_list.delete_at(gpa, 0);
    defer new_list_6.deinit(gpa);

    var new_list_7 = try tail_list.delete_at(gpa, 3);
    defer new_list_7.deinit(gpa);

    var new_list_8 = try single.delete_at(gpa, 0);
    defer new_list_8.deinit(gpa);

    try std.testing.expect(new_list_0.equalsSlice(&[_]u32{ 4, 3, 2, 1 }));
    try std.testing.expect(new_list_1.equalsSlice(&[_]u32{ 5, 3, 2, 1 }));
    try std.testing.expect(new_list_2.equalsSlice(&[_]u32{ 5, 4, 2, 1 }));
    try std.testing.expect(new_list_3.equalsSlice(&[_]u32{ 5, 4, 3, 1 }));
    try std.testing.expect(new_list_4.equalsSlice(&[_]u32{ 5, 4, 1 }));
    try std.testing.expect(new_list_5.equalsSlice(&[_]u32{ 5, 4, 3, 2 }));
    try std.testing.expect(new_list_6.equalsSlice(&[_]u32{ 3, 2, 1 }));
    try std.testing.expect(new_list_7.equalsSlice(&[_]u32{ 3, 2, 1 }));
    try std.testing.expect(new_list_8.equalsSlice(&[_]u32{}));
}

test "Empty" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var list = try MyList.init(gpa, &[_]u32{});
    defer list.deinit(gpa);

    var new_list_0 = try list.append(gpa, 1);
    defer new_list_0.deinit(gpa);

    var new_list_1 = try list.insert_at(gpa, 0, 5);
    defer new_list_1.deinit(gpa);

    try std.testing.expect(list.equalsSlice(&[_]u32{}));
    try std.testing.expect(new_list_0.equalsSlice(&[_]u32{1}));
    try std.testing.expect(new_list_1.equalsSlice(&[_]u32{5}));
    try std.testing.expectError(error.IndexOutOfRange, list.insert_at(gpa, 2, 9));
    try std.testing.expectError(error.IndexOutOfRange, list.delete_at(gpa, 0));
}

test "tail" {
    const MyList = List(u32);
    const gpa = std.testing.allocator;

    var empty = try MyList.init(gpa, &[_]u32{});
    defer empty.deinit(gpa);

    var list = try MyList.initWithTail(gpa, &[_]u32{ 1, 2, 3 }, &empty);
    defer list.deinit(gpa);

    const empty_tail = try empty.tail(gpa);

    try std.testing.expect(list.equalsSlice(&[_]u32{ 3, 2, 1 }));
    try std.testing.expect(empty_tail == null);
}

test "allocation failures" {
    const L = List(u32);
    var fail_index: usize = 0;
    while (true) : (fail_index += 1) {
        var fa = std.testing.FailingAllocator.init(std.testing.allocator, .{ .fail_index = fail_index });
        const gpa = fa.allocator();

        const completed = blk: {
            var l0 = L.init(gpa, &[_]u32{ 1, 2, 3 }) catch break :blk false;
            defer l0.deinit(gpa);
            var l1 = l0.append(gpa, 4) catch break :blk false;
            defer l1.deinit(gpa);
            var l2 = l0.append(gpa, 5) catch break :blk false;
            defer l2.deinit(gpa);
            var l3 = l1.insert_at(gpa, 2, 9) catch break :blk false;
            defer l3.deinit(gpa);
            var l4 = l3.delete_at(gpa, 1) catch break :blk false;
            defer l4.deinit(gpa);
            var l5 = l3.delete_at(gpa, 0) catch break :blk false;
            defer l5.deinit(gpa);

            var tn = L.init(gpa, &[_]u32{9}) catch break :blk false;
            defer tn.deinit(gpa);
            var joined = L.initWithTail(gpa, &[_]u32{ 1, 2, 3 }, &tn) catch break :blk false;
            defer joined.deinit(gpa);
            var ins = joined.insert_at(gpa, 4, 7) catch break :blk false;
            defer ins.deinit(gpa);
            var col = joined.delete_at(gpa, 3) catch break :blk false;
            defer col.deinit(gpa);
            var t = (joined.tail(gpa) catch break :blk false) orelse break :blk false;
            defer t.deinit(gpa);

            var upd0 = l3.update(gpa, 0, 8) catch break :blk false;
            defer upd0.deinit(gpa);
            var upd1 = l3.update(gpa, 2, 8) catch break :blk false;
            defer upd1.deinit(gpa);
            var upd2 = joined.update(gpa, 0, 8) catch break :blk false;
            defer upd2.deinit(gpa);
            var upd3 = joined.update(gpa, 3, 8) catch break :blk false;
            defer upd3.deinit(gpa);

            var big: [40]u32 = undefined;
            for (&big, 0..) |*x, i| x.* = @intCast(i);
            var b0 = L.init(gpa, &big) catch break :blk false;
            defer b0.deinit(gpa);
            var b1 = b0.append(gpa, 100) catch break :blk false;
            defer b1.deinit(gpa);
            var b2 = b0.append(gpa, 200) catch break :blk false;
            defer b2.deinit(gpa);

            var japp = joined.append(gpa, 1) catch break :blk false;
            defer japp.deinit(gpa);
            var tn3 = L.init(gpa, &[_]u32{ 6, 7, 8 }) catch break :blk false;
            defer tn3.deinit(gpa);
            var j3 = L.initWithTail(gpa, &[_]u32{ 1, 2, 3 }, &tn3) catch break :blk false;
            defer j3.deinit(gpa);
            var d3 = j3.delete_at(gpa, 4) catch break :blk false;
            defer d3.deinit(gpa);

            break :blk true;
        };

        if (completed) break;
    }
}

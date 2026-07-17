const std = @import("std");
const VM = @import("vm.zig").VM;
const Function = @import("obj/function.zig").Function;
const Closure = @import("obj/function.zig").Closure;
const ClosureMember = @import("obj/function.zig").ClosureMember;
const RC = @import("obj/ref_counter.zig").RC;
const Value = @import("value.zig").Value;
const List = @import("obj/list.zig").List(Value);
const Iterator = @import("obj/iterator.zig").Iterator;

pub const Obj = union(enum) {
    function: Function,
    closure: Closure,
    closure_member: ClosureMember,
    list: List,
    iterator: Iterator,

    pub fn deinit(obj: *Obj, gpa: std.mem.Allocator) void {
        switch (obj.*) {
            inline else => |*o| o.deinit(gpa),
        }
    }

    pub fn initList(gpa: std.mem.Allocator, values: []Value) !Obj {
        return .{
            .list = try List.initOwned(gpa, values),
        };
    }

    pub fn initFunction(
        name: []const u8,
        vm: VM,
    ) Obj {
        return .{
            .function = .{
                .vm = vm,
                .name = name,
            },
        };
    }

    pub fn format(obj: Obj, writer: *std.Io.Writer) !void {
        switch (obj) {
            .list => |list| {
                try writer.writeByte('[');

                var iter = list.iterNoBorrow();
                var first = true;
                while (iter.next()) |v| {
                    if (!first)
                        _ = try writer.write(", ")
                    else
                        first = false;
                    try v.format(writer);
                }

                try writer.writeByte(']');
            },
            inline else => |f| try f.format(writer),
        }
    }
};

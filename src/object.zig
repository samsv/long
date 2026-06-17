const std = @import("std");
const Value = @import("value.zig").Value;
const List = @import("list.zig").List(Value).LL;

pub const Obj = union(enum) {
    list: List,

    pub fn deinit(obj: *Obj, gpa: std.mem.Allocator) void {
        switch (obj.*) {
            inline else => |*o| o.deinit(gpa),
        }
    }
};

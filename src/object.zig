const std = @import("std");
const RC = @import("ref_counter.zig").RC;
const Value = @import("value.zig").Value;
const List = @import("list.zig").List(Value).LL;

pub const Obj = struct {
    kind: RC(Kind),

    const Kind = union(enum) {
        list: List,
    };

    pub fn deinit(obj: *Obj, gpa: std.mem.Allocator) !void {
        const v = try obj.kind.getPtr();
        switch (v.*) {
            inline else => |*o| o.deinit(gpa),
        }
    }
};

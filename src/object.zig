const std = @import("std");
const Value = @import("value.zig").Value;
const pstruct = @import("pstruct");

pub const Obj = union(enum) {
    vector: VecT,

    pub fn deinit(obj: *Obj, gpa: std.mem.Allocator) void {
        switch (obj.*) {
            inline else => |*o| o.deinit(gpa),
        }
    }

    pub const VecT = pstruct.AutoPVector(Value);
};

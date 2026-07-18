const std = @import("std");
const VM = @import("../vm.zig").VM;
const RC = @import("ref_counter.zig").RC;
const Value = @import("../value.zig").Value;

pub const Closure = struct {
    function: RC(VM),
    upvalues: std.ArrayList(Value),

    pub fn deinit(self: *Closure, gpa: std.mem.Allocator) void {
        self.function.deinit(gpa);

        for (self.upvalues.items) |*up| up.deinit(gpa);
        self.upvalues.deinit(gpa);
    }

    pub fn getVM(self: Closure) VM {
        return self.function.getUnwrap();
    }

    pub fn format(cls: Closure, writer: *std.Io.Writer) !void {
        try writer.print("{s}", .{cls.function.getUnwrap().name});
    }
};

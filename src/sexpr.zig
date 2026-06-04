const std = @import("std");
const Token = @import("scanner.zig").Token;

pub const SExpr = union(enum) {
    atom: Token,
    cons: std.ArrayList(SExpr),

    pub fn deinit(s: *SExpr, gpa: std.mem.Allocator) void {
        switch (s.*) {
            .atom => {},
            .cons => |*cons| {
                for (cons.items) |*c| {
                    c.deinit(gpa);
                }
                cons.deinit(gpa);
            },
        }
    }

    pub fn print(s: SExpr, writer: *std.Io.Writer) !void {
        return switch (s) {
            .atom => |t| t.kind.print(writer),
            .cons => |cons| {
                try writer.writeByte('(');
                for (cons.items, 0..) |c, i| {
                    try c.print(writer);
                    if (i != cons.items.len - 1)
                        try writer.writeByte(' ');
                }
                try writer.writeByte(')');
            },
        };
    }
};

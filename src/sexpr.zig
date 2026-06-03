const std = @import("std");
const Token = @import("scanner.zig").Token;

pub const SExpr = union(enum) {
    atom: Token,
    cons: std.ArrayList(SExpr),

    pub fn print(s: SExpr, writer: *std.Io.Writer) !void {
        return switch (s) {
            .atom => |t| t.kind.print(writer),
            .cons => |cons| {
                try writer.writeByte('(');
                for (cons.items) |c| {
                    try c.print(writer);
                }
                try writer.writeByte(')');
            },
        };
    }
};

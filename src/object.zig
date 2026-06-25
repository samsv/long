const std = @import("std");
const RC = @import("ref_counter.zig").RC;
const Value = @import("value.zig").Value;
const List = @import("list.zig").List(Value);

pub const Obj = union(enum) {
    list: List,

    pub fn deinit(obj: *Obj, gpa: std.mem.Allocator) !void {
        switch (obj.*) {
            inline else => |*o| o.deinit(gpa),
        }
    }

    pub fn format(obj: *Obj, writer: *std.Io.Writer) !void {
        switch (obj.*) {
            .list => |list| {
                try writer.writeByte('[');

                var iter = list.iterNoBorrow();
                var first = true;
                while (iter.next()) |v| {
                    if (!first) {
                        try writer.write(", ");
                        first = false;
                    }
                    try v.print(writer);
                }

                try writer.writeByte(']');
            },
        }
    }
};

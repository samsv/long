const std = @import("std");
const Value = @import("value.zig").Value;

pub const VM = struct {
    bytecode: std.ArrayList(u8),
    stack: std.ArrayList(Value),
    lines: std.ArrayList(usize),
    ip: usize,

    pub fn init() VM {
        return .{
            .bytecode = .empty,
            .stack = .empty,
            .lines = .empty,
            .ip = 0,
        };
    }

    pub fn deint(vm: *VM, gpa: std.mem.Allocator) void {
        vm.bytecode.deinit(gpa);
        vm.stack.deinit(gpa);
        vm.lines.deinit(gpa);
    }

    pub fn addByte(vm: *VM, gpa: std.mem.Allocator, b: u8, line: usize) !void {
        try vm.bytecode.append(gpa, b);
        try vm.lines.append(gpa, line);
    }

    pub fn printStack(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeByte('[');
        for (vm.stack.items, 0..) |value, i| {
            try value.print(writer);
            if (i < vm.stack.items.len - 1)
                try writer.writeAll(", ");
        }
        try writer.writeByte(']');
    }

    fn mathOp(vm: *VM, op: Instructions) !void {
        const v2 = vm.stack.pop() orelse unreachable;
        const v1 = vm.stack.pop() orelse unreachable;

        const v: Value = switch (v1) {
            .number => |n1| switch (v2) {
                .number => |n2| switch (op) {
                    .add => .{ .number = n1 + n2 },
                    .sub => .{ .number = n1 - n2 },
                    .mul => .{ .number = n1 * n2 },
                    .div => if (n2 != 0.0) .{ .number = n1 / n2 } else return error.DivisionBy0,
                    else => unreachable,
                },
                else => return error.InvalidArguments,
            },
            else => return error.InvalidArguments,
        };

        vm.stack.appendAssumeCapacity(v);
    }

    pub fn run(vm: *VM, gpa: std.mem.Allocator) !void {
        _ = gpa;
        while (vm.ip < vm.bytecode.items.len) {
            const instruction: Instructions = @enumFromInt(vm.bytecode.items[vm.ip]);
            switch (instruction) {
                .add, .sub, .mul, .div => try vm.mathOp(instruction),
                else => return error.NotImplemented,
            }
            vm.ip += 1;
        }
    }


    pub const Instructions = enum {
        @"if",
        add,
        sub,
        mul,
        div,
        negate,
    };
};


const std = @import("std");
const Value = @import("value.zig").Value;

pub const Chunk = struct {
    bytecode: std.ArrayList(u8),
    lines: std.ArrayList(usize),
    constants: std.ArrayList(Value),

    pub const empty: Chunk = .{
        .bytecode = .empty,
        .lines = .empty,
        .constants = .empty,
    };

    pub fn deinit(chunk: *Chunk, gpa: std.mem.Allocator) void {
        chunk.bytecode.deinit(gpa);
        chunk.constants.deinit(gpa);
        chunk.lines.deinit(gpa);
    }
};

pub const VM = struct {
    chunk: Chunk,
    stack: std.ArrayList(Value),
    ip: usize,

    pub fn init() VM {
        return .{
            .chunk = .empty,
            .stack = .empty,
            .ip = 0,
        };
    }

    pub fn deint(vm: *VM, gpa: std.mem.Allocator) void {
        vm.stack.deinit(gpa);
        vm.chunk.deinit(gpa);
    }

    pub fn addByte(vm: *VM, gpa: std.mem.Allocator, b: u8, line: usize) !void {
        try vm.chunk.bytecode.append(gpa, b);
        try vm.chunk.lines.append(gpa, line);
    }

    pub fn addBytes(vm: *VM, gpa: std.mem.Allocator, b1: u8, b2: u8, line: usize) !void {
        try vm.chunk.bytecode.append(gpa, b1);
        try vm.chunk.bytecode.append(gpa, b2);
        try vm.chunk.lines.append(gpa, line);
        try vm.chunk.lines.append(gpa, line);
    }

    pub fn addConstant(vm: *VM, gpa: std.mem.Allocator, c: Value) !u8 {
        try vm.chunk.constants.append(gpa, c);
        const i: u8 = @intCast(vm.chunk.constants.items.len - 1);
        try vm.addBytes(gpa, @intFromEnum(Instructions.load_constant), i, 0);
        return i;
    }

    fn getOffset(vm: VM, i: usize) u16 {
        const low: u16 = @intCast(vm.chunk.bytecode.items[i]);
        const high = @as(u16, @intCast(vm.chunk.bytecode.items[i + 1])) << 8;
        return low + high;
    }

    pub fn printStack(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeAll("===== Stack =====\n");
        try writer.writeByte('[');
        for (vm.stack.items, 0..) |value, i| {
            try value.print(writer);
            if (i < vm.stack.items.len - 1)
                try writer.writeAll(", ");
        }
        try writer.writeByte(']');
    }

    pub fn printInstructions(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeAll("===== Instructions =====\n");
        var i: usize = 0;
        while (i < vm.chunk.bytecode.items.len) {
            const instruction: Instructions = @enumFromInt(vm.chunk.bytecode.items[i]);
            switch (instruction) {
                .add, .sub, .mul, .div, .negate => {
                    try writer.print("{} [ {s} ]\n", .{i, @tagName(instruction)});
                    i += 1;
                },
                .jump, .jump_if_false => {
                    try writer.print("{} [ {s} ]", .{i, @tagName(instruction)});
                    const offset = vm.getOffset(i + 1);
                    try writer.print(" offset {}\n", .{offset});
                    i += 3;
                },
                .load_constant => {
                    try writer.print("{} [ {s} ]", .{i, @tagName(instruction)});
                    const index = vm.chunk.bytecode.items[i + 1];
                    try writer.print(" index {}\n", .{index});
                    i += 2;
                }
            }
        }
    }

    pub fn patchJump(vm: *VM, index: usize, value: u16) void {
        vm.chunk.bytecode.items[index] = @truncate(value);
        vm.chunk.bytecode.items[index+1] = @truncate(value >> 8);
    }

    pub fn addJump(vm: *VM, gpa: std.mem.Allocator, line: usize) !usize {
        try vm.addByte(gpa, @intFromEnum(Instructions.jump), line);
        try vm.addBytes(gpa, 255, 255, line);
        return vm.chunk.bytecode.items.len - 2;
    }

    pub fn addJumpIfFalse(vm: *VM, gpa: std.mem.Allocator, line: usize) !usize {
        try vm.addByte(gpa, @intFromEnum(Instructions.jump_if_false), line);
        try vm.addBytes(gpa, 255, 255, line);
        return vm.chunk.bytecode.items.len - 2;
    }

    fn mathOp(vm: *VM, op: Instructions) !void {
        const v2 = vm.stack.pop().?;
        const v1 = vm.stack.pop().?;

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

    fn loadConstant(vm: *VM, gpa: std.mem.Allocator) !void {
        const i = vm.chunk.bytecode.items[vm.ip + 1];
        const v = vm.chunk.constants.items[i];
        try vm.stack.append(gpa, v);
        vm.ip += 1;
    }

    fn jump(vm: *VM) !void {
        const offset = vm.getOffset(vm.ip + 1);
        vm.ip += offset;
    }

    fn jumpIfFalse(vm: *VM) void {
        const v = vm.stack.pop().?;
        if (v.isTruthy()) {
            // don't jump
            vm.ip += 2;
        } else {
            // jump
            const offset = vm.getOffset(vm.ip + 1);
            vm.ip += offset;
        }
    }

    pub fn run(vm: *VM, gpa: std.mem.Allocator) !void {
        while (vm.ip < vm.chunk.bytecode.items.len) {
            const instruction: Instructions = @enumFromInt(vm.chunk.bytecode.items[vm.ip]);
            try switch (instruction) {
                .add, .sub, .mul, .div => vm.mathOp(instruction),
                .load_constant => vm.loadConstant(gpa),
                .jump => vm.jump(),
                .jump_if_false => vm.jumpIfFalse(),
                else => return error.NotImplemented,
            };
            vm.ip += 1;
        }
    }


    pub const Instructions = enum {
        add,
        sub,
        mul,
        div,
        load_constant,
        negate,
        jump,
        jump_if_false,
    };
};


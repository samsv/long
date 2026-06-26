const std = @import("std");
const Value = @import("value.zig").Value;

pub const Chunk = struct {
    bytecode: std.ArrayList(u8),
    lines: std.ArrayList(usize),
    constants: std.ArrayList(Value),
    locals: std.ArrayList(Value),

    pub const empty: Chunk = .{
        .bytecode = .empty,
        .lines = .empty,
        .constants = .empty,
        .locals = .empty,
    };

    pub fn deinit(chunk: *Chunk, gpa: std.mem.Allocator) void {
        chunk.bytecode.deinit(gpa);
        chunk.constants.deinit(gpa);
        chunk.locals.deinit(gpa);
        chunk.lines.deinit(gpa);
    }
};

pub const VM = struct {
    chunk: Chunk,
    globals: std.ArrayList(Value),
    stack: std.ArrayList(Value),
    ip: usize,

    pub fn init() VM {
        return .{
            .chunk = .empty,
            .stack = .empty,
            .globals = .empty,
            .ip = 0,
        };
    }

    pub fn deint(vm: *VM, gpa: std.mem.Allocator) void {
        for (vm.stack.items) |*v| v.deinit(gpa);
        for (vm.globals.items) |*v| v.deinit(gpa);
        vm.stack.deinit(gpa);
        vm.chunk.deinit(gpa);
        vm.globals.deinit(gpa);
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

    fn printSlice(slice: []const Value, writer: *std.Io.Writer) !void {
        try writer.writeByte('[');
        for (slice, 0..) |value, i| {
            try value.format(writer);
            if (i < slice.len - 1)
                try writer.writeAll(", ");
        }
        try writer.writeByte(']');
    }

    pub fn printStack(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeAll("===== Stack =====\n");
        try printSlice(vm.stack.items, writer);
    }

    pub fn printLocals(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeAll("===== Locals =====\n");
        try printSlice(vm.chunk.locals.items, writer);
    }

    pub fn printGlobals(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeAll("===== Globals =====\n");
        try printSlice(vm.globals.items, writer);
    }

    pub fn printInstructions(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeAll("===== Instructions =====\n");
        var i: usize = 0;
        while (i < vm.chunk.bytecode.items.len) {
            const instruction: Instructions = @enumFromInt(vm.chunk.bytecode.items[i]);
            switch (instruction) {
                .add, .sub, .mul, .div, .negate, .set_global, .set_local, .pop => {
                    try writer.print("{} [ {s} ]\n", .{ i, @tagName(instruction) });
                    i += 1;
                },
                .jump, .jump_if_false => {
                    const offset = vm.getOffset(i + 1);
                    try writer.print("{} [ {s} ] offset {}\n", .{ i, @tagName(instruction), offset });
                    i += 3;
                },
                .pop_local, .list => {
                    const n = vm.chunk.bytecode.items[i + 1];
                    try writer.print("{} [ {s} ] size {} \n", .{ i, @tagName(instruction), n });
                    i += 2;
                },
                .load_constant, .get_global, .get_local => {
                    const index = vm.chunk.bytecode.items[i + 1];
                    try writer.print("{} [ {s} ] index {}\n", .{ i, @tagName(instruction), index });
                    i += 2;
                },
            }
        }
    }

    pub fn patchJump(vm: *VM, index: usize, value: u16) void {
        vm.chunk.bytecode.items[index] = @truncate(value);
        vm.chunk.bytecode.items[index + 1] = @truncate(value >> 8);
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
            else => {
                std.log.err("Can not {s} {f} with {f}\n", .{ @tagName(op), v1, v2 });
                return error.InvalidArguments;
            },
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

    fn setGlobal(vm: *VM, gpa: std.mem.Allocator) !void {
        var v = vm.stack.getLast();
        try vm.globals.append(gpa, v.borrow());
    }

    fn getGlobal(vm: *VM, gpa: std.mem.Allocator) !void {
        const i = vm.chunk.bytecode.items[vm.ip + 1];
        if (i >= vm.globals.items.len)
            return error.UndefinedGlobal;

        const v = vm.globals.items[i];
        try vm.stack.append(gpa, v);
        vm.ip += 1;
    }

    fn setLocal(vm: *VM, gpa: std.mem.Allocator) !void {
        var v = vm.stack.getLast();
        try vm.chunk.locals.append(gpa, v.borrow());
    }

    fn getLocal(vm: *VM, gpa: std.mem.Allocator) !void {
        const i = vm.chunk.bytecode.items[vm.ip + 1];
        const v = vm.chunk.locals.items[i];
        try vm.stack.append(gpa, v);
        vm.ip += 1;
    }

    fn popLocal(vm: *VM, gpa: std.mem.Allocator) void {
        const index = vm.chunk.bytecode.items[vm.ip + 1];
        for (vm.chunk.locals.items[vm.chunk.locals.items.len - index..]) |*v|
            v.deinit(gpa);

        vm.chunk.locals.items.len -= index;
        vm.ip += 1;
    }

    fn makeList(vm: *VM, gpa: std.mem.Allocator) !void {
        const n = vm.chunk.bytecode.items[vm.ip + 1];
        const values = try gpa.alloc(Value, n);
        @memcpy(values, vm.stack.items[vm.stack.items.len - n..vm.stack.items.len]);
        vm.stack.items.len -= n;
        try vm.stack.append(gpa, try Value.initList(gpa, values));
        vm.ip += 1;
    }

    pub fn run(vm: *VM, gpa: std.mem.Allocator) !void {
        while (vm.ip < vm.chunk.bytecode.items.len) {
            const instruction: Instructions = @enumFromInt(vm.chunk.bytecode.items[vm.ip]);
            try switch (instruction) {
                .add, .sub, .mul, .div => vm.mathOp(instruction),
                .set_global => vm.setGlobal(gpa),
                .get_global => vm.getGlobal(gpa),
                .set_local => vm.setLocal(gpa),
                .get_local => vm.getLocal(gpa),
                .pop_local => vm.popLocal(gpa),
                .load_constant => vm.loadConstant(gpa),
                .jump => vm.jump(),
                .jump_if_false => vm.jumpIfFalse(),
                .pop => _ = vm.stack.pop(),
                .list => vm.makeList(gpa),
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
        set_global,
        get_global,
        set_local,
        get_local,
        pop_local,
        load_constant,
        negate,
        pop,
        jump,
        jump_if_false,
        list,
    };
};

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
        for (chunk.constants.items) |*v| v.deinit(gpa);

        chunk.bytecode.deinit(gpa);
        chunk.constants.deinit(gpa);
        chunk.lines.deinit(gpa);
    }
};

pub const Array = struct {
    values: std.ArrayList(Value),

    pub const empty: Array = .{ .values = .empty };

    pub inline fn items(self: Array) []const Value {
        return self.values.items;
    }

    pub fn initFrom(gpa: std.mem.Allocator, values: []const Value) !Array {
        var arr: std.ArrayList(Value) = try .initCapacity(gpa, values.len);
        arr.appendSliceAssumeCapacity(values);
        return .{ .values = arr };
    }

    pub inline fn len(self: Array) usize {
        return self.values.items.len;
    }

    pub inline fn get(self: Array, i: usize) Value {
        return self.values.items[i];
    }

    pub fn append(self: *Array, gpa: std.mem.Allocator, value: *Value) !void {
        try self.values.append(gpa, value.borrow());
    }

    pub fn appendNoBorrow(self: *Array, gpa: std.mem.Allocator, value: Value) !void {
        try self.values.append(gpa, value);
    }

    pub fn appendAssumeCapacity(self: *Array, value: *Value) void {
        self.values.appendAssumeCapacity(value.borrow());
    }

    pub fn appendAssumeCapacityNoBorrow(self: *Array, value: Value) void {
        self.values.appendAssumeCapacity(value);
    }

    pub fn deinit(self: *Array, gpa: std.mem.Allocator) void {
        for (self.values.items) |*v|
            v.deinit(gpa);
        self.values.deinit(gpa);
    }

    pub fn pop(self: *Array) Value {
        return self.values.pop().?;
    }

    pub fn last(self: *Array) Value {
        return self.values.getLast();
    }

    pub fn remove(self: *Array, gpa: std.mem.Allocator) void {
        var v = self.values.pop().?;
        v.deinit(gpa);
    }

    pub fn removeN(self: *Array, gpa: std.mem.Allocator, n: usize) void {
        for (self.values.items[self.values.items.len - n ..]) |*v|
            v.deinit(gpa);

        self.values.items.len -= n;
    }
};

pub const VMBuilder = struct {
    vm: VM,

    pub fn init(gpa: std.mem.Allocator) !VMBuilder {
        return .{ .vm = try VM.init(gpa) };
    }

    pub fn build(self: VMBuilder) VM {
        return self.vm;
    }

    pub fn addByte(builder: *VMBuilder, gpa: std.mem.Allocator, b: u8, line: usize) !void {
        try builder.vm.chunk.bytecode.append(gpa, b);
        try builder.vm.chunk.lines.append(gpa, line);
    }

    pub fn addBytes(builder: *VMBuilder, gpa: std.mem.Allocator, b1: u8, b2: u8, line: usize) !void {
        try builder.vm.chunk.bytecode.append(gpa, b1);
        try builder.vm.chunk.bytecode.append(gpa, b2);
        try builder.vm.chunk.lines.append(gpa, line);
        try builder.vm.chunk.lines.append(gpa, line);
    }

    pub fn addConstant(builder: *VMBuilder, gpa: std.mem.Allocator, c: Value) !u8 {
        try builder.vm.chunk.constants.append(gpa, c);
        const i: u8 = @intCast(builder.vm.chunk.constants.items.len - 1);
        try builder.addBytes(gpa, @intFromEnum(VM.Instructions.load_constant), i, 0);
        return i;
    }

    pub fn patchJump(builder: *VMBuilder, index: usize, value: u16) void {
        builder.vm.chunk.bytecode.items[index] = @truncate(value);
        builder.vm.chunk.bytecode.items[index + 1] = @truncate(value >> 8);
    }

    pub fn addJump(builder: *VMBuilder, gpa: std.mem.Allocator, line: usize) !usize {
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.jump), line);
        try builder.addBytes(gpa, 255, 255, line);
        return builder.vm.chunk.bytecode.items.len - 2;
    }

    pub fn addJumpBack(builder: *VMBuilder, gpa: std.mem.Allocator, to: usize, line: usize) !void {
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.jump_back), line);
        const offset: u16 = @intCast(builder.vm.chunk.bytecode.items.len - to);
        try builder.addBytes(gpa, @truncate(offset), @truncate(offset >> 8), line);
    }

    pub fn addJumpIfFalse(builder: *VMBuilder, gpa: std.mem.Allocator, line: usize) !usize {
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.jump_if_false), line);
        try builder.addBytes(gpa, 255, 255, line);
        return builder.vm.chunk.bytecode.items.len - 2;
    }
};

pub const VM = struct {
    chunk: Chunk,
    globals: *Array,
    stack: Array,
    locals: Array,
    ip: usize,

    pub fn init(gpa: std.mem.Allocator) !VM {
        const globals = try gpa.create(Array);
        globals.* = .empty;
        return .{
            .chunk = .empty,
            .stack = .empty,
            .locals = .empty,
            .globals = globals,
            .ip = 0,
        };
    }

    pub fn deint(vm: *VM, gpa: std.mem.Allocator) void {
        vm.stack.deinit(gpa);
        vm.chunk.deinit(gpa);
        vm.locals.deinit(gpa);
        vm.globals.deinit(gpa);
        gpa.destroy(vm.globals);
    }

    fn getOffset(vm: VM, i: usize) u16 {
        const low: u16 = @intCast(vm.chunk.bytecode.items[i]);
        const high = @as(u16, @intCast(vm.chunk.bytecode.items[i + 1])) << 8;
        return low + high;
    }

    fn printSlice(slice: []const Value, name: []const u8, writer: *std.Io.Writer) !void {
        try writer.print("===== {s} =====\n", .{name});
        try writer.writeByte('[');
        for (slice, 0..) |value, i| {
            try value.format(writer);
            if (i < slice.len - 1)
                try writer.writeAll(", ");
        }
        try writer.writeByte(']');
    }

    pub fn printStack(vm: VM, writer: *std.Io.Writer) !void {
        try printSlice(vm.stack.items(), "Stack", writer);
    }

    pub fn printLocals(vm: VM, writer: *std.Io.Writer) !void {
        try printSlice(vm.locals.items(), "Locals", writer);
    }

    pub fn printGlobals(vm: VM, writer: *std.Io.Writer) !void {
        try printSlice(vm.globals.items(), "Globals", writer);
    }

    pub fn printConstants(vm: VM, writer: *std.Io.Writer) !void {
        try printSlice(vm.chunk.constants.items, "Constants", writer);
    }

    pub fn printInstructions(vm: VM, writer: *std.Io.Writer) !void {
        try writer.writeAll("===== Instructions =====\n");
        var i: usize = 0;
        while (i < vm.chunk.bytecode.items.len) {
            const instruction: Instructions = @enumFromInt(vm.chunk.bytecode.items[i]);
            switch (instruction) {
                .add,
                .sub,
                .mul,
                .div,
                .negate,
                .set_global,
                .set_local,
                .pop,
                .iter_create,
                .iter_next,
                .equals,
                => {
                    try writer.print("{} [ {s} ]\n", .{ i, @tagName(instruction) });
                    i += 1;
                },
                .jump, .jump_back, .jump_if_false => {
                    const offset = vm.getOffset(i + 1);
                    try writer.print("{} [ {s} ] offset {}\n", .{ i, @tagName(instruction), offset });
                    i += 3;
                },
                .list => {
                    const n = vm.chunk.bytecode.items[i + 1];
                    try writer.print("{} [ {s} ] size {} \n", .{ i, @tagName(instruction), n });
                    i += 2;
                },
                .pop_local, .load_constant, .get_global, .get_local => {
                    const index = vm.chunk.bytecode.items[i + 1];
                    try writer.print("{} [ {s} ] index {}\n", .{ i, @tagName(instruction), index });
                    i += 2;
                },
                .call => {
                    const n = vm.chunk.bytecode.items[i + 1];
                    try writer.print("{} [ {s} ] args {} \n", .{ i, @tagName(instruction), n });
                    i += 2;
                },
            }
        }
    }

    fn mathOp(vm: *VM, gpa: std.mem.Allocator, op: Instructions) !void {
        var v2 = vm.stack.pop();
        var v1 = vm.stack.pop();

        defer v1.deinit(gpa);
        defer v2.deinit(gpa);

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

        vm.stack.appendAssumeCapacityNoBorrow(v);
    }

    fn equals(vm: *VM, gpa: std.mem.Allocator) void {
        var v2 = vm.stack.pop();
        var v1 = vm.stack.pop();

        defer v1.deinit(gpa);
        defer v2.deinit(gpa);

        vm.stack.appendAssumeCapacityNoBorrow(.{ .boolean = v1.eql(v2) });
    }

    fn loadConstant(vm: *VM, gpa: std.mem.Allocator) !void {
        const i = vm.chunk.bytecode.items[vm.ip + 1];
        var v = vm.chunk.constants.items[i];
        try vm.stack.append(gpa, &v);
        vm.ip += 1;
    }

    fn jump(vm: *VM) !void {
        const offset = vm.getOffset(vm.ip + 1);
        vm.ip += offset;
    }

    fn jumpBack(vm: *VM) !void {
        const offset = vm.getOffset(vm.ip + 1);
        vm.ip -= offset;
    }

    fn jumpIfFalse(vm: *VM, gpa: std.mem.Allocator) void {
        var v = vm.stack.pop();
        defer v.deinit(gpa);
        if (v.isTruthy()) {
            // don't jump
            vm.ip += 2;
        } else {
            // jump
            const offset = vm.getOffset(vm.ip + 1);
            vm.ip += offset;
        }
    }

    fn iterCreate(vm: *VM, gpa: std.mem.Allocator) !void {
        var list = vm.stack.pop();
        defer list.deinit(gpa);

        const iter = try list.createIterator(gpa);
        try vm.stack.appendNoBorrow(gpa, iter);
    }

    fn iterNext(vm: *VM, gpa: std.mem.Allocator) !void {
        var maybe_iter = vm.stack.pop();
        defer maybe_iter.deinit(gpa);
        var iter = try switch (maybe_iter) {
            .obj => |obj| switch (obj.getPtrUnwrap().*) {
                .iterator => |*iter| iter,
                else => error.NotIterator,
            },
            else => error.NotIterator,
        };

        var next = iter.next();
        try vm.stack.append(gpa, &next);
    }

    fn setGlobal(vm: *VM, gpa: std.mem.Allocator) !void {
        var v = vm.stack.last();
        try vm.globals.append(gpa, &v);
    }

    fn getGlobal(vm: *VM, gpa: std.mem.Allocator) !void {
        const i = vm.chunk.bytecode.items[vm.ip + 1];
        if (i >= vm.globals.len())
            return error.UndefinedGlobal;

        var v = vm.globals.get(i);
        try vm.stack.append(gpa, &v);
        vm.ip += 1;
    }

    fn setLocal(vm: *VM, gpa: std.mem.Allocator) !void {
        var v = vm.stack.last();
        try vm.locals.append(gpa, &v);
    }

    fn getLocal(vm: *VM, gpa: std.mem.Allocator) !void {
        const i = vm.chunk.bytecode.items[vm.ip + 1];
        var v = vm.locals.get(i);
        try vm.stack.append(gpa, &v);
        vm.ip += 1;
    }

    fn popLocal(vm: *VM, gpa: std.mem.Allocator) void {
        const index = vm.chunk.bytecode.items[vm.ip + 1];
        vm.locals.removeN(gpa, index);
        vm.ip += 1;
    }

    fn makeList(vm: *VM, gpa: std.mem.Allocator) !void {
        const n = vm.chunk.bytecode.items[vm.ip + 1];
        const values = try gpa.alloc(Value, n);
        @memcpy(values, vm.stack.items()[vm.stack.len() - n ..]);
        vm.stack.values.items.len -= n;
        try vm.stack.appendNoBorrow(gpa, try Value.initList(gpa, values));
        vm.ip += 1;
    }

    fn call(vm: *VM, gpa: std.mem.Allocator) !void {
        var value = vm.stack.pop();
        defer value.deinit(gpa);

        var function = switch (value) {
            .obj => |*obj| switch (obj.getUnwrap()) {
                .function => |fn_| fn_,
                else => return error.NotCallable,
            },
            else => return error.NotCallable,
        };

        const arg_count = vm.chunk.bytecode.items[vm.ip + 1];
        const args = vm.stack.items()[vm.stack.len() - arg_count ..];

        function.vm.globals.values.clearRetainingCapacity();
        try function.vm.globals.values.appendSlice(gpa, args);
        try function.vm.run(gpa);

        vm.stack.removeN(gpa, arg_count);
        vm.stack.appendAssumeCapacityNoBorrow(function.vm.stack.pop());

        vm.ip += 1;

        // TODO! Add upvalues to locals stack
    }

    pub fn run(vm: *VM, gpa: std.mem.Allocator) anyerror!void {
        while (vm.ip < vm.chunk.bytecode.items.len) {
            const instruction: Instructions = @enumFromInt(vm.chunk.bytecode.items[vm.ip]);
            try switch (instruction) {
                .add, .sub, .mul, .div => vm.mathOp(gpa, instruction),
                .equals => vm.equals(gpa),
                .call => vm.call(gpa),
                .set_global => vm.setGlobal(gpa),
                .get_global => vm.getGlobal(gpa),
                .set_local => vm.setLocal(gpa),
                .get_local => vm.getLocal(gpa),
                .pop_local => vm.popLocal(gpa),
                .load_constant => vm.loadConstant(gpa),
                .jump => vm.jump(),
                .jump_back => vm.jumpBack(),
                .jump_if_false => vm.jumpIfFalse(gpa),
                .iter_create => vm.iterCreate(gpa),
                .iter_next => vm.iterNext(gpa),
                .pop => vm.stack.remove(gpa),
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
        equals,
        call,
        set_global,
        get_global,
        set_local,
        get_local,
        pop_local,
        iter_create,
        iter_next,
        load_constant,
        negate,
        pop,
        jump,
        jump_back,
        jump_if_false,
        list,
    };
};

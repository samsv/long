const std = @import("std");
const parser = @import("parser.zig");
const VM = @import("vm.zig").VM;
const VMBuilder = @import("vm.zig").VMBuilder;
const Token = @import("scanner.zig").Token;
const Literal = Token.Literal;
const Operator = Token.Operator;
const Value = @import("value.zig").Value;
const SExpr = @import("sexpr.zig").SExpr;
const Scanner = @import("scanner.zig").Scanner;

pub const Globals = struct {
    name_indexes: std.StringArrayHashMapUnmanaged(usize),

    pub fn init() Globals {
        return .{
            .name_indexes = .empty,
        };
    }

    pub fn deinit(g: *Globals, gpa: std.mem.Allocator) void {
        g.name_indexes.deinit(gpa);
    }

    pub fn add(g: *Globals, gpa: std.mem.Allocator, id: []const u8) !void {
        const idx = g.name_indexes.count();
        const res = try g.name_indexes.getOrPutValue(gpa, id, idx);
        if (res.found_existing)
            return error.GlobalRedefined;
    }

    pub fn get(g: Globals, id: []const u8) !usize {
        return g.name_indexes.get(id) orelse {
            std.log.err("Variable {s} not found.\n", .{id});
            return error.UndefinedVariable;
        };
    }
};

pub const Locals = struct {
    name_indexes: std.StringArrayHashMapUnmanaged(usize),
    next: ?*Locals,
    offset: usize,

    pub fn init(next: ?*Locals) Locals {
        return .{
            .name_indexes = .empty,
            .next = next,
            .offset = if (next) |n| n.name_indexes.count() + n.offset else 0,
        };
    }

    pub fn deinit(l: *Locals, gpa: std.mem.Allocator) void {
        l.name_indexes.deinit(gpa);
    }

    pub fn add(l: *Locals, gpa: std.mem.Allocator, id: []const u8) !void {
        const idx = l.name_indexes.count();
        const res = try l.name_indexes.getOrPutValue(gpa, id, idx);
        if (res.found_existing)
            return error.LocalRedefined;
    }

    pub fn get(l: Locals, id: []const u8) ?usize {
        if (l.name_indexes.get(id)) |idx|
            return idx + l.offset;

        return if (l.next) |n| n.get(id) else return null;
    }
};

pub const Compiler = struct {
    globals: Globals,
    locals: ?*Locals,

    pub fn init() Compiler {
        return .{
            .globals = Globals.init(),
            .locals = null,
        };
    }

    pub fn deinit(c: *Compiler, gpa: std.mem.Allocator) void {
        c.globals.deinit(gpa);
        if (c.locals) |locals|
            locals.deinit(gpa);
    }

    fn compileID(c: Compiler, gpa: std.mem.Allocator, id: []const u8, line: usize, builder: *VMBuilder) !void {
        if (c.locals) |local| if (local.get(id)) |idx| {
            try builder.addBytes(gpa, @intFromEnum(VM.Instructions.get_local), @intCast(idx), line);
            return;
        };

        const idx = try c.globals.get(id);
        try builder.addBytes(gpa, @intFromEnum(VM.Instructions.get_global), @intCast(idx), line);
    }

    fn compileLiteral(c: Compiler, gpa: std.mem.Allocator, literal: Literal, line: usize, builder: *VMBuilder) !void {
        const value: Value = switch (literal) {
            .number => |n| .{ .number = n },
            .string => unreachable,
            .constant => |constant| switch (constant) {
                .true => Value.True,
                .false => Value.False,
                .nil => .nil,
            },
            .identifier => |id| return c.compileID(gpa, id, line, builder),
        };

        _ = try builder.addConstant(gpa, value);
    }

    fn expect(u: anytype, comptime tag: std.meta.Tag(@TypeOf(u))) !@FieldType(@TypeOf(u), @tagName(tag)) {
        return if (std.meta.activeTag(u) == tag) @field(u, @tagName(tag)) else error.UnexpectedValue;
    }

    fn expectId(v: SExpr) ![]const u8 {
        const atom = try expect(v, .atom);
        const literal = try expect(atom.kind, .literal);
        const id = try expect(literal, .identifier);
        return id;
    }

    fn expectKeyword(v: SExpr, kw: Token.Keywords) !void {
        const atom = try expect(v, .atom);
        _ = try expect(atom.kind, kw);
    }

    fn compileEqual(
        c: *Compiler,
        gpa: std.mem.Allocator,
        args: []const SExpr,
        builder: *VMBuilder,
        line: usize,
    ) !void {
        const id = try expectId(args[0]);
        try c.compileBuilder(gpa, args[1], builder);

        if (c.locals) |local| {
            try builder.addByte(gpa, @intFromEnum(VM.Instructions.set_local), line);
            try local.add(gpa, id);
        } else {
            try builder.addByte(gpa, @intFromEnum(VM.Instructions.set_global), line);
            try c.globals.add(gpa, id);
        }
    }

    fn compileOperator(
        c: *Compiler,
        gpa: std.mem.Allocator,
        op: Operator,
        args: []const SExpr,
        builder: *VMBuilder,
        line: usize,
    ) !void {
        const instruction = switch (op) {
            .plus => VM.Instructions.add,
            .minus => if (args.len == 1) VM.Instructions.negate else VM.Instructions.sub,
            .slash => VM.Instructions.div,
            .star => VM.Instructions.mul,
            .equal => return c.compileEqual(gpa, args, builder, line),
            else => unreachable,
        };

        for (args) |a| try c.compileBuilder(gpa, a, builder);
        try builder.addByte(gpa, @intFromEnum(instruction), line);
    }

    fn patchJump(ji: usize, builder: *VMBuilder) !void {
        const offset = builder.vm.chunk.bytecode.items.len - ji;
        if (offset > std.math.maxInt(u16)) return error.JumpTooLong;
        builder.patchJump(ji, @intCast(offset));
    }

    fn initScope(c: *Compiler, gpa: std.mem.Allocator) !void {
        const local = try gpa.create(Locals);
        local.* = Locals.init(c.locals);
        c.locals = local;
    }

    fn deinitScope(c: *Compiler, gpa: std.mem.Allocator, builder: *VMBuilder) !void {
        var local = c.locals orelse return;

        const n: u8 = @intCast(local.name_indexes.count());
        try builder.addBytes(gpa, @intFromEnum(VM.Instructions.pop_local), n, 0);

        c.locals = local.next;
        local.deinit(gpa);
        gpa.destroy(local);
    }

    fn compileIf(c: *Compiler, gpa: std.mem.Allocator, args: []const SExpr, builder: *VMBuilder) !void {
        // cond
        try c.initScope(gpa);
        defer c.deinitScope(gpa, builder) catch unreachable;

        try c.compileBuilder(gpa, args[0], builder);

        // true branch
        const j1 = try builder.addJumpIfFalse(gpa, 0);

        // true branch
        try c.compileBuilder(gpa, args[1], builder);

        // false branch
        const j2 = try builder.addJump(gpa, 0);
        try patchJump(j1, builder);
        if (args.len == 3)
            try c.compileBuilder(gpa, args[2], builder)
        else
            _ = try builder.addConstant(gpa, .nil);

        try patchJump(j2, builder);
    }

    fn compileFor(c: *Compiler, gpa: std.mem.Allocator, args: []const SExpr, builder: *VMBuilder, line: usize) !void {
        try c.initScope(gpa);
        defer c.deinitScope(gpa, builder) catch unreachable;

        // for binding
        const binding = args[0].cons;

        try c.compileBuilder(gpa, binding.items[1], builder);
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.iter_create), line);
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.set_local), line);
        try c.locals.?.add(gpa, " list_iter ");

        // for condition
        try c.initScope(gpa);
        defer c.deinitScope(gpa, builder) catch unreachable;

        const loop_start = builder.vm.chunk.bytecode.items.len;

        const idx = c.locals.?.get(" list_iter ").?;
        try builder.addBytes(gpa, @intFromEnum(VM.Instructions.get_local), @intCast(idx), line);
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.iter_next), line);

        const id = try expectId(binding.items[0]);
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.set_local), line);
        try c.locals.?.add(gpa, id);

        const j1 = try builder.addJumpIfFalse(gpa, 0);

        // pop last value from for loop. For a map function, move this before
        // const loop_start = builder.vm.chunk.bytecode.items.len;
        try builder.addByte(gpa, @intFromEnum(VM.Instructions.pop), line);

        // for body
        try c.compileBuilder(gpa, args[1], builder);

        // end
        // manually clear loop stack for next iteration
        const n: u8 = @intCast(c.locals.?.name_indexes.count());
        try builder.addBytes(gpa, @intFromEnum(VM.Instructions.pop_local), n, 0);

        try builder.addJumpBack(gpa, loop_start, line);
        try patchJump(j1, builder);
    }

    fn compileList(c: *Compiler, gpa: std.mem.Allocator, args: []const SExpr, builder: *VMBuilder, line: usize) !void {
        for (1..args.len + 1) |i|
            try c.compileBuilder(gpa, args[args.len - i], builder);

        try builder.addBytes(gpa, @intFromEnum(VM.Instructions.list), @intCast(args.len), line);
    }

    fn compileDo(c: *Compiler, gpa: std.mem.Allocator, args: []const SExpr, builder: *VMBuilder) !void {
        try c.initScope(gpa);
        defer c.deinitScope(gpa, builder) catch unreachable;

        for (args, 0..) |s, i| {
            try c.compileBuilder(gpa, s, builder);
            if (i < args.len - 1)
                try builder.addByte(gpa, @intFromEnum(VM.Instructions.pop), 0);
        }
    }

    fn compileAtom(c: Compiler, gpa: std.mem.Allocator, token: Token, builder: *VMBuilder) !void {
        switch (token.kind) {
            .literal => |literal| try c.compileLiteral(gpa, literal, token.line, builder),
            else => unreachable,
        }
    }

    fn compileCons(c: *Compiler, gpa: std.mem.Allocator, cons: []const SExpr, builder: *VMBuilder) !void {
        if (cons.len == 0) return;
        try switch (cons[0]) {
            .atom => |a| switch (a.kind) {
                .operator => |op| c.compileOperator(gpa, op, cons[1..], builder, a.line),
                .special_fns => |fn_| switch (fn_) {
                    .@"if" => c.compileIf(gpa, cons[1..], builder),
                    .@"for" => c.compileFor(gpa, cons[1..], builder, a.line),
                    .list => c.compileList(gpa, cons[1..], builder, a.line),
                    else => return error.NotImplemented,
                },
                .keywords => |k| switch (k) {
                    .do => c.compileDo(gpa, cons[1..], builder),
                    else => unreachable,
                },
                else => unreachable,
            },
            .cons => |cs| {
                try c.compileCons(gpa, cs.items, builder);
                for (cons) |sexpr| try c.compileBuilder(gpa, sexpr, builder);
            },
        };
    }

    pub fn compileBuilder(c: *Compiler, gpa: std.mem.Allocator, sexpr: SExpr, builder: *VMBuilder) anyerror!void {
        try switch (sexpr) {
            .atom => |token| c.compileAtom(gpa, token, builder),
            .cons => |cons| c.compileCons(gpa, cons.items, builder),
        };
    }

    pub fn compile(gpa: std.mem.Allocator, source: []const u8) !VM {
        var scanner = try Scanner.init(source);

        var sexprs: std.ArrayList(SExpr) = .empty;
        defer {
            for (sexprs.items) |*sexpr| sexpr.deinit(gpa);
            sexprs.deinit(gpa);
        }

        while (try scanner.peek()) |_| {
            const sexpr = try parser.expr(gpa, &scanner, 0);
            try sexprs.append(gpa, sexpr);
        }

        var builder = VMBuilder.init();

        var compiler = init();
        defer compiler.deinit(gpa);

        for (sexprs.items, 0..) |sexpr, i| {
            try compiler.compileBuilder(gpa, sexpr, &builder);
            if (i < sexprs.items.len - 1)
                try builder.addByte(gpa, @intFromEnum(VM.Instructions.pop), 0);
        }

        return builder.build();
    }
};

test "if" {
    const gpa = std.testing.allocator;

    const test_cases = [_]struct { []const u8, Value }{
        .{ "x = 5", .{ .number = 5 } },
        .{ "y = if x = 8.5 do x end", .{ .number = 8.5 } },
        .{ "1", .{ .number = 1 } },
        .{ "5 * 2.5", .{ .number = 12.5 } },
        .{ "8 / 2", .{ .number = 4 } },
        .{ "3 / 2", .{ .number = 1.5 } },
        .{ "3 - 2", .{ .number = 1 } },
        .{ "if 1 + 2 do 3 - 4 else 5 - 7 end", .{ .number = -1 } },
        .{ "if nil do 3 - 4 else 5 - 7 end", .{ .number = -2 } },
        .{ "if true do 3 - 4 end", .{ .number = -1 } },
        .{ "if false do 3 - 4 end", .nil },
    };

    for (test_cases) |cs| {
        var vm = try Compiler.compile(gpa, cs[0]);
        defer vm.deint(gpa);

        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.len());
        try std.testing.expectEqual(cs[1], vm.stack.get(0));
    }
}

test "for loop" {
    const gpa = std.testing.allocator;

    var vm = try Compiler.compile(gpa,
        \\ for x in [1, 2, 3] do
        \\      k = x + 2
        \\      k
        \\ end",
    );
    defer vm.deint(gpa);

    try vm.run(gpa);

    try std.testing.expectEqual(1, vm.stack.len());
    try std.testing.expectEqual(Value{ .number = 5 }, vm.stack.get(0));
}

const std = @import("std");
const VM = @import("vm.zig").VM;
const scanner_ = @import("scanner.zig");
const Token = scanner_.Token;
const Literal = Token.Literal;
const Operator = Token.Operator;
const Value = @import("value.zig").Value;
const SExpr = @import("sexpr.zig").SExpr;

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
        return g.name_indexes.get(id) orelse error.UndefinedVariable;
    }
};

pub const Compiler = struct {
    globals: Globals,

    pub fn init() Compiler {
        return .{
            .globals = Globals.init(),
        };
    }

    pub fn deinit(c: *Compiler, gpa: std.mem.Allocator) void {
        c.globals.deinit(gpa);
    }

    fn compileID(c: Compiler, gpa: std.mem.Allocator, id: []const u8, line: usize, vm: *VM) !void {
        const idx = try c.globals.get(id);
        try vm.addBytes(gpa, @intFromEnum(VM.Instructions.get_global), @intCast(idx), line);
    }

    fn compileLiteral(c: Compiler, gpa: std.mem.Allocator, literal: Literal, line: usize, vm: *VM) !void {
        const value: Value = switch (literal) {
            .number => |n| .{ .number = n },
            .string => unreachable,
            .constant => |constant| switch (constant) {
                .true => Value.True,
                .false => Value.False,
                .nil => .nil,
            },
            .identifier => |id| return c.compileID(gpa, id, line, vm),
        };

        _ = try vm.addConstant(gpa, value);
    }

    fn expect(u: anytype, comptime tag: std.meta.Tag(@TypeOf(u))) !@FieldType(@TypeOf(u), @tagName(tag)) {
        return if (std.meta.activeTag(u) == tag) @field(u, @tagName(tag)) else error.UnexpectedValue;
    }

    fn compileEqual(
        c: *Compiler,
        gpa: std.mem.Allocator,
        args: []const SExpr,
        vm: *VM,
        line: usize,
    ) !void {
        const atom = try expect(args[0], .atom);
        const literal = try expect(atom.kind, .literal);
        const id = try expect(literal, .identifier);

        try c.compile(gpa, args[1], vm);
        try vm.addByte(gpa, @intFromEnum(VM.Instructions.set_global), line);

        try c.globals.add(gpa, id);
    }

    fn compileOperator(
        c: *Compiler,
        gpa: std.mem.Allocator,
        op: Operator,
        args: []const SExpr,
        vm: *VM,
        line: usize,
    ) !void {
        const instruction = switch (op) {
            .plus => VM.Instructions.add,
            .minus => if (args.len == 1) VM.Instructions.negate else VM.Instructions.sub,
            .slash => VM.Instructions.div,
            .star => VM.Instructions.mul,
            .equal => return c.compileEqual(gpa, args, vm, line),
            else => unreachable,
        };

        for (args) |a| try c.compile(gpa, a, vm);
        try vm.addByte(gpa, @intFromEnum(instruction), line);
    }

    fn patchJump(ji: usize, vm: *VM) !void {
        const offset = vm.chunk.bytecode.items.len - ji;
        if (offset > std.math.maxInt(u16)) return error.JumpTooLong;
        vm.patchJump(ji, @intCast(offset));
    }

    fn compileIf(c: *Compiler, gpa: std.mem.Allocator, args: []const SExpr, vm: *VM) !void {
        // cond
        try c.compile(gpa, args[0], vm);

        // true branch
        const j1 = try vm.addJumpIfFalse(gpa, 0);

        // true branch
        try c.compile(gpa, args[1], vm);

        // false branch
        const j2 = try vm.addJump(gpa, 0);
        try patchJump(j1, vm);
        if (args.len == 3)
            try c.compile(gpa, args[2], vm)
        else
            _ = try vm.addConstant(gpa, .nil);

        try patchJump(j2, vm);
    }

    fn compileAtom(c: Compiler, gpa: std.mem.Allocator, token: Token, vm: *VM) !void {
        switch (token.kind) {
            .literal => |literal| try c.compileLiteral(gpa, literal, token.line, vm),
            else => unreachable,
        }
    }

    fn compileCons(c: *Compiler, gpa: std.mem.Allocator, cons: []const SExpr, vm: *VM) !void {
        if (cons.len == 0) return;
        try switch (cons[0]) {
            .atom => |a| switch (a.kind) {
                .operator => |op| c.compileOperator(gpa, op, cons[1..], vm, a.line),
                .special_fns => |fn_| switch (fn_) {
                    .@"if" => c.compileIf(gpa, cons[1..], vm),
                    else => return error.NotImplemented,
                },
                else => unreachable,
            },
            .cons => |cs| {
                try c.compileCons(gpa, cs.items, vm);
                for (cons) |sexpr| try c.compile(gpa, sexpr, vm);
            },
        };
    }

    pub fn compile(c: *Compiler, gpa: std.mem.Allocator, sexpr: SExpr, vm: *VM) anyerror!void {
        try switch (sexpr) {
            .atom => |token| c.compileAtom(gpa, token, vm),
            .cons => |cons| c.compileCons(gpa, cons.items, vm),
        };
    }
};

test "if" {
    const parser = @import("parser.zig");
    const Scanner = @import("scanner.zig").Scanner;

    const gpa = std.testing.allocator;

    const test_cases = [_]struct { []const u8, Value }{
        .{ "x = 5", .{ .number = 5 } },
        .{ "y = if x = 8.5 do x", .{ .number = 8.5 } },
        .{ "1", .{ .number = 1 } },
        .{ "5 * 2.5", .{ .number = 12.5 } },
        .{ "8 / 2", .{ .number = 4 } },
        .{ "3 / 2", .{ .number = 1.5 } },
        .{ "3 - 2", .{ .number = 1 } },
        .{ "if 1 + 2 do 3 - 4 else 5 - 7", .{ .number = -1 } },
        .{ "if nil do 3 - 4 else 5 - 7", .{ .number = -2 } },
        .{ "if true do 3 - 4", .{ .number = -1 } },
        .{ "if false do 3 - 4", .nil },
    };

    for (test_cases) |cs| {
        var scanner = try Scanner.init(cs[0]);
        var sexpr = try parser.expr(gpa, &scanner, 0);
        defer sexpr.deinit(gpa);

        var vm = VM.init();
        defer vm.deint(gpa);

        var compiler = Compiler.init();
        defer compiler.deinit(gpa);

        try compiler.compile(gpa, sexpr, &vm);
        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.items.len);
        try std.testing.expectEqual(cs[1], vm.stack.items[0]);
    }
}

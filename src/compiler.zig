const std = @import("std");
const VM = @import("vm.zig").VM;
const scanner_ = @import("scanner.zig");
const Token = scanner_.Token;
const Literal = Token.Literal;
const Operator = Token.Operator;
const Value = @import("value.zig").Value;
const SExpr = @import("sexpr.zig").SExpr;

pub const Compiler = struct {
    fn compileLiteral(gpa: std.mem.Allocator, literal: Literal, vm: *VM) !void {
        const value: Value = switch (literal) {
            .number => |n| .{ .number = n },
            .string => unreachable,
            .identifier => unreachable,
            .constant => |c| switch (c) {
                .@"true" => Value.True,
                .@"false" => Value.False,
                .nil => .nil,
            }
        };

        _ = try vm.addConstant(gpa, value);
    }

    fn compileOperator(gpa: std.mem.Allocator, op: Operator, args: []const SExpr, vm: *VM, line: usize) !void {
        const instruction = switch (op) {
            .plus => VM.Instructions.add,
            .minus => if (args.len == 1) VM.Instructions.negate else VM.Instructions.sub,
            .slash => VM.Instructions.div,
            .star => VM.Instructions.mul,
            else => unreachable,
        };

        for (args) |a| try compile(gpa, a, vm);
        try vm.addByte(gpa, @intFromEnum(instruction), line);
    }

    fn patchJump(ji: usize, vm: *VM) !void {
        const offset = vm.chunk.bytecode.items.len - ji;
        if (offset > std.math.maxInt(u16)) return error.JumpTooLong;
        vm.patchJump(ji, @intCast(offset));
    }

    fn compileIf(gpa: std.mem.Allocator, args: []const SExpr, vm: *VM) !void {
        // cond
        try compile(gpa, args[0], vm);

        // true branch
        const j1 = try vm.addJumpIfFalse(gpa, 0);

        // true branch
        try compile(gpa, args[1], vm);

        // false branch
        if (args.len == 3) {
            const j2 = try vm.addJump(gpa, 0);
            try patchJump(j1, vm);

            try compile(gpa, args[2], vm);
            try patchJump(j2, vm);
        } else {
            try patchJump(j1, vm);
            _ = try vm.addConstant(gpa, .nil);
        }
    }

    fn compileAtom(gpa: std.mem.Allocator, token: Token, vm: *VM) !void {
        switch (token.kind) {
            .literal => |literal| try compileLiteral(gpa, literal, vm),
            else => unreachable,
        }
    }

    fn compileCons(gpa: std.mem.Allocator, cons: []const SExpr, vm: *VM) !void {
        if (cons.len == 0) return;
        try switch (cons[0]) {
            .atom => |a| switch (a.kind) {
                .operator => |op| compileOperator(gpa, op, cons[1..], vm, a.line),
                .special_fns => |fn_| switch (fn_) {
                    .@"if" => compileIf(gpa, cons[1..], vm),
                    else => return error.NotImplemented,
                },
                else => unreachable,
            },
            .cons => |cs| {
                try compileCons(gpa, cs.items, vm);
                for (cons) |c| try compile(gpa, c, vm);
            }
        };
    }

    pub fn compile(gpa: std.mem.Allocator, sexpr: SExpr, vm: *VM) anyerror!void {
        try switch (sexpr) {
            .atom => |token| compileAtom(gpa, token, vm),
            .cons => |cons| compileCons(gpa, cons.items, vm),
        };
    }
};

test "if" {
    const parser = @import("parser.zig");
    const Scanner = @import("scanner.zig").Scanner;

    const gpa = std.testing.allocator;

    const test_cases = [_]struct{ []const u8, Value }{
        .{"if 1 + 2 do 3 - 4 else 5 - 7", .{ .number = -1 }},
        .{"if nil do 3 - 4 else 5 - 7", .{ .number = -2 }},
        .{"if false do 3 - 4", .nil},
    };

    for (test_cases) |cs| {
        var scanner = try Scanner.init(cs[0]);
        var sexpr = try parser.expr(gpa, &scanner, 0);
        defer sexpr.deinit(gpa);

        var vm = VM.init();
        defer vm.deint(gpa);

        try Compiler.compile(gpa, sexpr, &vm);
        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.items.len);
        try std.testing.expectEqual(cs[1], vm.stack.items[0]);
    }
}

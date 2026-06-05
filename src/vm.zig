const std = @import("std");
const Value = @import("value.zig").Value;

pub const VM = struct {
    bytecode: std.ArrayList(u8),
    stack: std.ArrayList(Value),
    lines: std.ArrayList(usize),

    pub fn init() VM {
        return .{
            .bytecode = .empty,
            .stack = .empty,
            .lines = .empty,
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

    pub const Instructions = enum {
        @"if",
        add,
        sub,
        mul,
        div,
        negate,
    };
};


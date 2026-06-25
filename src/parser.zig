const std = @import("std");
const scanner = @import("scanner.zig");
const Token = scanner.Token;
const Scanner = scanner.Scanner;
const Operator = Token.Operator;
const SExpr = @import("sexpr.zig").SExpr;

const Precedence = struct {
    left: u8,
    right: ?u8,
};

pub const Error = error{
    UnexpectedToken,
    EOF,
};

fn expectClose(s: *Scanner, open: Token, kind: Token.Kind) !void {
    expect(s, kind) catch |err| {
        const token = try s.peek() orelse Token{ .kind = .eof, .line = s.line };
        std.log.err("Unclosed token: {any}. Expected token {any}, got {any}", .{ open, kind, token });
        return err;
    };
}

fn expect(s: *Scanner, kind: Token.Kind) !void {
    _ = try expectToken(s, kind);
    return;
}

fn expectToken(s: *Scanner, kind: Token.Kind) !Token {
    const token = try s.next() orelse return Error.EOF;

    if (std.meta.eql(token.kind, kind)) {
        return token;
    }

    std.log.err("Unexpected token: {f}. Expected token {f}", .{ token, kind });
    return Error.UnexpectedToken;
}

fn peek(s: *Scanner, kind: Token.Kind) !bool {
    const token = try s.peek() orelse return false;
    return std.meta.eql(token.kind, kind);
}

fn check(s: *Scanner, kind: Token.Kind) !?Token {
    return if (try peek(s, kind))
        s.next() catch unreachable
    else
        null;
}

fn parseBracket(gpa: std.mem.Allocator, s: *Scanner, left_bracket: Token, lhs: SExpr) !SExpr {
    const rhs = try expr(gpa, s, 0);
    try expectClose(s, left_bracket, .right_bracket);

    var list: std.ArrayList(SExpr) = .empty;
    try list.appendSlice(gpa, &.{ .{ .atom = left_bracket }, lhs, rhs });

    return .{ .cons = list };
}

fn parseContainer(
    list: *std.ArrayList(SExpr),
    gpa: std.mem.Allocator,
    s: *Scanner,
    open_token: Token,
    close_kind: Token.Kind,
) !SExpr {
    if (try s.peek()) |t| if (std.meta.eql(t.kind, close_kind)) {
        _ = s.next() catch unreachable;
        return .{ .cons = list.* };
    };

    while (true) {
        try list.append(gpa, try expr(gpa, s, 5));
        if (try check(s, .{ .operator = .comma })) |_|
            continue;
        break;
    }

    try expectClose(s, open_token, close_kind);
    return .{ .cons = list.* };
}

fn parseParens(gpa: std.mem.Allocator, s: *Scanner, left_paren: Token, lhs: SExpr) !SExpr {
    var list: std.ArrayList(SExpr) = .empty;
    try list.append(gpa, lhs);

    return parseContainer(&list, gpa, s, left_paren, .right_paren);
}

fn parseList(gpa: std.mem.Allocator, s: *Scanner, left_bracket: Token, close_token: Token.Kind) !SExpr {
    var list: std.ArrayList(SExpr) = .empty;
    try list.append(gpa, .{
        .atom = .{
            .line = left_bracket.line,
            .kind = .{ .special_fns = .list },
        },
    });

    return parseContainer(&list, gpa, s, left_bracket, close_token);
}

fn parseIf(gpa: std.mem.Allocator, s: *Scanner, token: Token) !SExpr {
    const cond = try expr(gpa, s, 0);
    try expect(s, .{ .keywords = .do });

    const true_branch = try parseBlock(
        gpa,
        s,
        &[_]Token.Kind{ .{ .keywords = .@"else" }, .{ .keywords = .end } },
        token.line,
    );
    var list: std.ArrayList(SExpr) = .empty;
    try list.appendSlice(gpa, &.{ .{ .atom = token }, cond, true_branch });

    if (try check(s, .{ .keywords = .@"else" })) |else_token| {
        const false_branch = if (try check(s, .{ .special_fns = .@"if" })) |if_token|
            try parseIf(gpa, s, if_token)
        else
            try parseBlock(gpa, s, &[_]Token.Kind{.{ .keywords = .end }}, else_token.line);
        try list.append(gpa, false_branch);
    }

    return .{ .cons = list };
}

fn parseBlock(
    gpa: std.mem.Allocator,
    s: *Scanner,
    end_token_kinds: []const Token.Kind,
    line: usize,
) !SExpr {
    var list: std.ArrayList(SExpr) = .empty;
    try list.append(gpa, .{
        .atom = .{
            .kind = .{ .keywords = .do },
            .line = line,
        },
    });

    loop: while (true) {
        const e = try expr(gpa, s, 0);
        try list.append(gpa, e);

        for (end_token_kinds) |k|
            if (try peek(s, k))
                break :loop;
    }
    return .{ .cons = list };
}

fn parseOperator(gpa: std.mem.Allocator, s: *Scanner, start_token: Token, min_prec: u8) !SExpr {
    var lhs: SExpr = switch (start_token.kind) {
        .operator => |op| switch (op) {
            .left_paren => paren: {
                const ret = try expr(gpa, s, 0);
                try expect(s, .right_paren);
                break :paren ret;
            },
            .left_bracket => try parseList(gpa, s, start_token, .right_bracket),
            else => blk: {
                const prec = try prefixPrec(op);
                const rhs = try expr(gpa, s, prec.left);
                var list: std.ArrayList(SExpr) = .empty;
                try list.appendSlice(gpa, &.{ .{ .atom = start_token }, rhs });
                break :blk .{ .cons = list };
            },
        },
        .literal => .{ .atom = start_token },
        else => {
            std.log.err("Unexpected token {}", .{start_token});
            return Error.UnexpectedToken;
        },
    };

    while (try s.peek()) |token| {
        const op = switch (token.kind) {
            .operator => |op| op,
            .literal, .special_fns => {
                if (token.line != start_token.line) break;

                std.log.err("Line {}: Unexpected token {any}\n", .{ s.line, start_token });
                return Error.UnexpectedToken;
            },
            else => break,
        };

        const prec = try infixPrec(op);
        if (prec.left < min_prec)
            break;

        _ = s.next() catch unreachable;
        lhs = if (prec.right) |right| blk: {
            const rhs = try expr(gpa, s, right);
            var list: std.ArrayList(SExpr) = .empty;
            try list.appendSlice(gpa, &.{ .{ .atom = token }, lhs, rhs });
            break :blk .{ .cons = list };
        } else switch (op) {
            .left_paren => try parseParens(gpa, s, token, lhs),
            .left_bracket => try parseBracket(gpa, s, token, lhs),
            else => unreachable,
        };
    }

    return lhs;
}

pub fn expr(gpa: std.mem.Allocator, s: *Scanner, min_prec: u8) anyerror!SExpr {
    const token = try s.next() orelse {
        std.log.err("Line {}: Expected Token, got <EOF>", .{s.line});
        return Error.UnexpectedToken;
    };

    return switch (token.kind) {
        .special_fns => |fn_| switch (fn_) {
            .@"if" => parseIf(gpa, s, token),
            .list => parseList(gpa, s, try expectToken(s, .{ .operator = .left_paren }), .right_paren),
            else => error.NotImplemented,
        },
        else => parseOperator(gpa, s, token, min_prec) catch |err| {
            std.debug.print("Error at line {} token: {f}\n", .{ token.line, token.kind });
            return err;
        },
    };
}

fn prefixPrec(op: Operator) !Precedence {
    return switch (op) {
        .minus => .{ .left = 11, .right = null },
        else => error.OperatorNotPrefix,
    };
}

fn infixPrec(op: Operator) !Precedence {
    return switch (op) {
        .equal => .{ .left = 1, .right = 2 },
        .comma => .{ .left = 3, .right = 4 },
        .pipe => .{ .left = 6, .right = 5 },
        .plus, .minus => .{ .left = 7, .right = 8 },
        .star, .slash => .{ .left = 9, .right = 10 },
        .dot => .{ .left = 16, .right = 15 },
        .left_paren, .left_bracket => .{ .left = 13, .right = null },
        else => error.OperatorNotInfix,
    };
}

test "ok exprs" {
    const gpa = std.testing.allocator;
    const tests = [12]struct { []const u8, []const u8 }{
        .{ "1 * 2 + 3", "(+ (* 1 2) 3)" },
        .{ "0", "0" },
        .{ "(((0)))", "0" },
        .{ "- 1 * (2 + 3)", "(* (- 1) (+ 2 3))" },
        .{ "x.hwllo |> world()", "(|> (. x hwllo) (world))" },
        .{ "x[0][1]", "([ ([ x 0) 1)" },
        .{ "x, y = 1, 2", "(= (, x y) (, 1 2))" },
        .{ "world(1, 2, 3)", "(world 1 2 3)" },
        .{ "if x + 5 do y + 1 end", "(if (+ x 5) (do (+ y 1)))" },
        .{ "if x + 5 do y + 1 else z + 1 end", "(if (+ x 5) (do (+ y 1)) (do (+ z 1)))" },
        .{ "if x + 5 do 1 else z + 1 end", "(if (+ x 5) (do 1) (do (+ z 1)))" },
        .{ "if x + 5 do 1 else if x + 6 do z + 1 else k + 9 end", "(if (+ x 5) (do 1) (if (+ x 6) (do (+ z 1)) (do (+ k 9))))" },
    };

    for (tests) |t| {
        var test_scanner = try Scanner.init(t[0]);
        var sexpr = try expr(gpa, &test_scanner, 0);
        defer sexpr.deinit(gpa);

        var a: std.Io.Writer.Allocating = .init(gpa);
        defer a.deinit();
        try sexpr.format(&a.writer);

        const w = a.written();
        std.testing.expectEqualDeep(t[1], w) catch |err| {
            std.debug.print("Expected {s}; got {s}\n", .{ t[1], w });
            return err;
        };
    }
}

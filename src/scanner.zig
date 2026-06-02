const std = @import("std");

pub const Operator = enum {
    dot,
    minus,
    plus,
    pipe,
    star,
    slash,

    comma,
    left_paren,
    left_bracket,

    bang_equal,
    equal,
    equal_equal,
    greater,
    greater_equal,
    less,
    less_equal,
};

pub const SpecialFns = enum {
    class,
    def,
    do,
    @"for",
    @"if",
    map,
    mapf,
    match,
    reduce,
    @"while",
    import,
};

pub const Literal = union(enum) {
    identifier: []const u8,
    string: []const u8,
    number: f64,
};

pub const TokenKind = union(enum) {
    right_paren,
    right_brace,
    right_bracket,
    left_brace,
    semicolon,
    hash,

    operator: Operator,
    special_fns: SpecialFns,
    literal: Literal,

    @"and",
    @"else",
    end,
    false,
    in,
    nil,
    @"or",
    self,
    true,

    eof,

    pub fn print(t: TokenKind, writer: *std.Io.Writer) !void {
        switch (t) {
            .right_paren => try writer.writeAll(")"),
            .right_brace => try writer.writeAll("}"),
            .right_bracket => try writer.writeAll("]"),
            .left_brace => try writer.writeAll("{"),
            .semicolon => try writer.writeAll(";"),
            .hash => try writer.writeAll("#"),
            .operator => |op| try writer.writeAll(switch (op) {
                .dot => ".",
                .minus => "-",
                .plus => "+",
                .pipe => "|",
                .star => "*",
                .slash => "/",
                .comma => ",",
                .left_paren => "(",
                .left_bracket => "[",
                .bang_equal => "!=",
                .equal => "=",
                .equal_equal => "==",
                .greater => ">",
                .greater_equal => ">=",
                .less => "<",
                .less_equal => "<=",
            }),
            .special_fns => |fns| try writer.writeAll(@tagName(fns)),
            .literal => |lit| switch (lit) {
                .identifier => |s| try writer.writeAll(s),
                .string => |s| try writer.print("\"{s}\"", .{s}),
                .number => |n| try writer.print("{d}", .{n}),
            },
            .@"and", .@"else", .end, .false, .in, .nil, .@"or", .self, .true, .eof => try writer.writeAll(@tagName(t)),
        }
    }
};

pub const Token = struct {
    kind: TokenKind,
    line: usize,
};

pub const ErrorCtx = struct {
    reason: []const u8,
    line: usize,
};

pub const Error = error{
    UnclosedString,
    InvalidNumber,
    UnknownToken,
};

pub const Scanner = struct {
    line: usize,
    curr_index: usize,
    chars: []const u8,
    next_token: ?Token,
    err_ctx: ?ErrorCtx,
    err_buffer: [256]u8,

    pub fn init(chars: []const u8) Scanner {
        return .{
            .line = 1,
            .chars = chars,
            .curr_index = 0,
            .next_token = null,
            .err_ctx = null,
            .err_buffer = undefined,
        };
    }

    pub fn peek(s: *Scanner) !?Token {
        if (s.next_token) |next_token| {
            return next_token;
        }

        s.next_token = try s.next();
        return s.next_token;
    }

    fn next_char_if_eq(s: *Scanner, char: u8) bool {
        if (s.curr_index + 1 >= s.chars.len or
            s.chars[s.curr_index + 1] != char)
        {
            return false;
        }

        s.curr_index += 1;
        return true;
    }

    fn unknown_token_err(s: *Scanner, token: []const u8) Error {
        s.err_ctx = .{
            .reason = std.fmt.bufPrint(
                &s.err_buffer,
                "Unknown token: {s}",
                .{token},
            ) catch unreachable,
            .line = s.line,
        };
        return Error.UnknownToken;
    }

    fn curr_char(s: Scanner) u8 {
        return s.chars[s.curr_index];
    }

    fn string(s: *Scanner) !Token {
        s.curr_index += 1;
        const initial_i = s.curr_index;
        while (s.curr_index < s.chars.len and s.curr_char() != '"') : (s.curr_index += 1) {
            if (s.curr_char() == '\n') {
                s.line += 1;
            }
        }

        if (s.curr_char() != '"') {
            s.err_ctx = .{ .reason = "Unclosed string", .line = s.line };
            return Error.UnclosedString;
        }
        return Token{ .kind = .{ .literal = .{ .string = s.chars[initial_i..s.curr_index] } }, .line = s.line };
    }

    fn literal(s: *Scanner) Token {
        const initial_i = s.curr_index;
        while (s.curr_index < s.chars.len and (std.ascii.isAlphanumeric(s.curr_char()) or s.curr_char() == '_')) {
            s.curr_index += 1;
        }
        s.curr_index -= 1;

        const str = s.chars[initial_i .. s.curr_index + 1];
        const kind: TokenKind = if (std.mem.eql(u8, str, "and"))
            .@"and"
        else if (std.mem.eql(u8, str, "class"))
            .{ .special_fns = .class }
        else if (std.mem.eql(u8, str, "do"))
            .{ .special_fns = .do }
        else if (std.mem.eql(u8, str, "def"))
            .{ .special_fns = .def }
        else if (std.mem.eql(u8, str, "false"))
            .false
        else if (std.mem.eql(u8, str, "for"))
            .{ .special_fns = .@"for" }
        else if (std.mem.eql(u8, str, "if"))
            .{ .special_fns = .@"if" }
        else if (std.mem.eql(u8, str, "in"))
            .in
        else if (std.mem.eql(u8, str, "import"))
            .{ .special_fns = .import }
        else if (std.mem.eql(u8, str, "else"))
            .@"else"
        else if (std.mem.eql(u8, str, "end"))
            .end
        else if (std.mem.eql(u8, str, "map"))
            .{ .special_fns = .map }
        else if (std.mem.eql(u8, str, "mapf"))
            .{ .special_fns = .mapf }
        else if (std.mem.eql(u8, str, "match"))
            .{ .special_fns = .match }
        else if (std.mem.eql(u8, str, "nil"))
            .nil
        else if (std.mem.eql(u8, str, "or"))
            .@"or"
        else if (std.mem.eql(u8, str, "reduce"))
            .{ .special_fns = .reduce }
        else if (std.mem.eql(u8, str, "true"))
            .true
        else if (std.mem.eql(u8, str, "self"))
            .self
        else if (std.mem.eql(u8, str, "while"))
            .{ .special_fns = .@"while" }
        else
            .{ .literal = .{ .identifier = str } };

        return .{ .kind = kind, .line = s.line };
    }

    fn number(s: *Scanner) !Token {
        const initial_i = s.curr_index;
        var has_dot = false;
        while (s.curr_index < s.chars.len and (std.ascii.isDigit(s.curr_char()) or s.curr_char() == '.')) : (s.curr_index += 1) {
            if (s.curr_char() == '.') {
                if (has_dot) {
                    s.err_ctx = ErrorCtx{
                        .reason = try std.fmt.bufPrint(
                            &s.err_buffer,
                            "Invalid number literal {s}",
                            .{s.chars[initial_i..s.curr_index]},
                        ),
                        .line = s.line,
                    };
                    return Error.InvalidNumber;
                }

                has_dot = true;
            }
        }

        const v = std.fmt.parseFloat(f64, s.chars[initial_i..s.curr_index]) catch unreachable;
        s.curr_index -= 1;
        return .{ .kind = .{ .literal = .{ .number = v } }, .line = s.line };
    }

    pub fn next(s: *Scanner) !?Token {
        if (s.next_token) |next_token| {
            s.next_token = null;
            return next_token;
        }

        if (s.curr_index >= s.chars.len)
            return null;

        const token: Token = switch (s.curr_char()) {
            '(' => .{ .kind = .{ .operator = .left_paren }, .line = s.line },
            ')' => .{ .kind = .right_paren, .line = s.line },
            '[' => .{ .kind = .{ .operator = .left_bracket }, .line = s.line },
            ']' => .{ .kind = .right_bracket, .line = s.line },
            '{' => .{ .kind = .left_brace, .line = s.line },
            '}' => .{ .kind = .right_brace, .line = s.line },
            '+' => .{ .kind = .{ .operator = .plus }, .line = s.line },
            '-' => .{ .kind = .{ .operator = .minus }, .line = s.line },
            '*' => .{ .kind = .{ .operator = .star }, .line = s.line },
            '/' => .{ .kind = .{ .operator = .slash }, .line = s.line },
            '#' => .{ .kind = .hash, .line = s.line },
            '.' => .{ .kind = .{ .operator = .dot }, .line = s.line },
            ',' => .{ .kind = .{ .operator = .comma }, .line = s.line },
            ';' => .{ .kind = .semicolon, .line = s.line },
            '|' => if (s.next_char_if_eq('>'))
                .{ .kind = .{ .operator = .pipe }, .line = s.line }
            else
                return s.unknown_token_err(s.chars[s.curr_index .. s.curr_index + 2]),
            '!' => if (s.next_char_if_eq('='))
                .{ .kind = .{ .operator = .bang_equal }, .line = s.line }
            else
                return s.unknown_token_err(s.chars[s.curr_index .. s.curr_index + 2]),
            '>' => if (s.next_char_if_eq('='))
                .{ .kind = .{ .operator = .greater_equal }, .line = s.line }
            else
                .{ .kind = .{ .operator = .greater }, .line = s.line },
            '<' => if (s.next_char_if_eq('='))
                .{ .kind = .{ .operator = .less_equal }, .line = s.line }
            else
                .{ .kind = .{ .operator = .less }, .line = s.line },
            '=' => if (s.next_char_if_eq('='))
                .{ .kind = .{ .operator = .equal_equal }, .line = s.line }
            else
                .{ .kind = .{ .operator = .equal }, .line = s.line },
            '"' => try s.string(),
            else => blk: {
                const c = s.curr_char();
                break :blk if (std.ascii.isDigit(c))
                    try s.number()
                else if (std.ascii.isWhitespace(c)) {
                    if (c == '\n') s.line += 1;
                    s.curr_index += 1;
                    return s.next();
                } else if (std.ascii.isAlphabetic(c) or c == '_')
                    s.literal()
                else
                    return s.unknown_token_err(s.chars[s.curr_index .. s.curr_index + 1]);
            },
        };

        s.curr_index += 1;
        return token;
    }
};

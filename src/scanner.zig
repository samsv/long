const std = @import("std");
const Utf8View = std.unicode.Utf8View;
const Utf8Iterator = std.unicode.Utf8Iterator;
const alphabetic_ranges = @import("unicode_alphabetic_table.zig").alphabetic_ranges;

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
                .pipe => "|>",
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
    view: Utf8Iterator,
    next_token: ?Token,
    err_ctx: ?ErrorCtx,
    err_buffer: [256]u8,

    pub fn init(chars: []const u8) !Scanner {
        return .{
            .line = 1,
            .view = Utf8View.iterator(try Utf8View.init(chars)),
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

    fn nextCharIfEq(s: *Scanner, char: u21) bool {
        const next_codepoint = s.view.peek(1);
        if (next_codepoint.len == 0)
            return false;

        if (std.unicode.utf8Decode(next_codepoint) catch unreachable != char)
            return false;

        _ = s.view.nextCodepoint();
        return true;
    }

    fn unknownTokenErr(s: *Scanner, token: []const u8) Error {
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

    fn invalidNumberErr(s: *Scanner, number_str: []const u8) anyerror {
        const str = if (number_str.len < 200) number_str else number_str[0..200];
        s.err_ctx = ErrorCtx{
            .reason = try std.fmt.bufPrint(
                &s.err_buffer,
                "Invalid number literal {s}",
                .{str},
            ),
            .line = s.line,
        };
        return Error.InvalidNumber;
    }

    fn string(s: *Scanner) !Token {
        const initial_i = s.view.i;
        while (s.view.nextCodepoint()) |c| {
            if (c == '"') break;

            if (c == '\n')
                s.line += 1;
        } else {
            s.err_ctx = .{ .reason = "Unclosed string", .line = s.line };
            return Error.UnclosedString;
        }

        return Token{
            .kind = .{ .literal = .{ .string = s.view.bytes[initial_i .. s.view.i - 1] } },
            .line = s.line,
        };
    }

    fn literal(s: *Scanner, initial_i: usize) Token {
        var last_i = s.view.i;
        while (s.view.nextCodepoint()) |c| {
            if (!isAlphanumeric(c) and c != '_')
                break;

            last_i = s.view.i;
        }

        s.view.i = last_i;

        const str = s.view.bytes[initial_i..last_i];
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

    fn number(s: *Scanner, initial_i: usize) !Token {
        var has_dot = false;
        var last_i = initial_i + 1;
        var last_point: u21 = 0;
        while (s.view.nextCodepoint()) |c| : (last_point = c) {
            if (isDigit(c)) {
                last_i = s.view.i;
                continue;
            }

            if (c != '.') {
                if (last_point != '.')
                    break
                else
                    return s.invalidNumberErr(s.view.bytes[initial_i..s.view.i]);
            }

            if (has_dot) {
                return s.invalidNumberErr(s.view.bytes[initial_i..s.view.i]);
            }

            has_dot = true;
        }

        s.view.i = last_i;
        const v = std.fmt.parseFloat(f64, s.view.bytes[initial_i..last_i]) catch unreachable;
        return .{ .kind = .{ .literal = .{ .number = v } }, .line = s.line };
    }

    pub fn next(s: *Scanner) !?Token {
        if (s.next_token) |next_token| {
            s.next_token = null;
            return next_token;
        }

        var initial_i = s.view.i;
        var c = s.view.nextCodepoint() orelse return null;

        while (isWhitespace(c)) {
            if (c == '\n') s.line += 1;
            initial_i = s.view.i;
            c = s.view.nextCodepoint() orelse return null;
        }

        const token: Token = switch (c) {
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
            '|' => if (s.nextCharIfEq('>'))
                .{ .kind = .{ .operator = .pipe }, .line = s.line }
            else {
                _ = s.view.nextCodepoint();
                return s.unknownTokenErr(s.view.bytes[initial_i..s.view.i]);
            },
            '!' => if (s.nextCharIfEq('='))
                .{ .kind = .{ .operator = .bang_equal }, .line = s.line }
            else {
                _ = s.view.nextCodepoint();
                return s.unknownTokenErr(s.view.bytes[initial_i..s.view.i]);
            },
            '>' => if (s.nextCharIfEq('='))
                .{ .kind = .{ .operator = .greater_equal }, .line = s.line }
            else
                .{ .kind = .{ .operator = .greater }, .line = s.line },
            '<' => if (s.nextCharIfEq('='))
                .{ .kind = .{ .operator = .less_equal }, .line = s.line }
            else
                .{ .kind = .{ .operator = .less }, .line = s.line },
            '=' => if (s.nextCharIfEq('='))
                .{ .kind = .{ .operator = .equal_equal }, .line = s.line }
            else
                .{ .kind = .{ .operator = .equal }, .line = s.line },
            '"' => try s.string(),
            else => if (isDigit(c))
                try s.number(initial_i)
            else if (isAlphabetic(c) or c == '_')
                s.literal(initial_i)
            else
                return s.unknownTokenErr(s.view.bytes[initial_i..s.view.i]),
        };

        return token;
    }
};

fn isDigit(c: u21) bool {
    return c >= '0' and c <= '9';
}

fn isAlphabetic(c: u21) bool {
    if (c < 0x80) return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z');

    // Binary search the sorted, non-overlapping Unicode Alphabetic ranges.
    var lo: usize = 0;
    var hi: usize = alphabetic_ranges.len;
    while (lo < hi) {
        const mid = lo + (hi - lo) / 2;
        const range = alphabetic_ranges[mid];
        if (c < range[0]) {
            hi = mid;
        } else if (c > range[1]) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

fn isAlphanumeric(c: u21) bool {
    return isDigit(c) or isAlphabetic(c);
}

// Matches Rust's char::is_whitespace: the Unicode White_Space property
// (PropList.txt, Unicode 16.0.0). This set is small and effectively frozen.
fn isWhitespace(c: u21) bool {
    return switch (c) {
        ' ',
        '\t',
        '\n',
        '\r',
        '\u{000B}', // vertical tab
        '\u{000C}', // form feed
        '\u{0085}', // next line (NEL)
        '\u{00A0}', // no-break space
        '\u{1680}', // ogham space mark
        '\u{2000}'...'\u{200A}', // en quad .. hair space
        '\u{2028}', // line separator
        '\u{2029}', // paragraph separator
        '\u{202F}', // narrow no-break space
        '\u{205F}', // medium mathematical space
        '\u{3000}', // ideographic space
        => true,
        else => false,
    };
}

test "isAlphabetic matches the Unicode Alphabetic property" {
    const expect = std.testing.expect;
    try expect(isAlphabetic('A') and isAlphabetic('z'));
    try expect(!isAlphabetic('1') and !isAlphabetic('_') and !isAlphabetic(' '));
    try expect(isAlphabetic(0x00E9)); // é  (Latin-1)
    try expect(isAlphabetic(0x03A9)); // Ω  (Greek)
    try expect(isAlphabetic(0x4E2D)); // 中 (CJK)
    try expect(isAlphabetic(0x05D0)); // א  (Hebrew)
    try expect(isAlphabetic(0x10000)); // 𐀀 (astral-plane letter)
    try expect(!isAlphabetic(0x0669)); // ٩  Arabic-Indic digit (not alphabetic)
    // Range boundaries (Latin-1 supplement block 0x00C0..0x00D6, then × at 0x00D7).
    try expect(isAlphabetic(0x00C0) and isAlphabetic(0x00D6) and !isAlphabetic(0x00D7));
}

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
    left_brace,
    right_brace,
    right_bracket,
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
    then,

    eof,
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
    UnknownCharacter,
};

pub const Scanner = struct {
    line: usize,
    curr_index: usize,
    chars: []const u8,
    next_token: ?Token,
    err_ctx: ?ErrorCtx,

    pub fn init(chars: []const u8) Scanner {
        return .{
            .line = 1,
            .chars = chars,
            .curr_index = 0,
            .next_token = null,
            .err_ctx = null,
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

    fn unknown_char_err(s: *Scanner, reason: []const u8) Error {
        s.err_ctx = .{ .reason = reason, .line = s.line };
        return Error.UnknownCharacter;
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
        return Token { .kind = .{ .literal = .{ .string = s.chars[initial_i..s.curr_index] } }, .line = s.line };
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
                return s.unknown_char_err("Unknown characted" ++ .{ '|', s.chars[s.curr_index] }),
            '!' => if (s.next_char_if_eq('='))
                .{ .kind = .{ .operator = .bang_equal }, .line = s.line }
            else
                return s.unknown_char_err("Unknown characted" ++ .{ '|', s.chars[s.curr_index] }),
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
            else => unreachable,
        };

        s.curr_index += 1;
        return token;
    }
};

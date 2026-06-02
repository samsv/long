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

    bange_qual,
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
    @"import",
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
    @"false",
    in,
    nil,
    @"or",
    self,
    @"true",
    then,

    eof,
};

pub const Token = struct {
    kind: TokenKind,
    line: usize,
};

pub const ErrorData = struct {
    reason: []const u8,
    line: usize,
};

pub const Error = error {
    UnclosedString,
};

pub const Scanner = struct {
    line: usize,
    current_index: usize,
    chars: []const u8,
    next_token: ?Token,

    pub fn init(chars: []const u8) Scanner {
        return .{
            .line = 1,
            .chars = chars,
            .current_index = 0,
            .next_token = null,
        };
    }

    pub fn peek(s: *Scanner) !?Token {
        if (s.next_token) |next_token| {
            return next_token;
        }

        s.next_token = try s.next();
        return s.next_token;
    }

    pub fn next(s: *Scanner) !?Token {
        if (s.next_token) |next_token| {
            s.next_token = null;
            return next_token;
        }

        if (s.current_index >= s.chars.len)
            return null;

        const token: Token = switch (s.chars[s.current_index]) {
            '(' => .{ .kind = .{ .operator = .left_paren }, .line = s.line },
            else => unreachable,
        };

        return token;
    }
};

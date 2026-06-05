pub const Value = union(enum) {
    number: f64,
    boolean: bool,
    nil,

    pub const True: Value = .{ .boolean = true };
    pub const False: Value = .{ .boolean = false };
};

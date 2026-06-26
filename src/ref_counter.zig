const std = @import("std");

const borrow_errors = error{
    AccessFreedReference,
    BorrowOfFreedReference,
};

pub fn RC(comptime T: type) type {
    return struct {
        inner: ?*Inner,

        const Self = @This();

        pub fn init(gpa: std.mem.Allocator, value: T) !Self {
            return Inner.init(gpa, value);
        }

        pub fn get(self: Self) !T {
            if (self.inner) |inner| {
                return inner.value;
            }

            return error.FreedReference;
        }

        pub fn getPtr(self: Self) !*T {
            if (self.inner) |inner| {
                return &inner.value;
            }

            return error.FreedReference;
        }

        pub fn getUnwrap(self: Self) T {
            return self.inner.?.value;
        }

        pub fn getPtrUnwrap(self: Self) *T {
            return &self.inner.?.value;
        }

        pub fn borrow(self: Self) !Self {
            const value = self.inner orelse return error.BorrowOfFreedReference;
            return .{ .inner = value._borrow() };
        }

        pub fn deinit(self: *Self, gpa: std.mem.Allocator) void {
            var value = self.inner orelse return;
            value._deinit(gpa);
            self.inner = null;
        }

        pub fn deinitWithCb(self: *Self, gpa: std.mem.Allocator, fun: *const fn(*T, gpa: std.mem.Allocator) void,) void {
            var inner = self.inner orelse return;
            fun(&inner.value, gpa);
            inner._deinit(gpa);
            self.inner = null;
        }

        pub fn count(self: Self) usize {
            const v = self.inner orelse return 1;
            return v.count;
        }

        const Inner = struct {
            value: T,
            count: usize = 1,

            pub fn init(gpa: std.mem.Allocator, value: T) !Self {
                const U = switch (@typeInfo(T)) {
                    .pointer => |info| info.child,
                    else => T,
                };

                if (!std.meta.hasFn(U, "deinit")) {
                    @compileError("Value must have deinit function.");
                }

                const method = @field(U, "deinit");
                const expected_signature = fn (*U, std.mem.Allocator) void;
                if (@TypeOf(method) != expected_signature) {
                    @compileError("deinit function must accept a self pointer and an allocator");
                }

                const self = try gpa.create(Inner);
                self.* = .{ .value = value };
                return .{ .inner = self };
            }

            fn _borrow(self: *Inner) *Inner {
                self.count += 1;
                return self;
            }

            fn _deinit(self: *Inner, gpa: std.mem.Allocator) void {
                self.count -= 1;
                if (self.count == 0) {
                    self.value.deinit(gpa);
                    gpa.destroy(self);
                }
            }
        };
    };
}

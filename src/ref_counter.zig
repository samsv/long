const std = @import("std");

const borrow_errors = error{
    AccessFreedReference,
    BorrowOfFreedReference,
};

pub fn RefCounter(comptime T: type) type {
    return struct {
        value: T,
        count: usize = 1,

        const Self = @This();

        pub const Ref = struct {
            value: ?*Self,

            pub fn init(gpa: std.mem.Allocator, value: T) !Ref {
                return RefCounter(T).init(gpa, value);
            }

            pub fn get(self: Ref) !T {
                if (self.value) |value| {
                    return value.value;
                }

                return error.FreedReference;
            }

            pub fn getPtr(self: Ref) !*T {
                if (self.value) |value| {
                    return &value.value;
                }

                return error.FreedReference;
            }

            pub fn getUnwrap(self: Ref) T {
                return self.value.?.value;
            }

            pub fn getPtrUnwrap(self: Ref) *T {
                return &self.value.?.value;
            }

            pub fn borrow(self: Ref) !Ref {
                const value = self.value orelse return error.BorrowOfFreedReference;
                return Ref{ .value = value._borrow() };
            }

            pub fn deinit(self: *Ref, gpa: std.mem.Allocator) void {
                var value = self.value orelse return;
                value._deinit(gpa);
                self.value = null;
            }
        };

        pub fn init(gpa: std.mem.Allocator, value: T) !Ref {
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

            const self = try gpa.create(Self);
            self.* = Self{ .value = value };
            return Ref{ .value = self };
        }

        fn _borrow(self: *Self) *Self {
            self.count += 1;
            return self;
        }

        fn _deinit(self: *Self, gpa: std.mem.Allocator) void {
            self.count -= 1;
            if (self.count == 0) {
                self.value.deinit(gpa);
                gpa.destroy(self);
            }
        }
    };
}

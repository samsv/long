#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "compiler.h"
#include "obj/map.h"
#include "scanner.h"
#include "sexpr.h"
#include "parser.h"
#include "std_native.h"

compiler_t compiler_init(void);
void compiler_free(compiler_t* c, const sv_allocator_t* a);
bool compile_sexpr(compiler_t* c, sexpr_t sexpr, ctx_t* ctx);

#define TRY(call) do { if (!(call)) return false; } while (0)

static bool compiler_error(ctx_t* ctx, compiler_error_kind kind, const char* msg)
{
    return error_set(&ctx->err, (int)kind, msg, &ctx->alloc);
}

static bool compiler_oom(ctx_t* ctx, int64_t line)
{
    return error_set_oom(&ctx->err, (int)C_ERR_OOM, line, &ctx->alloc);
}

static bool compiler_error_name(ctx_t* ctx, compiler_error_kind kind, int64_t line, const char* what, sv_str_t id)
{
    char msg[192];
    snprintf(msg, sizeof(msg), "%s '%.*s' at line %" PRId64, what, (int)id.size, id.chars, line);
    return compiler_error(ctx, kind, msg);
}

static bool compiler_malformed(ctx_t* ctx, const char* what, int64_t line)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "Malformed %s expression at line %" PRId64, what, line);
    return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
}

static bool emit(compiler_t* c, ctx_t* ctx, uint8_t byte, int64_t line)
{
    if (!vmb_add_byte(&c->builder, byte, line, &ctx->alloc))
        return compiler_oom(ctx, line);
    return true;
}

static bool emit2(compiler_t* c, ctx_t* ctx, uint8_t b1, uint8_t b2, int64_t line)
{
    if (!vmb_add_bytes(&c->builder, b1, b2, line, &ctx->alloc))
        return compiler_oom(ctx, line);
    return true;
}

static bool add_const(compiler_t* c, ctx_t* ctx, value_t v, int64_t line)
{
    sv_opt_t(uint8_t) i = vmb_add_constant(&c->builder, v, &ctx->alloc);
    if (!i.is_some) {
        value_free(&v, &ctx->alloc);
        return compiler_oom(ctx, line);
    }
    return true;
}

static bool jump_emit(ctx_t* ctx, sv_opt_t(int64_t) ji, int64_t* out, int64_t line)
{
    if (!ji.is_some)
        return compiler_oom(ctx, line);
    *out = ji.value;
    return true;
}

static bool patch_jump(compiler_t* c, ctx_t* ctx, int64_t ji, int64_t line)
{
    int64_t offset = c->builder.vm.chunk.bytecode.size - ji;
    if (offset > UINT16_MAX) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Jump too long at line %" PRId64, line);
        return compiler_error(ctx, C_ERR_JUMP_TOO_LONG, msg);
    }
    vmb_patch_jump(&c->builder, ji, (uint16_t)offset);
    return true;
}

static int64_t names_count(transient_hashmap_t names)
{
    return names.set.dense.cell != NULL ? thm_count(names) : 0;
}

static sv_opt_t(int64_t) names_get(transient_hashmap_t names, sv_str_t id, ctx_t* ctx)
{
    if (names.set.dense.cell == NULL)
        return sv_opt_none_t(int64_t);

    value_t key = value_init_str(id, &ctx->alloc);
    if (key.obj.cell == NULL)
        return sv_opt_none_t(int64_t);

    sv_opt_t(value_t) v = thm_get(names, key);
    value_free(&key, &ctx->alloc);
    if (!v.is_some)
        return sv_opt_none_t(int64_t);
    return sv_opt_some_t(int64_t, (int64_t)v.value.number);
}

static bool names_add(transient_hashmap_t* names, sv_str_t id, ctx_t* ctx, bool* existed)
{
    if (names->set.dense.cell == NULL) {
        *names = thm_init(4, &ctx->alloc);
        if (names->set.dense.cell == NULL)
            return false;
    }

    *existed = names_get(*names, id, ctx).is_some;
    if (*existed)
        return true;

    value_t key = value_init_str(id, &ctx->alloc);
    if (key.obj.cell == NULL)
        return false;

    kv_t kv = { .key = key, .value = { .kind = VALUE_NUMBER, .number = (double)thm_count(*names) } };
    bool ok = thm_put(names, kv, &ctx->alloc);
    value_free(&key, &ctx->alloc);
    return ok;
}

static bool globals_add(globals_t* g, sv_str_t id, ctx_t* ctx, int64_t line)
{
    bool existed = false;
    if (!names_add(&g->name_indexes, id, ctx, &existed))
        return compiler_oom(ctx, line);
    if (existed)
        return compiler_error_name(ctx, C_ERR_REDEFINED, line, "Global", id);
    return true;
}

static bool locals_add(locals_t* l, sv_str_t id, ctx_t* ctx, int64_t line)
{
    bool existed = false;
    if (!names_add(&l->name_indexes, id, ctx, &existed))
        return compiler_oom(ctx, line);
    if (existed)
        return compiler_error_name(ctx, C_ERR_REDEFINED, line, "Local", id);
    return true;
}

static sv_opt_t(int64_t) locals_get(const locals_t* l, sv_str_t id, ctx_t* ctx)
{
    for (; l != NULL; l = l->next) {
        sv_opt_t(int64_t) idx = names_get(l->name_indexes, id, ctx);
        if (idx.is_some)
            return sv_opt_some_t(int64_t, idx.value + l->offset);
    }
    return sv_opt_none_t(int64_t);
}

static bool expect_id(sexpr_t e, ctx_t* ctx, sv_str_t* out)
{
    if (e.tag != S_ATOM || e.atom.kind != TOKEN_LITERAL || e.atom.literal.kind != LITERAL_IDENTIFIER) {
        int64_t line = e.tag == S_ATOM ? e.atom.line : 0;
        char msg[96];
        snprintf(msg, sizeof(msg), "Expected an identifier at line %" PRId64, line);
        return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
    }
    *out = e.atom.literal.literal;
    return true;
}

compiler_t compiler_init(void)
{
    return (compiler_t){
        .globals = { .name_indexes = { .depth = 0 } },
        .upvalues = { .name_indexes = { .depth = 0 }, .next = NULL, .offset = 0 },
        .locals = NULL,
        .members = { .depth = 0 },
        .record_fields = NULL,
        .builder = { .vm = vm_init(sv_str_init("")) },
    };
}

void compiler_free(compiler_t* c, const sv_allocator_t* a)
{
    thm_deinit(&c->globals.name_indexes, a);
    thm_deinit(&c->upvalues.name_indexes, a);
    thm_deinit(&c->members, a);
    while (c->locals != NULL) {
        locals_t* l = c->locals;
        c->locals = l->next;
        thm_deinit(&l->name_indexes, a);
        sv_free(a, l);
    }
}

static bool compile_id(compiler_t* c, sv_str_t id, int64_t line, ctx_t* ctx)
{
    sv_opt_t(int64_t) idx = locals_get(c->locals, id, ctx);
    if (idx.is_some)
        return emit2(c, ctx, OP_GET_LOCAL, (uint8_t)idx.value, line);

    idx = locals_get(&c->upvalues, id, ctx);
    if (idx.is_some)
        return emit2(c, ctx, OP_GET_UPVALUE, (uint8_t)idx.value, line);

    idx = names_get(c->globals.name_indexes, id, ctx);
    if (idx.is_some)
        return emit2(c, ctx, OP_GET_GLOBAL, (uint8_t)idx.value, line);

    idx = names_get(c->members, id, ctx);
    if (idx.is_some)
        return emit2(c, ctx, OP_GET_MEMBER, (uint8_t)idx.value, line);

    return compiler_error_name(ctx, C_ERR_UNDEFINED_VARIABLE, line, "Variable", id);
}

static bool add_var(compiler_t* c, sv_str_t id, int64_t line, ctx_t* ctx)
{
    if (c->locals != NULL) {
        TRY(emit(c, ctx, OP_SET_LOCAL, line));
        TRY(locals_add(c->locals, id, ctx, line));
        sv_opt_t(int64_t) idx = locals_get(c->locals, id, ctx);
        return emit2(c, ctx, OP_GET_LOCAL, (uint8_t)idx.value, line);
    }
    TRY(emit(c, ctx, OP_SET_GLOBAL, line));
    TRY(globals_add(&c->globals, id, ctx, line));
    sv_opt_t(int64_t) idx = names_get(c->globals.name_indexes, id, ctx);
    return emit2(c, ctx, OP_GET_GLOBAL, (uint8_t)idx.value, line);
}

static bool init_scope(compiler_t* c, ctx_t* ctx, int64_t line)
{
    locals_t* local = sv_malloc(&ctx->alloc, sizeof(locals_t));
    if (local == NULL)
        return compiler_oom(ctx, line);
    *local = (locals_t){
        .name_indexes = { .depth = 0 },
        .next = c->locals,
        .offset = c->locals != NULL ? names_count(c->locals->name_indexes) + c->locals->offset : 0,
    };
    c->locals = local;
    return true;
}

static bool deinit_scope(compiler_t* c, ctx_t* ctx)
{
    locals_t* local = c->locals;
    if (local == NULL)
        return true;

    uint8_t n = (uint8_t)names_count(local->name_indexes);
    c->locals = local->next;
    thm_deinit(&local->name_indexes, &ctx->alloc);
    sv_free(&ctx->alloc, local);
    return emit2(c, ctx, OP_POP_LOCAL, n, 0);
}

static bool compile_equal(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "assignment", line);
    sv_str_t id;
    TRY(expect_id(args[0], ctx, &id));
    TRY(compile_sexpr(c, args[1], ctx));
    return add_var(c, id, line, ctx);
}

static bool compile_pipe(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "pipe", line);
    TRY(compile_sexpr(c, args[0], ctx));

    sexpr_t rhs = args[1];
    if (rhs.tag != S_CONS || rhs.cons.size == 0)
        return compiler_malformed(ctx, "pipe", line);

    sv_str_t fn_name;
    TRY(expect_id(rhs.cons.arr[0], ctx, &fn_name));
    for (int64_t i = 1; i < rhs.cons.size; i++)
        TRY(compile_sexpr(c, rhs.cons.arr[i], ctx));
    TRY(compile_id(c, fn_name, line, ctx));
    return emit2(c, ctx, OP_CALL, (uint8_t)rhs.cons.size, line);
}

static bool record_field_id(compiler_t* c, sv_str_t name, int64_t line, ctx_t* ctx, uint32_t* out)
{
    bool existed = false;
    if (!names_add(c->record_fields, name, ctx, &existed))
        return compiler_oom(ctx, line);

    sv_opt_t(int64_t) id = names_get(*c->record_fields, name, ctx);
    if (id.value > UINT8_MAX) {
        char msg[96];
        snprintf(msg, sizeof(msg), "More than %d record fields at line %" PRId64, UINT8_MAX + 1, line);
        return compiler_error(ctx, C_ERR_NOT_IMPLEMENTED, msg);
    }
    *out = (uint32_t)id.value;
    return true;
}

static bool compile_tuple(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n < 1 || n > UINT8_MAX)
        return compiler_malformed(ctx, "tuple", line);

    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], ctx));
    return emit2(c, ctx, OP_TUPLE, (uint8_t)n, line);
}

static bool compile_record(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n % 2 != 0 || n / 2 > UINT8_MAX)
        return compiler_malformed(ctx, "record", line);

    struct { uint32_t id; sv_str_t name; const sexpr_t* value; } fields[UINT8_MAX];
    int64_t n_fields = n / 2;
    for (int64_t i = 0; i < n_fields; i++) {
        sv_str_t name;
        TRY(expect_id(args[2 * i], ctx, &name));
        uint32_t id = 0;
        TRY(record_field_id(c, name, line, ctx, &id));

        int64_t j = i;
        for (; j > 0 && fields[j - 1].id > id; j--)
            fields[j] = fields[j - 1];
        fields[j].id = id;
        fields[j].name = name;
        fields[j].value = &args[2 * i + 1];
    }

    for (int64_t i = 1; i < n_fields; i++)
        if (fields[i - 1].id == fields[i].id)
            return compiler_error_name(ctx, C_ERR_REDEFINED, line, "Record field", fields[i].name);

    for (int64_t i = 0; i < n_fields; i++) {
        TRY(add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = (double)fields[i].id }, line));
        TRY(compile_sexpr(c, *fields[i].value, ctx));
    }
    return emit2(c, ctx, OP_RECORD, (uint8_t)n_fields, line);
}

static bool compile_dot(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "field access", line);

    sv_str_t name;
    TRY(expect_id(args[1], ctx, &name));
    uint32_t id = 0;
    TRY(record_field_id(c, name, line, ctx, &id));

    TRY(compile_sexpr(c, args[0], ctx));
    return emit2(c, ctx, OP_RECORD_GET, (uint8_t)id, line);
}

static bool compile_binary_op(compiler_t* c, uint8_t instruction, const char* what,
                              const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, what, line);
    TRY(compile_sexpr(c, args[0], ctx));
    TRY(compile_sexpr(c, args[1], ctx));
    return emit(c, ctx, instruction, line);
}

static bool compile_operator(compiler_t* c, operator_kind op, const sexpr_t* args,
                             int64_t n, int64_t line, ctx_t* ctx)
{
    uint8_t instruction = 0;
    switch (op) {
        case OPERATOR_PLUS: instruction = OP_ADD; break;
        case OPERATOR_MINUS: instruction = n == 1 ? OP_NEGATE : OP_SUB; break;
        case OPERATOR_SLASH: instruction = OP_DIV; break;
        case OPERATOR_STAR: instruction = OP_MUL; break;
        case OPERATOR_EQUAL: return compile_equal(c, args, n, line, ctx);
        case OPERATOR_EQUAL_EQUAL: return compile_binary_op(c, OP_EQUALS, "comparison", args, n, line, ctx);
        case OPERATOR_BANG_EQUAL: return compile_binary_op(c, OP_NOT_EQUALS, "comparison", args, n, line, ctx);
        case OPERATOR_GREATER: return compile_binary_op(c, OP_GREATER, "comparison", args, n, line, ctx);
        case OPERATOR_GREATER_EQUAL: return compile_binary_op(c, OP_GREATER_EQUAL, "comparison", args, n, line, ctx);
        case OPERATOR_LESS: return compile_binary_op(c, OP_LESS, "comparison", args, n, line, ctx);
        case OPERATOR_LESS_EQUAL: return compile_binary_op(c, OP_LESS_EQUAL, "comparison", args, n, line, ctx);
        case OPERATOR_LEFT_BRACKET: return compile_binary_op(c, OP_INDEX, "index", args, n, line, ctx);
        case OPERATOR_PIPE_FORWARD: return compile_pipe(c, args, n, line, ctx);
        case OPERATOR_DOT: return compile_dot(c, args, n, line, ctx);
        case OPERATOR_LEFT_PAREN: {
            char msg[96];
            snprintf(msg, sizeof(msg), "Operator not implemented at line %" PRId64, line);
            return compiler_error(ctx, C_ERR_NOT_IMPLEMENTED, msg);
        }
    }

    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], ctx));
    return emit(c, ctx, instruction, line);
}

static bool compile_if(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2 && n != 3)
        return compiler_malformed(ctx, "if", line);

    TRY(init_scope(c, ctx, line));
    TRY(compile_sexpr(c, args[0], ctx));

    int64_t j1 = 0;
    TRY(jump_emit(ctx, vmb_add_jump_if_false(&c->builder, line, &ctx->alloc), &j1, line));
    TRY(compile_sexpr(c, args[1], ctx));

    int64_t j2 = 0;
    TRY(jump_emit(ctx, vmb_add_jump(&c->builder, line, &ctx->alloc), &j2, line));
    TRY(patch_jump(c, ctx, j1, line));
    if (n == 3)
        TRY(compile_sexpr(c, args[2], ctx));
    else
        TRY(add_const(c, ctx, (value_t){ .kind = VALUE_NIL }, line));

    TRY(patch_jump(c, ctx, j2, line));
    return deinit_scope(c, ctx);
}

static bool compile_for(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2 || args[0].tag != S_CONS || args[0].cons.size != 2)
        return compiler_malformed(ctx, "for", line);
    const sexpr_t* binding = args[0].cons.arr;
    sv_str_t iter_name = sv_str_init(" list_iter ");

    TRY(init_scope(c, ctx, line));
    TRY(compile_sexpr(c, binding[1], ctx));
    TRY(emit(c, ctx, OP_ITER_CREATE, line));
    TRY(emit(c, ctx, OP_SET_LOCAL, line));
    TRY(locals_add(c->locals, iter_name, ctx, line));
    sv_opt_t(int64_t) iter_slot = locals_get(c->locals, iter_name, ctx);
    TRY(emit2(c, ctx, OP_GET_LOCAL, (uint8_t)iter_slot.value, line));

    TRY(init_scope(c, ctx, line));
    int64_t loop_start = c->builder.vm.chunk.bytecode.size;

    sv_opt_t(int64_t) iter_idx = locals_get(c->locals, iter_name, ctx);
    TRY(emit2(c, ctx, OP_GET_LOCAL, (uint8_t)iter_idx.value, line));
    TRY(emit(c, ctx, OP_ITER_NEXT, line));

    sv_str_t id;
    TRY(expect_id(binding[0], ctx, &id));
    TRY(emit(c, ctx, OP_SET_LOCAL, line));
    TRY(locals_add(c->locals, id, ctx, line));
    sv_opt_t(int64_t) id_slot = locals_get(c->locals, id, ctx);
    TRY(emit2(c, ctx, OP_GET_LOCAL, (uint8_t)id_slot.value, line));

    int64_t j1 = 0;
    TRY(jump_emit(ctx, vmb_add_jump_if_false(&c->builder, line, &ctx->alloc), &j1, line));
    TRY(emit(c, ctx, OP_POP, line));

    TRY(compile_sexpr(c, args[1], ctx));

    uint8_t inner = (uint8_t)names_count(c->locals->name_indexes);
    TRY(emit2(c, ctx, OP_POP_LOCAL, inner, 0));
    if (!vmb_add_jump_back(&c->builder, loop_start, line, &ctx->alloc))
        return compiler_oom(ctx, line);
    TRY(patch_jump(c, ctx, j1, line));

    TRY(deinit_scope(c, ctx));
    return deinit_scope(c, ctx);
}

static bool compile_list(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], ctx));
    return emit2(c, ctx, OP_LIST, (uint8_t)n, line);
}

static bool compile_hashmap(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], ctx));
    return emit2(c, ctx, OP_HASHMAP, (uint8_t)(n / 2), line);
}

static bool compile_and_or(compiler_t* c, bool is_and, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, is_and ? "and" : "or", line);
    TRY(compile_sexpr(c, args[0], ctx));
    TRY(emit(c, ctx, OP_DUP, line));

    int64_t j1 = 0;
    TRY(jump_emit(ctx, vmb_add_jump_if_false(&c->builder, line, &ctx->alloc), &j1, line));

    if (is_and) {
        TRY(emit(c, ctx, OP_POP, line));
        TRY(compile_sexpr(c, args[1], ctx));
        return patch_jump(c, ctx, j1, line);
    }

    int64_t j2 = 0;
    TRY(jump_emit(ctx, vmb_add_jump(&c->builder, line, &ctx->alloc), &j2, line));
    TRY(patch_jump(c, ctx, j1, line));
    TRY(emit(c, ctx, OP_POP, line));
    TRY(compile_sexpr(c, args[1], ctx));
    return patch_jump(c, ctx, j2, line);
}

static bool compile_not(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 1)
        return compiler_malformed(ctx, "not", line);
    TRY(compile_sexpr(c, args[0], ctx));
    return emit(c, ctx, OP_NOT, line);
}

static bool compile_do(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    TRY(init_scope(c, ctx, line));
    for (int64_t i = 0; i < n; i++) {
        TRY(compile_sexpr(c, args[i], ctx));
        if (i < n - 1)
            TRY(emit(c, ctx, OP_POP, 0));
    }
    return deinit_scope(c, ctx);
}

static bool compile_fn_vm(
    compiler_t* c,
    const sexpr_t* cls,
    const sexpr_t* params,
    sexpr_t body,
    sv_str_t name,
    int64_t upvalue_offset,
    transient_hashmap_t members,
    int64_t line,
    ctx_t* ctx,
    vm_t* out)
{
    compiler_t fc = compiler_init();
    fc.members = members;
    fc.globals = c->globals;
    fc.record_fields = c->record_fields;

#define FN_TRY(call) do {                                                                                     \
    if (!(call)) {                                                                                            \
        fc.members = (transient_hashmap_t){0};                                                                \
        fc.globals.name_indexes = (transient_hashmap_t){0};                                                   \
        compiler_free(&fc, &ctx->alloc);                                                                      \
        vm_deinit(&fc.builder.vm, &ctx->alloc);                                                               \
        return false;                                                                                         \
    } } while (0)

    if (cls != NULL) {
        fc.upvalues.offset = upvalue_offset;
        for (int64_t i = 0; i < cls->cons.size; i++) {
            sv_str_t cls_name = { 0 };
            FN_TRY(expect_id(cls->cons.arr[i], ctx, &cls_name));
            FN_TRY(locals_add(&fc.upvalues, cls_name, ctx, line));
        }
    }

    FN_TRY(init_scope(&fc, ctx, line));
    for (int64_t i = 0; i < params->cons.size; i++) {
        sv_str_t p = { 0 };
        FN_TRY(expect_id(params->cons.arr[i], ctx, &p));
        FN_TRY(locals_add(fc.locals, p, ctx, line));
    }
    FN_TRY(locals_add(fc.locals, name, ctx, line));

    FN_TRY(compile_sexpr(&fc, body, ctx));
    FN_TRY(vmb_add_byte(&fc.builder, OP_RETURN, 0, &ctx->alloc));
#undef FN_TRY

    *out = vmb_build(&fc.builder);
    out->name = name;
    out->arity = (uint8_t)params->cons.size;
    fc.members = (transient_hashmap_t){0};
    fc.globals.name_indexes = (transient_hashmap_t){0};
    compiler_free(&fc, &ctx->alloc);
    return true;
}

static bool compile_upvalue_loads(compiler_t* c, const sexpr_t* cls, int64_t line, ctx_t* ctx)
{
    if (cls == NULL)
        return true;
    for (int64_t i = 0; i < cls->cons.size; i++) {
        sv_str_t cls_name;
        TRY(expect_id(cls->cons.arr[i], ctx, &cls_name));
        TRY(compile_id(c, cls_name, line, ctx));
    }
    return true;
}

static bool compile_fun_group(compiler_t* c, const sexpr_t* members, int64_t n, int64_t line, ctx_t* ctx);

static bool compile_fun(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n > 0 && args[0].tag == S_CONS)
        return compile_fun_group(c, args, n, line, ctx);
    if (n != 3 && n != 4)
        return compiler_malformed(ctx, "fun", line);

    bool has_cls = n == 4;
    const sexpr_t* cls = has_cls ? &args[1] : NULL;
    const sexpr_t* params = has_cls ? &args[2] : &args[1];
    sexpr_t body = has_cls ? args[3] : args[2];
    if ((has_cls && cls->tag != S_CONS) || params->tag != S_CONS)
        return compiler_malformed(ctx, "fun", line);

    sv_str_t name;
    TRY(expect_id(args[0], ctx, &name));

    vm_t fn_vm;
    TRY(compile_fn_vm(c, cls, params, body, name, 0, (transient_hashmap_t){0}, line, ctx, &fn_vm));

    if (!compile_upvalue_loads(c, cls, 0, ctx)) {
        vm_deinit(&fn_vm, &ctx->alloc);
        return false;
    }

    sv_opt_t(uint8_t) fi = vmb_add_closure(
        &c->builder,
        has_cls ? (uint8_t)cls->cons.size : 0,
        fn_vm,
        &ctx->alloc);
    if (!fi.is_some) {
        vm_deinit(&fn_vm, &ctx->alloc);
        return compiler_oom(ctx, line);
    }
    return add_var(c, name, line, ctx);
}

static bool compile_fun_group(compiler_t* c, const sexpr_t* members, int64_t n, int64_t line, ctx_t* ctx)
{
    uint8_t first = (uint8_t)c->builder.vm.chunk.functions.size;

    transient_hashmap_t member_names = {0};
#define G_TRY(call) do {                                                                                      \
    if (!(call)) {                                                                                            \
        thm_deinit(&member_names, &ctx->alloc);                                                               \
        return false;                                                                                         \
    } } while (0)

    for (int64_t i = 0; i < n; i++) {
        if (members[i].tag != S_CONS || (members[i].cons.size != 3 && members[i].cons.size != 4))
            G_TRY(compiler_malformed(ctx, "fun group", line));
        sv_str_t name;
        G_TRY(expect_id(members[i].cons.arr[0], ctx, &name));
        bool existed = false;
        if (!names_add(&member_names, name, ctx, &existed))
            G_TRY(compiler_oom(ctx, line));
        if (existed)
            G_TRY(compiler_error_name(ctx, C_ERR_REDEFINED, line, "Group member", name));
    }

    int64_t upvalue_base = 0;
    for (int64_t i = 0; i < n; i++) {
        const sexpr_t* item = members[i].cons.arr;
        bool has_cls = members[i].cons.size == 4;
        const sexpr_t* cls = has_cls ? &item[1] : NULL;
        const sexpr_t* params = has_cls ? &item[2] : &item[1];
        sexpr_t body = has_cls ? item[3] : item[2];
        if ((has_cls && cls->tag != S_CONS) || params->tag != S_CONS)
            G_TRY(compiler_malformed(ctx, "fun group", line));

        sv_str_t name;
        G_TRY(expect_id(item[0], ctx, &name));

        vm_t fn_vm;
        G_TRY(compile_fn_vm(c, cls, params, body, name, upvalue_base, member_names, line, ctx, &fn_vm));

        int success = 0;
        sv_vec_push(&c->builder.vm.chunk.functions, fn_vm, &success, &ctx->alloc);
        if (!success) {
            vm_deinit(&fn_vm, &ctx->alloc);
            G_TRY(compiler_oom(ctx, line));
        }

        upvalue_base += has_cls ? cls->cons.size : 0;
    }

    for (int64_t i = 0; i < n; i++) {
        bool has_cls = members[i].cons.size == 4;
        G_TRY(compile_upvalue_loads(c, has_cls ? &members[i].cons.arr[1] : NULL, line, ctx));
    }

    G_TRY(emit2(c, ctx, OP_CREATE_GROUP, first, line));
    G_TRY(emit2(c, ctx, (uint8_t)n, (uint8_t)upvalue_base, line));

    for (int64_t i = 0; i < n; i++) {
        sv_str_t name;
        G_TRY(expect_id(members[i].cons.arr[0], ctx, &name));
        G_TRY(add_var(c, name, line, ctx));
        if (i < n - 1)
            G_TRY(emit(c, ctx, OP_POP, line));
    }

    thm_deinit(&member_names, &ctx->alloc);
    return true;
#undef G_TRY
}

static bool compile_call(compiler_t* c, sv_str_t fn_name, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], ctx));
    TRY(compile_id(c, fn_name, line, ctx));
    return emit2(c, ctx, OP_CALL, (uint8_t)n, line);
}

static bool compile_literal(compiler_t* c, literal_t lit, int64_t line, ctx_t* ctx)
{
    switch (lit.kind) {
        case LITERAL_NUMBER: return add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = lit.number }, line);
        case LITERAL_TRUE: return add_const(c, ctx, (value_t){ .kind = VALUE_BOOL, .boolean = true }, line);
        case LITERAL_FALSE: return add_const(c, ctx, (value_t){ .kind = VALUE_BOOL, .boolean = false }, line);
        case LITERAL_NIL: return add_const(c, ctx, (value_t){ .kind = VALUE_NIL }, line);
        case LITERAL_STRING: {
            value_t s = value_init_str(lit.str, &ctx->alloc);
            if (s.obj.cell == NULL)
                return compiler_oom(ctx, line);
            return add_const(c, ctx, s, line);
        }
        case LITERAL_IDENTIFIER: return compile_id(c, lit.literal, line, ctx);
    }
    return false;
}

static bool compile_atom(compiler_t* c, token_t token, ctx_t* ctx)
{
    if (token.kind != TOKEN_LITERAL) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Unexpected token at line %" PRId64, token.line);
        return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
    }
    return compile_literal(c, token.literal, token.line, ctx);
}

static bool compile_cons(compiler_t* c, const sexpr_t* cons, int64_t n, ctx_t* ctx)
{
    if (n == 0)
        return true;

    sexpr_t head = cons[0];
    if (head.tag == S_CONS) {
        for (int64_t i = 1; i < n; i++)
            TRY(compile_sexpr(c, cons[i], ctx));
        TRY(compile_cons(c, head.cons.arr, head.cons.size, ctx));
        return emit2(c, ctx, OP_CALL, (uint8_t)(n - 1), 0);
    }

    token_t a = head.atom;
    if (a.kind == TOKEN_OPERATOR)
        return compile_operator(c, a.operator, cons + 1, n - 1, a.line, ctx);
    if (a.kind == TOKEN_SP_FUNCTION) {
        switch (a.fn) {
            case FN_IF: return compile_if(c, cons + 1, n - 1, a.line, ctx);
            case FN_FOR: return compile_for(c, cons + 1, n - 1, a.line, ctx);
            case FN_LIST: return compile_list(c, cons + 1, n - 1, a.line, ctx);
            case FN_HASHMAP: return compile_hashmap(c, cons + 1, n - 1, a.line, ctx);
            case FN_RECORD: return compile_record(c, cons + 1, n - 1, a.line, ctx);
            case FN_TUPLE: return compile_tuple(c, cons + 1, n - 1, a.line, ctx);
            case FN_FUN: return compile_fun(c, cons + 1, n - 1, a.line, ctx);
            case FN_CLASS:
            case FN_MAP:
            case FN_MAPF:
            case FN_MATCH:
            case FN_REDUCE:
            case FN_WHILE:
            case FN_IMPORT: {
                char msg[96];
                snprintf(msg, sizeof(msg), "'%s' not implemented at line %" PRId64, special_fn_text(a.fn), a.line);
                return compiler_error(ctx, C_ERR_NOT_IMPLEMENTED, msg);
            }
        }
    }
    if (a.kind == TOKEN_KEYWORD && a.keyword == KEYWORD_DO)
        return compile_do(c, cons + 1, n - 1, a.line, ctx);
    if (a.kind == TOKEN_KEYWORD && (a.keyword == KEYWORD_AND || a.keyword == KEYWORD_OR))
        return compile_and_or(c, a.keyword == KEYWORD_AND, cons + 1, n - 1, a.line, ctx);
    if (a.kind == TOKEN_KEYWORD && a.keyword == KEYWORD_NOT)
        return compile_not(c, cons + 1, n - 1, a.line, ctx);
    if (a.kind == TOKEN_LITERAL && a.literal.kind == LITERAL_IDENTIFIER)
        return compile_call(c, a.literal.literal, cons + 1, n - 1, a.line, ctx);

    char msg[96];
    snprintf(msg, sizeof(msg), "Value is not callable at line %" PRId64, a.line);
    return compiler_error(ctx, C_ERR_NOT_CALLABLE, msg);
}

bool compile_sexpr(compiler_t* c, sexpr_t sexpr, ctx_t* ctx)
{
    if (sexpr.tag == S_ATOM)
        return compile_atom(c, sexpr.atom, ctx);
    return compile_cons(c, sexpr.cons.arr, sexpr.cons.size, ctx);
}

bool add_native_fn(compiler_t* c, native_fn_t fn, ctx_t* ctx)
{
    TRY(globals_add(&c->globals, sv_str_init(fn.name), ctx, 0));
    value_t fn_val = value_init_native(fn, &ctx->alloc);
    TRY(fn_val.obj.cell != NULL);
    return vmb_add_global(&c->builder, fn_val, &ctx->alloc);
}

vm_t compile(const char* source_code, ctx_t* ctx)
{
#define ERR_RETURN do {                                                                                       \
    compiler_free(&compiler, &ctx->alloc);                                                                    \
    vm_deinit(&compiler.builder.vm, &ctx->alloc);                                                             \
    thm_deinit(&record_fields, &ctx->alloc);                                                                  \
    return (vm_t){0}; } while (0)

    scanner_t s = scanner_init(sv_str_init(source_code));
    compiler_t compiler = compiler_init();
    transient_hashmap_t record_fields = { .depth = 0 };
    compiler.record_fields = &record_fields;

#define FNS_SIZE 3
    native_fn_t native_fns[FNS_SIZE] = {
        { .arity = 1, .name = "print", .fn = ntv_print },
        { .arity = 1, .name = "println", .fn = ntv_println },
        { .arity = 1, .name = "print_vals", .fn = ntv_print_arr },
    };
    for (int64_t i = 0; i < FNS_SIZE; i++)
        if(!add_native_fn(&compiler, native_fns[i], ctx))
            ERR_RETURN;
#undef FNS_SIZE

    token_t token;
    bool first = true;
    for (;;) {
        parser_skip_semicolons(&s, ctx);
        token = scanner_peek(&s, ctx);
        if (token.kind == TOKEN_EOF || token.kind == TOKEN_ERROR)
            break;

        if (!first && !vmb_add_byte(&compiler.builder, OP_POP, token.line, &ctx->alloc)) {
            compiler_oom(ctx, token.line);
            ERR_RETURN;
        }

        sexpr_t sexpr = parser_expr(&s, ctx);
        if (is_error_sexpr(sexpr))
            ERR_RETURN;

        bool ok = compile_sexpr(&compiler, sexpr, ctx);
        sexpr_free(&sexpr, &ctx->alloc);
        if (!ok)
            ERR_RETURN;
        first = false;
    }
    if (token.kind == TOKEN_ERROR)
        ERR_RETURN;

    if (!vmb_add_byte(&compiler.builder, OP_RETURN, token.line, &ctx->alloc))
        ERR_RETURN;

    compiler_free(&compiler, &ctx->alloc);
    vm_t vm = vmb_build(&compiler.builder);
    vm.ctx.alloc = &ctx->alloc;

    int64_t n_fields = names_count(record_fields);
    if (n_fields > 0) {
        const char** names = sv_malloc(&ctx->alloc, sizeof(char*) * (size_t)n_fields);
        if (names == NULL) {
            compiler_oom(ctx, 0);
            vm_deinit(&vm, &ctx->alloc);
            thm_deinit(&record_fields, &ctx->alloc);
            return (vm_t){0};
        }

        for (int64_t i = 0; i < n_fields; i++)
            names[i] = NULL;

        map_iter_t it = thm_iter_init(record_fields);
        for (sv_opt_t(kv_t) kv = map_iter_next(&it); kv.is_some; kv = map_iter_next(&it)) {
            sv_str_t name = AS_STR(kv.value.key);
            char* copy = sv_malloc(&ctx->alloc, (size_t)name.size + 1);
            if (copy == NULL) {
                compiler_oom(ctx, 0);
                for (int64_t i = 0; i < n_fields; i++)
                    if (names[i] != NULL)
                        sv_free(&ctx->alloc, (void*)names[i]);
                sv_free(&ctx->alloc, names);
                vm_deinit(&vm, &ctx->alloc);
                thm_deinit(&record_fields, &ctx->alloc);
                return (vm_t){0};
            }
            memcpy(copy, name.chars, (size_t)name.size);
            copy[name.size] = '\0';
            names[(int64_t)kv.value.value.number] = copy;
        }
        vm.ctx.record_key_names = names;
        vm.ctx.record_names_sizes = (uint32_t)n_fields;
    }

    thm_deinit(&record_fields, &ctx->alloc);
    return vm;
#undef ERR_RETURN
}

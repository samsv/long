#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "compiler.h"
#include "obj/map.h"
#include "scanner.h"
#include "sexpr.h"
#include "parser.h"
#include "std/string.h"
#define SV_ARENA_IMPLEMENTATION
#include "std/arena.h"
#include "std_native.h"
#include "pattern_shape.h"
#include "pattern_match.h"
#include "deps/cwalk.h"

void compiler_free(compiler_t* c, const sv_allocator_t* a);
bool compile_sexpr(compiler_t* c, sexpr_t sexpr, bool is_tail, ctx_t* ctx);

typedef struct {
    const char* name;
    vm_instructions op;
} type_test_t;

static const type_test_t TYPE_TESTS[] = {
    { "is-str?", OP_IS_STR },
    { "is-number?", OP_IS_NUMBER },
    { "is-bool?", OP_IS_BOOL },
    { "is-nil?", OP_IS_NIL },
    { "is-list?", OP_IS_LIST },
    { "is-cons?", OP_IS_CONS },
    { "is-nil-list?", OP_IS_NIL_LIST },
};

#define TRY(call) do { if (!(call)) return false; } while (0)

static bool compiler_error(ctx_t* ctx, compiler_error_kind kind, const char* msg)
{
    return error_set(&ctx->err, (int)kind, msg, &ctx->alloc);
}

static bool compiler_oom(ctx_t* ctx, int64_t line)
{
    return error_set_oom(&ctx->err, (int)C_ERR_OOM, line, &ctx->alloc);
}

compiler_t compiler_init(const char* base_path, ctx_t* ctx, bool* success)
{
    module_map_t modules = {
        .compiled_modules = thm_init(8, &ctx->alloc),
        .to_be_compiled_modules = thm_init(8, &ctx->alloc),
    };
    if (modules.compiled_modules.set.dense.cell == NULL
        || modules.to_be_compiled_modules.set.dense.cell == NULL
    ) {
        thm_deinit(&modules.compiled_modules, &ctx->alloc);
        thm_deinit(&modules.to_be_compiled_modules, &ctx->alloc);
        compiler_oom(ctx, 0);
        *success = false;
        return (compiler_t){0};
    }

    *success = true;
    return (compiler_t){
        .globals = { .name_indexes = { .depth = 0 } },
        .upvalues = { .name_indexes = { .depth = 0 }, .next = NULL, .offset = 0 },
        .locals = NULL,
        .members = { .depth = 0 },
        .record_fields = NULL,
        .fail_targets = NULL,
        .builder = fnb_init(sv_str_init("")),
        .global_values = sv_vec_init(value_t),
        .current_path = base_path,
        .modules = modules,
    };
}

static char* read_file(const char* path, const sv_allocator_t* a)
{
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = sv_malloc(a, fileSize + 1);
    if (buffer == NULL) {
        return NULL;
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        return NULL;
    }
    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
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
    if (!fnb_add_byte(&c->builder, byte, line, &ctx->alloc))
        return compiler_oom(ctx, line);
    return true;
}

static bool emit2(compiler_t* c, ctx_t* ctx, uint8_t b1, uint8_t b2, int64_t line)
{
    if (!fnb_add_bytes(&c->builder, b1, b2, line, &ctx->alloc))
        return compiler_oom(ctx, line);
    return true;
}

static bool compile_source(compiler_t* c, const char* source_code, ctx_t* ctx)
{
    scanner_t s = scanner_init(sv_str_init(source_code));
    c->var_to_modules = thm_init(8, &ctx->alloc);
    if (c->var_to_modules.set.dense.cell == NULL) {
        return compiler_oom(ctx, 0);
    }

    token_t token;
    bool first = true;
    for (;;) {
        parser_skip_semicolons(&s, ctx);
        token = scanner_peek(&s, ctx);
        if (token.kind == TOKEN_EOF || token.kind == TOKEN_ERROR)
            break;

        if (!first && !fnb_add_byte(&c->builder, OP_POP, token.line, &ctx->alloc)) {
            compiler_oom(ctx, token.line);
            goto fail;
        }

        sexpr_t sexpr = parser_expr(&s, ctx);
        if (is_error_sexpr(sexpr))
            goto fail;

        bool ok = compile_sexpr(c, sexpr, false, ctx);
        sexpr_free(&sexpr, &ctx->alloc);
        if (!ok)
            goto fail;
        first = false;
    }
    if (token.kind == TOKEN_ERROR)
        goto fail;

    thm_deinit(&c->var_to_modules, &ctx->alloc);
    return true;

fail:
    thm_deinit(&c->var_to_modules, &ctx->alloc);
    return false;
}

static bool add_const(compiler_t* c, ctx_t* ctx, value_t v, int64_t line)
{
    sv_opt_t(uint32_t) i = fnb_add_constant(&c->builder, v, &ctx->alloc);
    if (!i.is_some) {
        value_free(&v, &ctx->alloc);
        return compiler_oom(ctx, line);
    }
    return true;
}

static bool emit_narrow(compiler_t* c, ctx_t* ctx, uint8_t op, int64_t arg,
                        const char* what, int64_t line)
{
    TRY(check_limit(ctx, arg, UINT8_MAX, what, line));
    return emit2(c, ctx, op, (uint8_t)arg, line);
}

static bool emit_wide(compiler_t* c, ctx_t* ctx, uint8_t op, uint32_t arg, int64_t line)
{
    if (!fnb_add_arg(&c->builder, op, arg, line, &ctx->alloc))
        return compiler_oom(ctx, line);
    return true;
}

static bool emit_pop_locals(compiler_t* c, ctx_t* ctx, int64_t count, int64_t line)
{
    if (count == 0)
        return true;
    return emit_wide(c, ctx, OP_POP_LOCAL, (uint32_t)count, line);
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
    int64_t offset = c->builder.fn.chunk.bytecode.size - ji;
    if (offset > UINT16_MAX) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Jump too long at line %" PRId64, line);
        return compiler_error(ctx, C_ERR_JUMP_TOO_LONG, msg);
    }
    fnb_patch_jump(&c->builder, ji, (uint16_t)offset);
    return true;
}


static bool locals_add(locals_t* l, sv_str_t id, ctx_t* ctx, int64_t line)
{
    TRY(check_limit(ctx, names_count(l->name_indexes) + l->offset, UINT32_MAX, "locals", line));
    bool existed = false;
    if (!names_add(&l->name_indexes, id, ctx, &existed))
        return compiler_oom(ctx, line);
    if (existed)
        return compiler_error_name(ctx, C_ERR_REDEFINED, line, "Local redefined", id);
    return true;
}

static sv_opt_t(uint32_t) locals_get(const locals_t* l, sv_str_t id, ctx_t* ctx)
{
    for (; l != NULL; l = l->next) {
        sv_opt_t(uint32_t) idx = names_get(l->name_indexes, id, ctx);
        if (idx.is_some)
            return sv_opt_some_t(uint32_t, (uint32_t)(idx.value + l->offset));
    }
    return sv_opt_none_t(uint32_t);
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

void compiler_free(compiler_t* c, const sv_allocator_t* a)
{
    thm_deinit(&c->upvalues.name_indexes, a);
    thm_deinit(&c->members, a);
    thm_deinit(&c->modules.compiled_modules, a);
    thm_deinit(&c->modules.to_be_compiled_modules, a);
    value_arr_deinit(&c->global_values, a);
    while (c->locals != NULL) {
        locals_t* l = c->locals;
        c->locals = l->next;
        thm_deinit(&l->name_indexes, a);
        sv_free(a, l);
    }
}

static bool compile_fail(compiler_t*, int64_t, ctx_t*);
static bool record_field_id(compiler_t*, sv_str_t, int64_t, ctx_t*, uint32_t*);
static bool reject_pattern_only(ctx_t*, int64_t);
static bool reject_shape(ctx_t*, const char*, int64_t);

static bool compile_id(compiler_t* c, sv_str_t id, int64_t line, ctx_t* ctx)
{
    if (sv_str_comp(id, sv_str_init("$fail")))
        return compile_fail(c, line, ctx);

    sv_opt_t(uint32_t) idx = locals_get(c->locals, id, ctx);
    if (idx.is_some)
        return emit_wide(c, ctx, OP_GET_LOCAL, idx.value, line);

    idx = locals_get(&c->upvalues, id, ctx);
    if (idx.is_some)
        return emit_wide(c, ctx, OP_GET_UPVALUE, idx.value, line);

    idx = globals_get(c->globals, id, sv_str_init(c->current_path), ctx);
    if (idx.is_some)
        return emit_wide(c, ctx, OP_GET_GLOBAL, idx.value, line);

    idx = names_get(c->members, id, ctx);
    if (idx.is_some)
        return emit_narrow(c, ctx, OP_GET_MEMBER, idx.value, "closure group members", line);

    return compiler_error_name(ctx, C_ERR_UNDEFINED_VARIABLE, line, "Undefined variable", id);
}

static bool add_var(compiler_t* c, sv_str_t id, int64_t line, ctx_t* ctx)
{
    if (c->locals != NULL) {
        TRY(emit(c, ctx, OP_SET_LOCAL, line));
        TRY(locals_add(c->locals, id, ctx, line));
        sv_opt_t(uint32_t) idx = locals_get(c->locals, id, ctx);
        return emit_wide(c, ctx, OP_GET_LOCAL, idx.value, line);
    }
    TRY(emit(c, ctx, OP_SET_GLOBAL, line));
    TRY(!globals_add(&c->globals, id, sv_str_init(c->current_path), line, ctx).is_some);
    sv_opt_t(uint32_t) idx = globals_get(c->globals, id, sv_str_init(c->current_path), ctx);
    return emit_wide(c, ctx, OP_GET_GLOBAL, idx.value, line);
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

    int64_t n = names_count(local->name_indexes);
    c->locals = local->next;
    thm_deinit(&local->name_indexes, &ctx->alloc);
    sv_free(&ctx->alloc, local);
    return emit_pop_locals(c, ctx, n, 0);
}

static bool is_pattern_wildcard(sexpr_t e)
{
    return e.tag == S_ATOM && e.atom.kind == TOKEN_LITERAL
        && e.atom.literal.kind == LITERAL_IDENTIFIER
        && sv_str_comp(e.atom.literal.literal, sv_str_init("_"));
}

/**
 * Emits a test already on the stack top and raises when it is false. The subject
 * sits directly beneath, so the error can name the offending value.
 */
static bool emit_assert(compiler_t* c, int64_t line, ctx_t* ctx)
{
    return emit(c, ctx, OP_ASSERT_MATCH, line);
}

static bool bind_pattern(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                         int64_t line, ctx_t* ctx);

/**
 * A first occurrence declares the name; a repeat compares against what the earlier
 * occurrence bound, so `(a, a) = e` requires both positions to be equal.
 */
static bool bind_var(compiler_t* c, sv_str_t name, sv_vec_t(sv_str_t)* seen,
                     int64_t line, ctx_t* ctx)
{
    for (int64_t i = 0; i < seen->size; i++) {
        if (!sv_str_comp(seen->arr[i], name))
            continue;

        TRY(emit(c, ctx, OP_DUP, line));
        TRY(compile_id(c, name, line, ctx));
        TRY(emit(c, ctx, OP_EQUALS, line));
        return emit_assert(c, line, ctx);
    }

    int success = 0;
    sv_vec_push(seen, name, &success, &ctx->alloc);
    if (success == 0)
        return compiler_oom(ctx, line);

    return add_var(c, name, line, ctx);
}

/**
 * Compiles one sub pattern against the value on the stack top, leaving that value
 * in place: `read` pushes the part, the recursion consumes and restores it, and
 * the pop returns to the parent's subject.
 */
static bool bind_part(compiler_t* c, sexpr_t sub, sv_vec_t(sv_str_t)* seen,
                      int64_t line, ctx_t* ctx)
{
    TRY(bind_pattern(c, sub, seen, line, ctx));
    return emit(c, ctx, OP_POP, line);
}

static bool bind_tuple(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                       int64_t line, ctx_t* ctx)
{
    int64_t arity = pattern.cons.size - 1;
    TRY(emit(c, ctx, OP_DUP, line));
    TRY(emit_narrow(c, ctx, OP_IS_TUPLE, arity, "tuple elements", line));
    TRY(emit_assert(c, line, ctx));

    for (int64_t i = 0; i < arity; i++) {
        TRY(emit(c, ctx, OP_DUP, line));
        TRY(add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = (double)i }, line));
        TRY(emit(c, ctx, OP_INDEX, line));
        TRY(bind_part(c, pattern.cons.arr[1 + i], seen, line, ctx));
    }
    return true;
}

static bool bind_record(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                        int64_t line, ctx_t* ctx)
{
    if (spread_of(pattern.cons.arr + 1, pattern.cons.size - 1) != NULL)
        return reject_shape(ctx, "A record or hashmap pattern cannot bind its rest, use a bare '..'", line);

    int64_t n = record_n_fields(pattern);
    TRY(emit(c, ctx, OP_DUP, line));
    if (record_is_open(pattern))
        TRY(emit(c, ctx, OP_IS_RECORD_ANY, line));
    else
        TRY(emit_narrow(c, ctx, OP_IS_RECORD, n, "record fields", line));
    TRY(emit_assert(c, line, ctx));

    for (int64_t i = 0; i < n; i++) {
        sv_str_t field;
        TRY(expect_id(pattern.cons.arr[1 + 2 * i], ctx, &field));
        uint32_t id = 0;
        TRY(record_field_id(c, field, line, ctx, &id));

        TRY(emit(c, ctx, OP_DUP, line));
        TRY(emit2(c, ctx, OP_HAS_FIELD, (uint8_t)id, line));
        TRY(emit_assert(c, line, ctx));

        TRY(emit(c, ctx, OP_DUP, line));
        TRY(emit2(c, ctx, OP_RECORD_GET, (uint8_t)id, line));
        TRY(bind_part(c, pattern.cons.arr[2 + 2 * i], seen, line, ctx));
    }
    return true;
}

static bool bind_hashmap(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                         int64_t line, ctx_t* ctx)
{
    if (spread_of(pattern.cons.arr + 1, pattern.cons.size - 1) != NULL)
        return reject_shape(ctx, "A record or hashmap pattern cannot bind its rest, use a bare '..'", line);

    int64_t n = hashmap_n_keys(pattern);
    TRY(emit(c, ctx, OP_DUP, line));
    if (hashmap_is_open(pattern)) {
        TRY(emit(c, ctx, OP_IS_HASHMAP_ANY, line));
    } else {
        TRY(add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = (double)n }, line));
        TRY(emit(c, ctx, OP_SWAP, line));
        TRY(emit(c, ctx, OP_IS_HASHMAP, line));
    }
    TRY(emit_assert(c, line, ctx));

    for (int64_t i = 0; i < n; i++) {
        sexpr_t key = pattern.cons.arr[1 + 2 * i];
        TRY(emit(c, ctx, OP_DUP, line));
        TRY(compile_sexpr(c, key, false, ctx));
        TRY(emit(c, ctx, OP_HAS_KEY, line));
        TRY(emit_assert(c, line, ctx));

        TRY(emit(c, ctx, OP_DUP, line));
        TRY(compile_sexpr(c, key, false, ctx));
        TRY(emit(c, ctx, OP_INDEX, line));
        TRY(bind_part(c, pattern.cons.arr[2 + 2 * i], seen, line, ctx));
    }
    return true;
}

/**
 * Each element unconses the current remainder, so the tails stack up and are
 * popped together at the end. Without a `..` the remainder must be empty, which
 * is what pins the length.
 */
static bool bind_list(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                      int64_t line, ctx_t* ctx)
{
    int64_t fixed = list_n_fixed(pattern);
    TRY(emit(c, ctx, OP_DUP, line));
    TRY(emit(c, ctx, OP_IS_LIST, line));
    TRY(emit_assert(c, line, ctx));

    for (int64_t i = 0; i < fixed; i++) {
        TRY(emit(c, ctx, OP_DUP, line));
        TRY(emit(c, ctx, OP_IS_CONS, line));
        TRY(emit_assert(c, line, ctx));

        TRY(emit(c, ctx, OP_LIST_UNCONS, line));
        TRY(bind_part(c, pattern.cons.arr[1 + i], seen, line, ctx));
    }

    if (list_has_tail(pattern)) {
        sexpr_t tail = pattern.cons.arr[pattern.cons.size - 1].cons.arr[1];
        if (!is_list_tail(tail))
            return reject_shape(ctx, "List tail must be a variable or a list", line);
        TRY(bind_pattern(c, tail, seen, line, ctx));
    } else {
        TRY(emit(c, ctx, OP_DUP, line));
        TRY(emit(c, ctx, OP_IS_CONS, line));
        TRY(emit(c, ctx, OP_NOT, line));
        TRY(emit_assert(c, line, ctx));
    }

    for (int64_t i = 0; i < fixed; i++)
        TRY(emit(c, ctx, OP_POP, line));

    return true;
}

static bool bind_literal(compiler_t* c, sexpr_t pattern, int64_t line, ctx_t* ctx)
{
    TRY(emit(c, ctx, OP_DUP, line));
    TRY(compile_sexpr(c, pattern, false, ctx));
    TRY(emit(c, ctx, OP_EQUALS, line));
    return emit_assert(c, line, ctx);
}

/**
 * A negated number is a literal pattern, but reaches the left of `=` as a unary
 * minus cons because the left side is parsed as an expression.
 */
static bool is_negative_number(sexpr_t e)
{
    return e.tag == S_CONS && e.cons.size == 2
        && e.cons.arr[0].tag == S_ATOM && e.cons.arr[0].atom.kind == TOKEN_OPERATOR
        && e.cons.arr[0].atom.operator == OPERATOR_MINUS
        && e.cons.arr[1].tag == S_ATOM && e.cons.arr[1].atom.kind == TOKEN_LITERAL
        && e.cons.arr[1].atom.literal.kind == LITERAL_NUMBER;
}

/**
 * Both sides of an alias match the same subject, and every arm leaves the subject on top,
 * so the second side simply runs after the first.
 */
static bool bind_alias(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                       int64_t line, ctx_t* ctx)
{
    if (!pattern_is_name(pattern.cons.arr[1]) && !pattern_is_name(pattern.cons.arr[2])) {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "One side of '=' in a pattern must be a name at line %" PRId64, line);
        return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
    }
    TRY(bind_pattern(c, pattern.cons.arr[1], seen, line, ctx));
    return bind_pattern(c, pattern.cons.arr[2], seen, line, ctx);
}

/**
 * Compiles one pattern against the value on the stack top, leaving that value in
 * place. Every arm holds to that: the subject is on top on entry and on top on exit,
 * which is what lets the container cases read a part, recurse and pop back. A
 * variable leaf satisfies it for free, since add_var consumes the top and pushes the
 * value back.
 */
static bool bind_pattern(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                         int64_t line, ctx_t* ctx)
{
    if (pattern.tag == S_ATOM) {
        if (is_pattern_wildcard(pattern))
            return true;
        if (pattern.atom.kind == TOKEN_LITERAL
            && pattern.atom.literal.kind == LITERAL_IDENTIFIER)
            return bind_var(c, pattern.atom.literal.literal, seen, line, ctx);

        return bind_literal(c, pattern, line, ctx);
    }

    if (is_negative_number(pattern))
        return bind_literal(c, pattern, line, ctx);
    if (pattern_is_alias(pattern))
        return bind_alias(c, pattern, seen, line, ctx);

    token_t head = pattern.cons.arr[0].atom;
    if (head.kind != TOKEN_SP_FUNCTION)
        return compiler_malformed(ctx, "destructuring pattern", line);

    if (head.fn == FN_LIST)
        return bind_list(c, pattern, seen, line, ctx);
    if (head.fn == FN_RECORD)
        return bind_record(c, pattern, seen, line, ctx);
    if (head.fn == FN_HASHMAP)
        return bind_hashmap(c, pattern, seen, line, ctx);
    if (head.fn == FN_TUPLE)
        return bind_tuple(c, pattern, seen, line, ctx);

    return compiler_malformed(ctx, "destructuring pattern", line);
}

/**
 * Destructures the value on the stack top. There is no next alternative to fall
 * through to, so a value that does not fit raises rather than failing over.
 */
static bool compile_destructure(compiler_t* c, sexpr_t pattern, int64_t line, ctx_t* ctx)
{
    sv_vec_t(sv_str_t) seen = sv_vec_init(sv_str_t);
    bool ok = bind_pattern(c, pattern, &seen, line, ctx);
    sv_vec_deinit(&seen, &ctx->alloc);
    return ok;
}

static bool compile_equal(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "assignment", line);

    if (args[0].tag == S_CONS || is_pattern_wildcard(args[0])
        || args[0].atom.kind != TOKEN_LITERAL
        || args[0].atom.literal.kind != LITERAL_IDENTIFIER) {
        TRY(compile_sexpr(c, args[1], false, ctx));
        return compile_destructure(c, args[0], line, ctx);
    }

    sv_str_t id;
    TRY(expect_id(args[0], ctx, &id));
    TRY(compile_sexpr(c, args[1], false, ctx));
    return add_var(c, id, line, ctx);
}

static bool compile_pipe(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, bool is_tail, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "pipe", line);
    TRY(compile_sexpr(c, args[0], false, ctx));

    sexpr_t rhs = args[1];
    if (rhs.tag != S_CONS || rhs.cons.size == 0)
        return compiler_malformed(ctx, "pipe", line);

    sv_str_t fn_name;
    TRY(expect_id(rhs.cons.arr[0], ctx, &fn_name));
    for (int64_t i = 1; i < rhs.cons.size; i++)
        TRY(compile_sexpr(c, rhs.cons.arr[i], false, ctx));
    TRY(compile_id(c, fn_name, line, ctx));
    uint8_t op = is_tail ? OP_TAIL_CALL : OP_CALL;
    return emit_narrow(c, ctx, op, rhs.cons.size, "arguments", line);
}

static bool record_field_id(compiler_t* c, sv_str_t name, int64_t line, ctx_t* ctx, uint32_t* out)
{
    bool existed = false;
    if (!names_add(c->record_fields, name, ctx, &existed))
        return compiler_oom(ctx, line);

    sv_opt_t(uint32_t) id = names_get(*c->record_fields, name, ctx);
    if (id.value > UINT8_MAX) {
        char msg[96];
        snprintf(msg, sizeof(msg), "More than %d record fields at line %" PRId64, UINT8_MAX + 1, line);
        return compiler_error(ctx, C_ERR_LIMIT_EXCEEDED, msg);
    }
    *out = id.value;
    return true;
}

static bool compile_tuple(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n < 1 || n > UINT8_MAX)
        return compiler_malformed(ctx, "tuple", line);

    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], false, ctx));
    return emit2(c, ctx, OP_TUPLE, (uint8_t)n, line);
}

static bool compile_record(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n > 0 && args[n - 1].tag == S_ATOM && args[n - 1].atom.kind == TOKEN_DOT_DOT)
        return reject_pattern_only(ctx, args[n - 1].atom.line);

    const sexpr_t* base = spread_of(args, n);
    int64_t n_kvs = base == NULL ? n : n - 1;
    if (n_kvs % 2 != 0 || n_kvs / 2 > UINT8_MAX)
        return compiler_malformed(ctx, "record", line);

    struct { uint32_t id; sv_str_t name; const sexpr_t* value; } fields[UINT8_MAX];
    int64_t n_fields = n_kvs / 2;
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
        TRY(compile_sexpr(c, *fields[i].value, false, ctx));
    }
    if (base == NULL)
        return emit2(c, ctx, OP_RECORD, (uint8_t)n_fields, line);

    // id_1 v_1 ... base => record_update n
    TRY(compile_sexpr(c, *base, false, ctx));
    return emit2(c, ctx, OP_RECORD_UPDATE, (uint8_t)n_fields, line);
}

static bool compile_dot(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "field access", line);

    sv_str_t name;
    TRY(expect_id(args[1], ctx, &name));
    uint32_t id = 0;
    TRY(record_field_id(c, name, line, ctx, &id));

    TRY(compile_sexpr(c, args[0], false, ctx));
    return emit2(c, ctx, OP_RECORD_GET, (uint8_t)id, line);
}

static bool compile_double_colon(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "field access", line);

    sv_str_t module_name;
    TRY(expect_id(args[0], ctx, &module_name));

    sv_str_t var_name = {0};
    TRY(expect_id(args[1], ctx, &var_name));

    value_t module_name_value = value_init_str(module_name, &ctx->alloc);
    TRY(module_name_value.obj.cell != NULL);

    sv_opt_t(value_t) module_path = thm_get(c->var_to_modules, module_name_value);
    if (!module_path.is_some) {
        compiler_error_name(ctx, C_ERR_UNDEFINED_VARIABLE, line, "Undefined variable", module_name);
        value_free(&module_name_value, &ctx->alloc);
        return false;
    }
    value_free(&module_name_value, &ctx->alloc);
    sv_opt_t(uint32_t) id = globals_get(c->globals, var_name, AS_STR(module_path.value), ctx);
    if (!id.is_some)
        return compiler_error_name(ctx, C_ERR_UNDEFINED_VARIABLE, line, "Undefined variable", var_name);

    return emit_wide(c, ctx, OP_GET_GLOBAL, id.value, line);
}

static bool compile_record_get_or_nil(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "field access", line);

    sv_str_t name;
    TRY(expect_id(args[1], ctx, &name));
    uint32_t id = 0;
    TRY(record_field_id(c, name, line, ctx, &id));

    TRY(compile_sexpr(c, args[0], false, ctx));
    return emit2(c, ctx, OP_RECORD_GET_OR_UNDEF, (uint8_t)id, line);
}

static bool compile_hashmap_get_or_nil(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "field access", line);

    TRY(compile_sexpr(c, args[0], false, ctx));
    TRY(compile_sexpr(c, args[1], false, ctx));
    return emit(c, ctx, OP_HASHMAP_GET_OR_UNDEF, line);
}

static bool compile_length(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 1)
        return compiler_malformed(ctx, "length", line);

    TRY(compile_sexpr(c, args[0], false, ctx));
    return emit(c, ctx, OP_LENGTH, line);
}

static bool compile_binary_op(compiler_t* c, uint8_t instruction, const char* what,
                              const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, what, line);
    TRY(compile_sexpr(c, args[0], false, ctx));
    TRY(compile_sexpr(c, args[1], false, ctx));
    return emit(c, ctx, instruction, line);
}

static bool compile_operator(compiler_t* c, operator_kind op, const sexpr_t* args,
                             int64_t n, int64_t line, bool is_tail, ctx_t* ctx)
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
        case OPERATOR_PIPE_FORWARD: return compile_pipe(c, args, n, line, is_tail, ctx);
        case OPERATOR_DOT: return compile_dot(c, args, n, line, ctx);
        case OPERATOR_DOUBLE_COLON: return compile_double_colon(c, args, n, line, ctx);
        case OPERATOR_LEFT_PAREN: {
            char msg[96];
            snprintf(msg, sizeof(msg), "Operator not implemented at line %" PRId64, line);
            return compiler_error(ctx, C_ERR_NOT_IMPLEMENTED, msg);
        }
    }

    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], false, ctx));
    return emit(c, ctx, instruction, line);
}

static bool compile_if(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, bool is_tail, ctx_t* ctx)
{
    if (n != 2 && n != 3)
        return compiler_malformed(ctx, "if", line);

    TRY(init_scope(c, ctx, line));
    TRY(compile_sexpr(c, args[0], false, ctx));

    int64_t j1 = 0;
    TRY(jump_emit(ctx, fnb_add_jump_if_false(&c->builder, line, &ctx->alloc), &j1, line));
    TRY(compile_sexpr(c, args[1], is_tail, ctx));

    int64_t j2 = 0;
    TRY(jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), &j2, line));
    TRY(patch_jump(c, ctx, j1, line));
    if (n == 3)
        TRY(compile_sexpr(c, args[2], is_tail, ctx));
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
    TRY(compile_sexpr(c, binding[1], false, ctx));
    TRY(emit(c, ctx, OP_ITER_CREATE, line));
    TRY(emit(c, ctx, OP_SET_LOCAL, line));
    TRY(locals_add(c->locals, iter_name, ctx, line));
    sv_opt_t(uint32_t) iter_slot = locals_get(c->locals, iter_name, ctx);
    TRY(emit_wide(c, ctx, OP_GET_LOCAL, iter_slot.value, line));

    TRY(init_scope(c, ctx, line));
    int64_t loop_start = c->builder.fn.chunk.bytecode.size;

    sv_opt_t(uint32_t) iter_idx = locals_get(c->locals, iter_name, ctx);
    TRY(emit_wide(c, ctx, OP_GET_LOCAL, iter_idx.value, line));
    TRY(emit(c, ctx, OP_ITER_NEXT, line));

    sv_str_t id;
    bool destructure = binding[0].tag == S_CONS;
    if (destructure)
        id = sv_str_init(" for_item ");
    else
        TRY(expect_id(binding[0], ctx, &id));
    TRY(emit(c, ctx, OP_SET_LOCAL, line));
    TRY(locals_add(c->locals, id, ctx, line));
    sv_opt_t(uint32_t) id_slot = locals_get(c->locals, id, ctx);
    TRY(emit_wide(c, ctx, OP_GET_LOCAL, id_slot.value, line));

    int64_t j1 = 0;
    TRY(jump_emit(ctx, fnb_add_jump_if_false(&c->builder, line, &ctx->alloc), &j1, line));
    TRY(emit(c, ctx, OP_POP, line));

    /* The pattern's names get their own scope: the exit path jumps here having
     * pushed only the item, so the loop's own pop must keep counting just that. */
    if (destructure) {
        TRY(init_scope(c, ctx, line));
        TRY(emit_wide(c, ctx, OP_GET_LOCAL, id_slot.value, line));
        TRY(compile_destructure(c, binding[0], line, ctx));
        TRY(emit(c, ctx, OP_POP, line));
    }

    TRY(compile_sexpr(c, args[1], false, ctx));

    if (destructure)
        TRY(deinit_scope(c, ctx));

    TRY(emit_pop_locals(c, ctx, names_count(c->locals->name_indexes), 0));
    if (!fnb_add_jump_back(&c->builder, loop_start, line, &ctx->alloc))
        return compiler_oom(ctx, line);
    TRY(patch_jump(c, ctx, j1, line));

    TRY(deinit_scope(c, ctx));
    return deinit_scope(c, ctx);
}

static bool reject_pattern_only(ctx_t* ctx, int64_t line)
{
    char msg[96];
    snprintf(msg, sizeof(msg),
             "'..' is only valid in a pattern or on the left of '=' at line %" PRId64, line);
    return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
}

static bool reject_shape(ctx_t* ctx, const char* what, int64_t line)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "%s at line %" PRId64, what, line);
    return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
}

static bool compile_list(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    const sexpr_t* tail = spread_of(args, n);
    int64_t fixed = tail == NULL ? n : n - 1;
    for (int64_t i = 0; i < fixed; i++) {
        if (spread_of(&args[i], 1) != NULL)
            return compiler_malformed(ctx, "list spread", line);
        TRY(compile_sexpr(c, args[i], false, ctx));
    }
    if (tail == NULL) {
        TRY(check_limit(ctx, n, UINT32_MAX, "list elements", line));
        return emit_wide(c, ctx, OP_LIST, (uint32_t)n, line);
    }

    // e_1 ... e_n tail => list_prepend n
    TRY(compile_sexpr(c, *tail, false, ctx));
    TRY(check_limit(ctx, fixed, UINT32_MAX, "list elements", line));
    return emit_wide(c, ctx, OP_LIST_PREPEND, (uint32_t)fixed, line);
}

static bool compile_hashmap(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n > 0 && args[n - 1].tag == S_ATOM && args[n - 1].atom.kind == TOKEN_DOT_DOT)
        return reject_pattern_only(ctx, args[n - 1].atom.line);

    const sexpr_t* base = spread_of(args, n);
    int64_t n_kvs = base == NULL ? n : n - 1;
    if (n_kvs % 2 != 0)
        return compiler_malformed(ctx, "hashmap", line);
    TRY(check_limit(ctx, n_kvs / 2, UINT32_MAX, "hashmap entries", line));

    for (int64_t i = 0; i < n_kvs; i++)
        TRY(compile_sexpr(c, args[i], false, ctx));
    if (base == NULL)
        return emit_wide(c, ctx, OP_HASHMAP, (uint32_t)(n_kvs / 2), line);

    // k_1 v_1 ... base => hashmap_update n
    TRY(compile_sexpr(c, *base, false, ctx));
    return emit_wide(c, ctx, OP_HASHMAP_UPDATE, (uint32_t)(n_kvs / 2), line);
}

static bool compile_and_or(compiler_t* c, bool is_and, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, is_and ? "and" : "or", line);
    TRY(compile_sexpr(c, args[0], false, ctx));
    TRY(emit(c, ctx, OP_DUP, line));

    int64_t j1 = 0;
    TRY(jump_emit(ctx, fnb_add_jump_if_false(&c->builder, line, &ctx->alloc), &j1, line));

    if (is_and) {
        TRY(emit(c, ctx, OP_POP, line));
        TRY(compile_sexpr(c, args[1], false, ctx));
        return patch_jump(c, ctx, j1, line);
    }

    int64_t j2 = 0;
    TRY(jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), &j2, line));
    TRY(patch_jump(c, ctx, j1, line));
    TRY(emit(c, ctx, OP_POP, line));
    TRY(compile_sexpr(c, args[1], false, ctx));
    return patch_jump(c, ctx, j2, line);
}

static bool compile_not(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 1)
        return compiler_malformed(ctx, "not", line);
    TRY(compile_sexpr(c, args[0], false, ctx));
    return emit(c, ctx, OP_NOT, line);
}

static bool compile_do(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, bool is_tail, ctx_t* ctx)
{
    TRY(init_scope(c, ctx, line));
    for (int64_t i = 0; i < n; i++) {
        bool is_last = i == n - 1;
        TRY(compile_sexpr(c, args[i], is_last && is_tail, ctx));
        if (!is_last)
            TRY(emit(c, ctx, OP_POP, 0));
    }
    return deinit_scope(c, ctx);
}

/**
 * Names the local slot a destructured parameter occupies. The spaces keep it
 * unscannable, so it can never collide with a user name. The index is an int
 * because arity is a uint8_t, which also lets the compiler bound the buffer.
 */
static sv_str_t param_slot(char* buf, size_t n, int i)
{
    snprintf(buf, n, " arg%d ", i);
    return sv_str_init(buf);
}

static bool compile_fn_vm(compiler_t* c, const sexpr_t* cls, const sexpr_t* params, sexpr_t body,
                          sv_str_t name, int64_t upvalue_offset, transient_hashmap_t members, int64_t line,
                          ctx_t* ctx, fn_t* out)
{
    bool success;
    compiler_t fc = compiler_init(c->current_path, ctx, &success);
    if (!success)
        return success;
    fc.members = members;
    fc.globals = c->globals;
    fc.record_fields = c->record_fields;

#define FN_TRY(call) do {                                                                                     \
    if (!(call)) {                                                                                            \
        fc.members = (transient_hashmap_t){0};                                                                \
        fc.globals.name_indexes = (transient_hashmap_t){0};                                                   \
        compiler_free(&fc, &ctx->alloc);                                                                      \
        fn_deinit(&fc.builder.fn, &ctx->alloc);                                                               \
        return false;                                                                                         \
} } while (0)

    if (cls != NULL) {
        fc.upvalues.offset = upvalue_offset;
        FN_TRY(check_limit(ctx, cls->cons.size, UINT8_MAX, "closure upvalues", line));
        for (int64_t i = 0; i < cls->cons.size; i++) {
            sv_str_t cls_name = { 0 };
            FN_TRY(expect_id(cls->cons.arr[i], ctx, &cls_name));
            FN_TRY(locals_add(&fc.upvalues, cls_name, ctx, line));
        }
    }

    FN_TRY(check_limit(ctx, params->cons.size, UINT8_MAX, "parameters", line));
    FN_TRY(init_scope(&fc, ctx, line));
    for (int64_t i = 0; i < params->cons.size; i++) {
        sv_str_t p = { 0 };
        char slot[24];
        if (params->cons.arr[i].tag == S_CONS)
            p = param_slot(slot, sizeof(slot), (int)i);
        else
            FN_TRY(expect_id(params->cons.arr[i], ctx, &p));
        FN_TRY(locals_add(fc.locals, p, ctx, line));
    }
    FN_TRY(locals_add(fc.locals, name, ctx, line));

    for (int64_t i = 0; i < params->cons.size; i++) {
        if (params->cons.arr[i].tag != S_CONS)
            continue;

        char slot[24];
        FN_TRY(compile_id(&fc, param_slot(slot, sizeof(slot), (int)i), line, ctx));
        FN_TRY(compile_destructure(&fc, params->cons.arr[i], line, ctx));
        FN_TRY(emit(&fc, ctx, OP_POP, line));
    }

    FN_TRY(compile_sexpr(&fc, body, true, ctx));
    FN_TRY(fnb_add_byte(&fc.builder, OP_RETURN, 0, &ctx->alloc));
#undef FN_TRY

    *out = fnb_build(&fc.builder);
    out->name = sv_str_copy(name, &ctx->alloc);
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

    fn_t fn_vm;
    TRY(compile_fn_vm(c, cls, params, body, name, 0, (transient_hashmap_t){0}, line, ctx, &fn_vm));

    if (!compile_upvalue_loads(c, cls, 0, ctx)) {
        fn_deinit(&fn_vm, &ctx->alloc);
        return false;
    }

    sv_opt_t(uint32_t) fi = fnb_add_closure(
        &c->builder,
        has_cls ? (uint8_t)cls->cons.size : 0,
        fn_vm,
        &ctx->alloc);
    if (!fi.is_some) {
        fn_deinit(&fn_vm, &ctx->alloc);
        return compiler_oom(ctx, line);
    }
    return add_var(c, name, line, ctx);
}

static bool compile_fun_group(compiler_t* c, const sexpr_t* members, int64_t n, int64_t line, ctx_t* ctx)
{
    int64_t first = c->builder.fn.chunk.functions.size;

    transient_hashmap_t member_names = {0};
#define G_TRY(call) do {                                                                                      \
    if (!(call)) {                                                                                            \
        thm_deinit(&member_names, &ctx->alloc);                                                               \
        return false;                                                                                         \
    } } while (0)

    G_TRY(check_limit(ctx, first, UINT32_MAX, "functions", line));
    G_TRY(check_limit(ctx, n, UINT8_MAX, "closure group members", line));
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

        fn_t fn_vm;
        G_TRY(compile_fn_vm(c, cls, params, body, name, upvalue_base, member_names, line, ctx, &fn_vm));

        if (!fnb_add_function(&c->builder, fn_vm, &ctx->alloc).is_some) {
            fn_deinit(&fn_vm, &ctx->alloc);
            G_TRY(compiler_oom(ctx, line));
        }

        upvalue_base += has_cls ? cls->cons.size : 0;
    }

    for (int64_t i = 0; i < n; i++) {
        bool has_cls = members[i].cons.size == 4;
        G_TRY(compile_upvalue_loads(c, has_cls ? &members[i].cons.arr[1] : NULL, line, ctx));
    }

    G_TRY(check_limit(ctx, upvalue_base, UINT8_MAX, "closure group upvalues", line));
    G_TRY(emit_wide(c, ctx, OP_CREATE_GROUP, (uint32_t)first, line));
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

static int64_t live_locals(const compiler_t* c)
{
    return c->locals == NULL ? 0 : c->locals->offset + names_count(c->locals->name_indexes);
}

/**
 * `(| A B)`: evaluate A, and if control inside it reaches `$fail`, evaluate B
 * instead. Each arm gets its own scope so A cannot add a local to the scope the
 * fail target was measured against.
 */
static bool compile_fatbar(compiler_t* c, const sexpr_t* args, int64_t n,
                           int64_t line, bool is_tail, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "alternative", line);

    fail_target_t target = {
        .jumps = sv_vec_init(int64_t),
        .locals = live_locals(c),
        .next = c->fail_targets,
    };
    c->fail_targets = &target;

    bool ok = init_scope(c, ctx, line)
        && compile_sexpr(c, args[0], is_tail, ctx)
        && deinit_scope(c, ctx);

    int64_t over = 0;
    ok = ok && jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), &over, line);

    c->fail_targets = target.next;
    for (int64_t i = 0; ok && i < target.jumps.size; i++)
        ok = patch_jump(c, ctx, target.jumps.arr[i], line);
    sv_vec_deinit(&target.jumps, &ctx->alloc);

    return ok
        && init_scope(c, ctx, line)
        && compile_sexpr(c, args[1], is_tail, ctx)
        && deinit_scope(c, ctx)
        && patch_jump(c, ctx, over, line);
}

/**
 * `$fail`: unwind the locals bound since the enclosing alternative, then jump to
 * its second arm.
 */
static bool compile_fail(compiler_t* c, int64_t line, ctx_t* ctx)
{
    fail_target_t* target = c->fail_targets;
    if (target == NULL)
        return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, "No alternative to fail to");

    TRY(emit_pop_locals(c, ctx, live_locals(c) - target->locals, line));

    int64_t j = 0;
    TRY(jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), &j, line));

    int success;
    sv_vec_push(&target->jumps, j, &success, &ctx->alloc);
    return success != 0 ? true : compiler_oom(ctx, line);
}

static bool is_form(sv_str_t name, const char* form)
{
    return sv_str_comp(name, sv_str_init(form));
}

/**
 * `(is-tuple? c k)` and `(is-record? c k)` put the size in the operand, and
 * `(is-record? c)` matches a record of any size.
 */
static bool compile_sized_test(compiler_t* c, sv_str_t name, const sexpr_t* args, int64_t n,
                               int64_t line, ctx_t* ctx)
{
    bool tuple = is_form(name, "is-tuple?");
    if (n == 1 && !tuple) {
        TRY(compile_sexpr(c, args[0], false, ctx));
        return emit(c, ctx, OP_IS_RECORD_ANY, line);
    }
    if (n != 2 || args[1].tag != S_ATOM || args[1].atom.literal.kind != LITERAL_NUMBER)
        return compiler_malformed(ctx, tuple ? "is-tuple?" : "is-record?", line);

    TRY(compile_sexpr(c, args[0], false, ctx));
    return emit_narrow(c, ctx, tuple ? OP_IS_TUPLE : OP_IS_RECORD,
                       (int64_t)args[1].atom.literal.number,
                       tuple ? "tuple elements" : "record fields", line);
}

/**
 * `(has-field? c name)` interns the field the way a field access does, so the id
 * lands in the operand.
 */
static bool compile_has_field(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        return compiler_malformed(ctx, "has-field?", line);

    sv_str_t name;
    TRY(expect_id(args[1], ctx, &name));
    uint32_t id = 0;
    TRY(record_field_id(c, name, line, ctx, &id));

    TRY(compile_sexpr(c, args[0], false, ctx));
    return emit2(c, ctx, OP_HAS_FIELD, (uint8_t)id, line);
}

/**
 * `(list-uncons subject head tail body)` binds both parts from one opcode. The
 * binds are raw so neither value is left on the stack.
 */
static bool compile_uncons(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 3)
        return compiler_malformed(ctx, "list-uncons", line);

    sv_str_t head;
    sv_str_t tail;
    TRY(expect_id(args[1], ctx, &head));
    TRY(expect_id(args[2], ctx, &tail));

    TRY(compile_sexpr(c, args[0], false, ctx));
    TRY(emit(c, ctx, OP_LIST_UNCONS, line));

    if (c->locals) {
        TRY(emit(c, ctx, OP_SET_LOCAL, line));
        TRY(locals_add(c->locals, head, ctx, line));
        TRY(emit(c, ctx, OP_SET_LOCAL, line));
        TRY(locals_add(c->locals, tail, ctx, line));
    } else {
        TRY(emit(c, ctx, OP_SET_GLOBAL, line));
        TRY(!globals_add(&c->globals, head, sv_str_init(c->current_path), line, ctx).is_some);
        TRY(emit(c, ctx, OP_SET_GLOBAL, line));
        TRY(!globals_add(&c->globals, tail, sv_str_init(c->current_path), line, ctx).is_some);
    }

    return true;
}

/**
 * Compiles a form the match lowering emits, setting handled when the name is
 * one. These names hold characters the scanner rejects, so user code can never
 * reach them.
 */
static bool compile_internal(compiler_t* c, sv_str_t name, const sexpr_t* args, int64_t n,
                             int64_t line, ctx_t* ctx, bool* handled)
{
    *handled = true;
    for (int64_t i = 0; i < (int64_t)(sizeof(TYPE_TESTS) / sizeof(TYPE_TESTS[0])); i++) {
        if (!is_form(name, TYPE_TESTS[i].name))
            continue;
        if (n != 1)
            return compiler_malformed(ctx, TYPE_TESTS[i].name, line);
        TRY(compile_sexpr(c, args[0], false, ctx));
        return emit(c, ctx, TYPE_TESTS[i].op, line);
    }

    if (is_form(name, "is-tuple?") || is_form(name, "is-record?"))
        return compile_sized_test(c, name, args, n, line, ctx);
    if (is_form(name, "has-field?"))
        return compile_has_field(c, args, n, line, ctx);
    if (is_form(name, "match-fail")) {
        if (n != 1)
            return compiler_malformed(ctx, "match-fail", line);
        TRY(compile_sexpr(c, args[0], false, ctx));
        return emit(c, ctx, OP_NO_MATCH, line);
    }
    if (is_form(name, "is-hashmap?") && n == 1) {
        TRY(compile_sexpr(c, args[0], false, ctx));
        return emit(c, ctx, OP_IS_HASHMAP_ANY, line);
    }
    if (is_form(name, "is-hashmap?") || is_form(name, "has-key?")) {
        if (n != 2)
            return compiler_malformed(ctx, "keyed test", line);
        TRY(compile_sexpr(c, args[is_form(name, "has-key?") ? 0 : 1], false, ctx));
        TRY(compile_sexpr(c, args[is_form(name, "has-key?") ? 1 : 0], false, ctx));
        return emit(c, ctx, is_form(name, "has-key?") ? OP_HAS_KEY : OP_IS_HASHMAP, line);
    }
    if (is_form(name, "list-uncons"))
        return compile_uncons(c, args, n, line, ctx);

    *handled = false;
    return true;
}

static bool compile_call(compiler_t* c, sv_str_t fn_name, const sexpr_t* args, int64_t n,
                         int64_t line, bool is_tail, ctx_t* ctx)
{
    for (int64_t i = 0; i < n; i++)
        TRY(compile_sexpr(c, args[i], false, ctx));
    TRY(compile_id(c, fn_name, line, ctx));
    uint8_t op = is_tail ? OP_TAIL_CALL : OP_CALL;
    return emit_narrow(c, ctx, op, n, "arguments", line);
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

static bool compile_import(compiler_t* c, const sexpr_t* args, int64_t line, ctx_t* ctx)
{
#define CLEANUP() do {\
    sv_free(&ctx->alloc, source_code); \
    value_free(&path_value, &ctx->alloc); \
    value_free(&module_name, &ctx->alloc); \
} while (0)

    char* source_code = NULL;
    value_t path_value = value_nil;
    value_t module_name = value_nil;

    sv_str_t name;
    TRY(expect_id(args[0], ctx, &name));
    module_name = value_init_str(name, &ctx->alloc);
    if (module_name.obj.cell == NULL)
        return false;

    // get import file name
    char current_dir[FILENAME_MAX];
    size_t length;
    cwk_path_get_dirname(c->current_path, &length);
    current_dir[length] = '\0';
    memcpy(current_dir, c->current_path, length);

    char* import_path = sv_str_to_c_str(args[1].atom.literal.str, &ctx->alloc);
    if (import_path == NULL)
        goto error_oom;

    char import_full_path[FILENAME_MAX];
    cwk_path_join(current_dir, import_path, import_full_path, sizeof(import_full_path));
    sv_free(&ctx->alloc, import_path);

    // check if module has already been compiled
    sv_str_t full_path_str = sv_str_init(import_full_path);
    path_value = value_init_str(full_path_str, &ctx->alloc);
    if (path_value.obj.cell == NULL)
        goto error_oom;

    if (thm_get(c->modules.compiled_modules, path_value).is_some) {
        CLEANUP();
        return true;
    }
    // add it to modules to be compiled
    if (thm_get(c->modules.to_be_compiled_modules, path_value).is_some) {
        CLEANUP();
        return compiler_error(ctx, (int)C_ERR_IMPORT_CICLE, "Import cicle detected");
    }
    if (!thm_put(&c->modules.to_be_compiled_modules, (kv_t){ .key = path_value }, &ctx->alloc))
        goto error_oom;

    // read and compile file
    source_code = read_file(import_full_path, &ctx->alloc);
    if (source_code == NULL)
        goto error_oom;

    const char* current_path = c->current_path;
    transient_hashmap_t var_to_modules = c->var_to_modules;
    c->current_path = import_full_path;
    if (!compile_source(c, source_code, ctx)) {
        CLEANUP();
        return false;
    }
    if (!fnb_add_byte(&c->builder, OP_POP, 0, &ctx->alloc))
        goto error_oom;
    c->current_path = current_path;
    c->var_to_modules = var_to_modules;

    // insert file into compiled modules and remove it from to be compiled
    if (!thm_put(&c->modules.compiled_modules, (kv_t){ .key = path_value }, &ctx->alloc))
        goto error_oom;
    if (!thm_put(&c->var_to_modules, (kv_t){ .key = module_name, .value = path_value }, &ctx->alloc))
        goto error_oom;
    thm_delete(&c->modules.to_be_compiled_modules, path_value, &ctx->alloc);

    CLEANUP();
    return add_const(c, ctx, value_nil, line);

error_oom:
    CLEANUP();
    return compiler_oom(ctx, line);
#undef CLEANUP
}

static bool compile_cons(compiler_t* c, const sexpr_t* cons, int64_t n, bool is_tail, ctx_t* ctx)
{
    if (n == 0)
        return true;

    sexpr_t head = cons[0];
    if (head.tag == S_CONS) {
        for (int64_t i = 1; i < n; i++)
            TRY(compile_sexpr(c, cons[i], false, ctx));
        TRY(compile_cons(c, head.cons.arr, head.cons.size, false, ctx));
        uint8_t op = is_tail ? OP_TAIL_CALL : OP_CALL;
        return emit_narrow(c, ctx, op, n - 1, "arguments", 0);
    }

    token_t a = head.atom;
    if (a.kind == TOKEN_OPERATOR)
        return compile_operator(c, a.operator, cons + 1, n - 1, a.line, is_tail, ctx);
    if (a.kind == TOKEN_SP_FUNCTION) {
        switch (a.fn) {
            case FN_IF: return compile_if(c, cons + 1, n - 1, a.line, is_tail, ctx);
            case FN_FOR: return compile_for(c, cons + 1, n - 1, a.line, ctx);
            case FN_LIST: return compile_list(c, cons + 1, n - 1, a.line, ctx);
            case FN_HASHMAP: return compile_hashmap(c, cons + 1, n - 1, a.line, ctx);
            case FN_RECORD: return compile_record(c, cons + 1, n - 1, a.line, ctx);
            case FN_TUPLE: return compile_tuple(c, cons + 1, n - 1, a.line, ctx);
            case FN_FUN: return compile_fun(c, cons + 1, n - 1, a.line, ctx);
            case FN_LENGTH: return compile_length(c, cons + 1, n - 1, a.line, ctx);
            case FN_RECORD_GET_OR_NIL: return compile_record_get_or_nil(c, cons + 1, n - 1, a.line, ctx);
            case FN_HASHMAP_GET_OR_NIL: return compile_hashmap_get_or_nil(c, cons + 1, n - 1, a.line, ctx);
            case FN_IMPORT: return compile_import(c, cons + 1, a.line, ctx);
            case FN_MATCH: return compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR,
                                                 "Unlowered match expression");
            case FN_MAP:
            case FN_MAPF:
            case FN_REDUCE:
            case FN_WHILE: {
                char msg[96];
                snprintf(msg, sizeof(msg), "Compiler '%s' not implemented at line %" PRId64, special_fn_text(a.fn), a.line);
                return compiler_error(ctx, C_ERR_NOT_IMPLEMENTED, msg);
            }
        }
    }
    if (a.kind == TOKEN_KEYWORD && a.keyword == KEYWORD_DO)
        return compile_do(c, cons + 1, n - 1, a.line, is_tail, ctx);
    if (a.kind == TOKEN_KEYWORD && (a.keyword == KEYWORD_AND || a.keyword == KEYWORD_OR))
        return compile_and_or(c, a.keyword == KEYWORD_AND, cons + 1, n - 1, a.line, ctx);
    if (a.kind == TOKEN_KEYWORD && a.keyword == KEYWORD_NOT)
        return compile_not(c, cons + 1, n - 1, a.line, ctx);
    if (a.kind == TOKEN_PIPE)
        return compile_fatbar(c, cons + 1, n - 1, a.line, is_tail, ctx);
    if (a.kind == TOKEN_LITERAL && a.literal.kind == LITERAL_IDENTIFIER) {
        bool handled = false;
        TRY(compile_internal(c, a.literal.literal, cons + 1, n - 1, a.line, ctx, &handled));
        if (handled)
            return true;
        return compile_call(c, a.literal.literal, cons + 1, n - 1, a.line, is_tail, ctx);
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "Value is not callable at line %" PRId64, a.line);
    return compiler_error(ctx, C_ERR_NOT_CALLABLE, msg);
}

bool compile_sexpr(compiler_t* c, sexpr_t sexpr, bool is_tail, ctx_t* ctx)
{
    if (sexpr.tag == S_ATOM)
        return compile_atom(c, sexpr.atom, ctx);

    if (sexpr.cons.size > 1 && sexpr.cons.arr[0].tag == S_ATOM) {
        token_t head = sexpr.cons.arr[0].atom;
        if (head.kind == TOKEN_SP_FUNCTION && head.fn == FN_MATCH) {
            sv_arena_t arena = sv_arena_init(1 << 16);
            sexpr_t lower_match = match_compile(sexpr, ctx, &arena);
            if (is_error_sexpr(lower_match)) {
                ctx->err.msg = sv_str_copy(ctx->err.msg, &ctx->alloc);
                sv_arena_deinit(&arena);
                return false;
            }
            bool ret = compile_sexpr(c, lower_match, is_tail, ctx);
            sv_arena_deinit(&arena);
            return ret;
        }
    }

    return compile_cons(c, sexpr.cons.arr, sexpr.cons.size, is_tail, ctx);
}

static bool add_native_fn(compiler_t* c, native_fn_t fn, ctx_t* ctx)
{
    TRY(!globals_add(&c->globals, sv_str_init(fn.name), sv_str_init(""), 0, ctx).is_some);
    value_t fn_val = value_init_native(fn, &ctx->alloc);
    TRY(fn_val.obj.cell != NULL);
    int success = 0;
    sv_vec_push(&c->global_values, fn_val, &success, &ctx->alloc);
    if (!success) {
        value_free(&fn_val, &ctx->alloc);
        return false;
    }
    return true;
}

vm_t compile(const char* base_path, compile_opts_t opts, ctx_t* ctx)
{
    return compile_files(&base_path, 1, opts, ctx);
}

vm_t compile_files(const char** files, int64_t count, compile_opts_t opts, ctx_t* ctx)
{
#define ERR_RETURN do {                                                                                       \
        compiler_free(&compiler, &ctx->alloc);                                                                \
        fn_deinit(&compiler.builder.fn, &ctx->alloc);                                                         \
        thm_deinit(&record_fields, &ctx->alloc);                                                              \
        return (vm_t){0}; } while (0)

    bool success;
    compiler_t compiler = compiler_init("", ctx, &success);
    if (!success)
        return (vm_t){0};

    transient_hashmap_t record_fields = { .depth = 0 };
    compiler.record_fields = &record_fields;

    /* Reserved for the ids map iteration builds its records with. */
    uint32_t reserved = 0;
    if (!record_field_id(&compiler, sv_str_init("key"), 0, ctx, &reserved))
        ERR_RETURN;
    if (!record_field_id(&compiler, sv_str_init("value"), 0, ctx, &reserved))
        ERR_RETURN;

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

    for (int64_t i = 0; i < opts.native_count; i++)
        if(!add_native_fn(&compiler, opts.native_funs[i], ctx))
            ERR_RETURN;

    for (int64_t i = 0; i < count; i++) {
        char* source_code = read_file(files[i], &ctx->alloc);
        if (source_code == NULL) {
            compiler_oom(ctx, 0);
            ERR_RETURN;
        }
        compiler.current_path = files[i];

        bool ok = compile_source(&compiler, source_code, ctx);
        sv_free(&ctx->alloc, source_code);
        if (!ok)
            ERR_RETURN;

        if (i < count - 1 && !fnb_add_byte(&compiler.builder, OP_POP, 0, &ctx->alloc)) {
            compiler_oom(ctx, 0);
            ERR_RETURN;
        }
    }
    if (!fnb_add_byte(&compiler.builder, OP_RETURN, 0, &ctx->alloc)) {
        compiler_oom(ctx, 0);
        ERR_RETURN;
    }

    vm_t vm = vm_init(fnb_build(&compiler.builder), opts.max_frames, compiler.globals);
    vm.globals = compiler.global_values;

    compiler.global_values = sv_vec_init(value_t);
    compiler_free(&compiler, &ctx->alloc);
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

#define SV_ARENA_IMPLEMENTATION
#include "std/arena.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "compiler.h"
#include "obj/map.h"
#include "scanner.h"
#include "sexpr.h"
#include "parser.h"
#include "std/string.h"
#include "std_native.h"
#include "pattern_shape.h"
#include "pattern_match.h"
#include "deps/cwalk.h"

static void compile_sexpr(compiler_t* c, sexpr_t sexpr, bool is_tail, ctx_t* ctx);
static bool compile_import(compiler_t* c, const sexpr_t* args, int64_t line, ctx_t* ctx);

typedef struct {
    const char* name;
    vm_instructions op;
} type_test_t;

static const type_test_t TYPE_TESTS[] = {
    { "is-str?", OP_IS_STR },
    { "is-number?", OP_IS_NUMBER },
    { "is-atom?", OP_IS_ATOM },
    { "is-bool?", OP_IS_BOOL },
    { "is-nil?", OP_IS_NIL },
    { "is-list?", OP_IS_LIST },
    { "is-cons?", OP_IS_CONS },
    { "is-nil-list?", OP_IS_NIL_LIST },
};

_Noreturn static void compiler_error(ctx_t* ctx, compiler_error_kind kind, const char* msg)
{
    ctx_fail(ctx, (int)kind, msg);
}

_Noreturn static void compiler_oom(ctx_t* ctx, int64_t line)
{
    ctx_fail_oom(ctx, (int)C_ERR_OOM, line);
}

/**
 * Allocates a compiler in the arena.
 */
static compiler_t* compiler_init(const char* base_path, sv_arena_t* arena, const sv_allocator_t* scratch, ctx_t* ctx)
{
    compiler_t* c = sv_arena_malloc(arena, sizeof(compiler_t));
    module_map_t modules = {
        .compiled_modules = thm_init(8, &ctx->alloc),
        .to_be_compiled_modules = thm_init(8, &ctx->alloc),
    };
    if (modules.compiled_modules.set.store.cell == NULL
        || modules.to_be_compiled_modules.set.store.cell == NULL
    ) {
        thm_deinit(&modules.compiled_modules, &ctx->alloc);
        thm_deinit(&modules.to_be_compiled_modules, &ctx->alloc);
        compiler_oom(ctx, 0);
    }

    *c = (compiler_t){
        .globals = { .name_indexes = { .depth = 0 } },
        .upvalues = { .name_indexes = { .depth = 0 }, .next = NULL, .offset = 0 },
        .members = { .depth = 0 },
        .builder = fnb_init(sv_str_init("")),
        .global_values = sv_vec_init(value_t),
        .current_path = base_path,
        .modules = modules,
        .arena = arena,
        .scratch = scratch,
    };
    return c;
}

static void modules_deinit(compiler_t* c, ctx_t* ctx)
{
    thm_deinit(&c->modules.compiled_modules, &ctx->alloc);
    thm_deinit(&c->modules.to_be_compiled_modules, &ctx->alloc);
    c->modules = (module_map_t){0};
}

/* Frees the name tables of the open scopes and of the upvalues. */
static void scopes_deinit(compiler_t* c, ctx_t* ctx)
{
    for (locals_t* l = c->locals; l != NULL; l = l->next)
        thm_deinit(&l->name_indexes, &ctx->alloc);
    c->locals = NULL;
    thm_deinit(&c->upvalues.name_indexes, &ctx->alloc);
}

/**
 * Frees what the compilation holds on ctx->alloc after it leaves through on_error.
 */
static void compiler_abort(compiler_t* c, ctx_t* ctx)
{
    compiler_t* innermost = c;
    for (compiler_t* fc = c; fc != NULL; fc = fc->child) {
        fn_deinit(&fc->builder.fn, &ctx->alloc);
        modules_deinit(fc, ctx);
        scopes_deinit(fc, ctx);
        innermost = fc;
    }
    for (module_scope_t* s = innermost->module_scope; s != NULL; s = s->outer)
        thm_deinit(&s->var_to_modules, &ctx->alloc);
    value_arr_deinit(&c->global_values, &ctx->alloc);
    thm_deinit(&c->globals.name_indexes, &ctx->alloc);
    thm_deinit(c->record_fields, &ctx->alloc);
    thm_deinit(c->atoms, &ctx->alloc);
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

_Noreturn static void compiler_error_name(ctx_t* ctx, compiler_error_kind kind, int64_t line, const char* what,
                                          sv_str_t id)
{
    char msg[192];
    snprintf(msg, sizeof(msg), "%s '%.*s' at line %" PRId64, what, (int)id.size, id.chars, line);
    compiler_error(ctx, kind, msg);
}

_Noreturn static void compiler_malformed(ctx_t* ctx, const char* what, int64_t line)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "Malformed %s expression at line %" PRId64, what, line);
    compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
}

static void emit(compiler_t* c, ctx_t* ctx, uint8_t byte, int64_t line)
{
    if (!fnb_add_byte(&c->builder, byte, line, &ctx->alloc))
        compiler_oom(ctx, line);
}

static void emit2(compiler_t* c, ctx_t* ctx, uint8_t b1, uint8_t b2, int64_t line)
{
    if (!fnb_add_bytes(&c->builder, b1, b2, line, &ctx->alloc))
        compiler_oom(ctx, line);
}

static void add_const(compiler_t* c, ctx_t* ctx, value_t v, int64_t line)
{
    if (!fnb_add_constant(&c->builder, v, &ctx->alloc).is_some) {
        value_free(&v, &ctx->alloc);
        compiler_oom(ctx, line);
    }
}

static bool is_import(sexpr_t e)
{
    return e.tag == S_CONS && e.cons.size > 0 && e.cons.arr[0].tag == S_ATOM
        && e.cons.arr[0].atom.kind == TOKEN_SP_FUNCTION && e.cons.arr[0].atom.fn == FN_IMPORT;
}

/* Compiles a top-level expression over the previous value, if any. False when nothing was emitted. */
static bool compile_top(compiler_t* c, sexpr_t sexpr, bool has_value, int64_t line, ctx_t* ctx)
{
    bool import = is_import(sexpr);
    if (import && !compile_import(c, sexpr.cons.arr + 1, line, ctx))
        return false;
    if (has_value)
        emit(c, ctx, OP_POP, line);
    if (import)
        add_const(c, ctx, value_nil, line);
    else
        compile_sexpr(c, sexpr, false, ctx);
    return true;
}

/**
 * Compiles every top-level expression of a file, releasing each one's scratch before the next.
 * Returns whether the file left a value.
 */
static bool compile_source(compiler_t* c, const char* source_code, ctx_t* ctx)
{
    scanner_t s = scanner_init(sv_str_init(source_code));
    bool top_level = c->module_scope == NULL;  // an imported file is compiled inside an expression, which releases it
    module_scope_t* scope = sv_malloc(c->scratch, sizeof(module_scope_t));
    *scope = (module_scope_t){ .var_to_modules = thm_init(8, &ctx->alloc), .outer = c->module_scope };
    if (scope->var_to_modules.set.store.cell == NULL)
        compiler_oom(ctx, 0);
    c->module_scope = scope;

    token_t token;
    bool has_value = false;
    for (;;) {
        parser_skip_semicolons(&s, ctx);
        token = scanner_peek(&s, ctx);
        if (token.kind == TOKEN_EOF || token.kind == TOKEN_ERROR)
            break;

        sv_arena_mark_t mark = sv_arena_mark(c->arena);
        if (compile_top(c, parser_expr(&s, ctx, c->scratch), has_value, token.line, ctx))
            has_value = true;
        if (top_level)
            sv_arena_reset(c->arena, mark);
    }
    if (token.kind == TOKEN_ERROR)
        longjmp(*ctx->on_error, 2);

    c->module_scope = scope->outer;
    thm_deinit(&scope->var_to_modules, &ctx->alloc);
    return has_value;
}

static void emit_narrow(compiler_t* c, ctx_t* ctx, uint8_t op, int64_t arg,
                        const char* what, int64_t line)
{
    check_limit(ctx, arg, UINT8_MAX, what, line);
    emit2(c, ctx, op, (uint8_t)arg, line);
}

static void emit_wide(compiler_t* c, ctx_t* ctx, uint8_t op, uint32_t arg, int64_t line)
{
    if (!fnb_add_arg(&c->builder, op, arg, line, &ctx->alloc))
        compiler_oom(ctx, line);
}

static void emit_pop_locals(compiler_t* c, ctx_t* ctx, int64_t count, int64_t line)
{
    if (count != 0)
        emit_wide(c, ctx, OP_POP_LOCAL, (uint32_t)count, line);
}

static int64_t jump_emit(ctx_t* ctx, sv_opt_t(int64_t) ji, int64_t line)
{
    if (!ji.is_some)
        compiler_oom(ctx, line);
    return ji.value;
}

static void patch_jump(compiler_t* c, ctx_t* ctx, int64_t ji, int64_t line)
{
    int64_t offset = c->builder.fn.chunk.bytecode.size - ji;
    if (offset > UINT16_MAX) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Jump too long at line %" PRId64, line);
        compiler_error(ctx, C_ERR_JUMP_TOO_LONG, msg);
    }
    fnb_patch_jump(&c->builder, ji, (uint16_t)offset);
}

/* Adds the name to the scope and returns its slot. */
static uint32_t locals_add(locals_t* l, sv_str_t id, ctx_t* ctx, int64_t line)
{
    check_limit(ctx, names_count(l->name_indexes) + l->offset, UINT32_MAX, "locals", line);
    bool existed = false;
    uint32_t idx = names_add(&l->name_indexes, id, &existed, ctx, &ctx->alloc);
    if (existed)
        compiler_error_name(ctx, C_ERR_REDEFINED, line, "Local redefined", id);
    return (uint32_t)(idx + l->offset);
}

static sv_opt_t(uint32_t) locals_get(const locals_t* l, sv_str_t id, const sv_allocator_t* a)
{
    for (; l != NULL; l = l->next) {
        sv_opt_t(uint32_t) idx = names_get(l->name_indexes, id, a);
        if (idx.is_some)
            return sv_opt_some_t(uint32_t, (uint32_t)(idx.value + l->offset));
    }
    return sv_opt_none_t(uint32_t);
}

/* The line of the first atom in a tree, or 0 when it has none. */
static int64_t sexpr_line(sexpr_t e)
{
    while (e.tag == S_CONS) {
        if (e.cons.size == 0)
            return 0;
        e = e.cons.arr[0];
    }
    return e.atom.line;
}

static sv_str_t expect_id(sexpr_t e, ctx_t* ctx)
{
    if (e.tag != S_ATOM || e.atom.kind != TOKEN_LITERAL || e.atom.literal.kind != LITERAL_IDENTIFIER) {
        int64_t line = sexpr_line(e);
        char msg[96];
        snprintf(msg, sizeof(msg), "Expected an identifier at line %" PRId64, line);
        compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
    }
    return e.atom.literal.literal;
}

static void compile_fail(compiler_t*, int64_t, ctx_t*);
static uint32_t record_field_id(compiler_t*, sv_str_t, int64_t, ctx_t*);
_Noreturn static void reject_pattern_only(ctx_t*, int64_t);
_Noreturn static void reject_shape(ctx_t*, const char*, int64_t);

static void compile_id(compiler_t* c, sv_str_t id, int64_t line, ctx_t* ctx)
{
    sv_opt_t(uint32_t) idx;
    if (sv_str_comp(id, sv_str_init("$fail")))
        compile_fail(c, line, ctx);
    else if ((idx = locals_get(c->locals, id, c->scratch)).is_some)
        emit_wide(c, ctx, OP_GET_LOCAL, idx.value, line);
    else if ((idx = locals_get(&c->upvalues, id, c->scratch)).is_some)
        emit_wide(c, ctx, OP_GET_UPVALUE, idx.value, line);
    else if ((idx = globals_get(c->globals, id, sv_str_init(c->current_path), c->scratch)).is_some)
        emit_wide(c, ctx, OP_GET_GLOBAL, idx.value, line);
    else if ((idx = names_get(c->members, id, c->scratch)).is_some)
        emit_narrow(c, ctx, OP_GET_MEMBER, idx.value, "closure group members", line);
    else
        compiler_error_name(ctx, C_ERR_UNDEFINED_VARIABLE, line, "Undefined variable", id);
}

static void add_var(compiler_t* c, sv_str_t id, int64_t line, ctx_t* ctx)
{
    if (c->locals != NULL) {
        emit(c, ctx, OP_SET_LOCAL, line);
        uint32_t idx = locals_add(c->locals, id, ctx, line);
        emit_wide(c, ctx, OP_GET_LOCAL, idx, line);
    } else {
        emit(c, ctx, OP_SET_GLOBAL, line);
        uint32_t idx = globals_add(&c->globals, id, sv_str_init(c->current_path), line, ctx, c->scratch);
        emit_wide(c, ctx, OP_GET_GLOBAL, idx, line);
    }
}

static void init_scope(compiler_t* c)
{
    locals_t* local = sv_malloc(c->scratch, sizeof(locals_t));
    *local = (locals_t){
        .name_indexes = { .depth = 0 },
        .next = c->locals,
        .offset = c->locals != NULL ? names_count(c->locals->name_indexes) + c->locals->offset : 0,
    };
    c->locals = local;
}

static void deinit_scope(compiler_t* c, ctx_t* ctx)
{
    locals_t* local = c->locals;
    if (local == NULL)
        return;
    c->locals = local->next;
    int64_t n = names_count(local->name_indexes);
    thm_deinit(&local->name_indexes, &ctx->alloc);
    emit_pop_locals(c, ctx, n, 0);
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
static void emit_assert(compiler_t* c, int64_t line, ctx_t* ctx)
{
    emit(c, ctx, OP_ASSERT_MATCH, line);
}

static void bind_pattern(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                         int64_t line, ctx_t* ctx);

/**
 * A first occurrence declares the name; a repeat compares against what the earlier
 * occurrence bound, so `(a, a) = e` requires both positions to be equal.
 */
static void bind_var(compiler_t* c, sv_str_t name, sv_vec_t(sv_str_t)* seen,
                     int64_t line, ctx_t* ctx)
{
    for (int64_t i = 0; i < seen->size; i++) {
        if (!sv_str_comp(seen->arr[i], name))
            continue;

        emit(c, ctx, OP_DUP, line);
        compile_id(c, name, line, ctx);
        emit(c, ctx, OP_EQUALS, line);
        emit_assert(c, line, ctx);
        return;
    }

    sv_vec_push(seen, name, NULL, c->scratch);
    add_var(c, name, line, ctx);
}

/**
 * Compiles one sub pattern against the value on the stack top, leaving that value
 * in place: `read` pushes the part, the recursion consumes and restores it, and
 * the pop returns to the parent's subject.
 */
static void bind_part(compiler_t* c, sexpr_t sub, sv_vec_t(sv_str_t)* seen,
                      int64_t line, ctx_t* ctx)
{
    bind_pattern(c, sub, seen, line, ctx);
    emit(c, ctx, OP_POP, line);
}

static void bind_tuple(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                       int64_t line, ctx_t* ctx)
{
    int64_t arity = pattern.cons.size - 1;
    emit(c, ctx, OP_DUP, line);
    emit_narrow(c, ctx, OP_IS_TUPLE, arity, "tuple elements", line);
    emit_assert(c, line, ctx);

    for (int64_t i = 0; i < arity; i++) {
        emit(c, ctx, OP_DUP, line);
        add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = (double)i }, line);
        emit(c, ctx, OP_INDEX, line);
        bind_part(c, pattern.cons.arr[1 + i], seen, line, ctx);
    }
}

static void bind_record(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                        int64_t line, ctx_t* ctx)
{
    if (spread_of(pattern.cons.arr + 1, pattern.cons.size - 1) != NULL)
        reject_shape(ctx, "A record or hashmap pattern cannot bind its rest, use a bare '..'", line);

    int64_t n = record_n_fields(pattern);
    emit(c, ctx, OP_DUP, line);
    if (record_is_open(pattern))
        emit(c, ctx, OP_IS_RECORD_ANY, line);
    else
        emit_narrow(c, ctx, OP_IS_RECORD, n, "record fields", line);
    emit_assert(c, line, ctx);

    for (int64_t i = 0; i < n; i++) {
        sv_str_t field = expect_id(pattern.cons.arr[1 + 2 * i], ctx);
        uint32_t id = record_field_id(c, field, line, ctx);

        emit(c, ctx, OP_DUP, line);
        emit2(c, ctx, OP_HAS_FIELD, (uint8_t)id, line);
        emit_assert(c, line, ctx);

        emit(c, ctx, OP_DUP, line);
        emit2(c, ctx, OP_RECORD_GET, (uint8_t)id, line);
        bind_part(c, pattern.cons.arr[2 + 2 * i], seen, line, ctx);
    }
}

static void bind_hashmap(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                         int64_t line, ctx_t* ctx)
{
    if (spread_of(pattern.cons.arr + 1, pattern.cons.size - 1) != NULL)
        reject_shape(ctx, "A record or hashmap pattern cannot bind its rest, use a bare '..'", line);

    int64_t n = hashmap_n_keys(pattern);
    emit(c, ctx, OP_DUP, line);
    if (hashmap_is_open(pattern)) {
        emit(c, ctx, OP_IS_HASHMAP_ANY, line);
    } else {
        add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = (double)n }, line);
        emit(c, ctx, OP_SWAP, line);
        emit(c, ctx, OP_IS_HASHMAP, line);
    }
    emit_assert(c, line, ctx);

    for (int64_t i = 0; i < n; i++) {
        sexpr_t key = pattern.cons.arr[1 + 2 * i];
        emit(c, ctx, OP_DUP, line);
        compile_sexpr(c, key, false, ctx);
        emit(c, ctx, OP_HAS_KEY, line);
        emit_assert(c, line, ctx);

        emit(c, ctx, OP_DUP, line);
        compile_sexpr(c, key, false, ctx);
        emit(c, ctx, OP_INDEX, line);
        bind_part(c, pattern.cons.arr[2 + 2 * i], seen, line, ctx);
    }
}

/**
 * Each element unconses the current remainder, so the tails stack up and are
 * popped together at the end. Without a `..` the remainder must be empty, which
 * is what pins the length.
 */
static void bind_list(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                      int64_t line, ctx_t* ctx)
{
    int64_t fixed = list_n_fixed(pattern);
    emit(c, ctx, OP_DUP, line);
    emit(c, ctx, OP_IS_LIST, line);
    emit_assert(c, line, ctx);

    for (int64_t i = 0; i < fixed; i++) {
        emit(c, ctx, OP_DUP, line);
        emit(c, ctx, OP_IS_CONS, line);
        emit_assert(c, line, ctx);

        emit(c, ctx, OP_LIST_UNCONS, line);
        bind_part(c, pattern.cons.arr[1 + i], seen, line, ctx);
    }

    if (list_has_tail(pattern)) {
        sexpr_t tail = pattern.cons.arr[pattern.cons.size - 1].cons.arr[1];
        if (!is_list_tail(tail))
            reject_shape(ctx, "List tail must be a variable or a list", line);
        bind_pattern(c, tail, seen, line, ctx);
    } else {
        emit(c, ctx, OP_DUP, line);
        emit(c, ctx, OP_IS_CONS, line);
        emit(c, ctx, OP_NOT, line);
        emit_assert(c, line, ctx);
    }

    for (int64_t i = 0; i < fixed; i++)
        emit(c, ctx, OP_POP, line);
}

static void bind_literal(compiler_t* c, sexpr_t pattern, int64_t line, ctx_t* ctx)
{
    emit(c, ctx, OP_DUP, line);
    compile_sexpr(c, pattern, false, ctx);
    emit(c, ctx, OP_EQUALS, line);
    emit_assert(c, line, ctx);
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
static void bind_alias(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                       int64_t line, ctx_t* ctx)
{
    if (!pattern_is_name(pattern.cons.arr[1]) && !pattern_is_name(pattern.cons.arr[2])) {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "One side of '=' in a pattern must be a name at line %" PRId64, line);
        compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
    }
    bind_pattern(c, pattern.cons.arr[1], seen, line, ctx);
    bind_pattern(c, pattern.cons.arr[2], seen, line, ctx);
}

/**
 * Compiles one pattern against the value on the stack top, leaving that value in
 * place. Every arm holds to that: the subject is on top on entry and on top on exit,
 * which is what lets the container cases read a part, recurse and pop back. A
 * variable leaf satisfies it for free, since add_var consumes the top and pushes the
 * value back.
 */
static void bind_pattern(compiler_t* c, sexpr_t pattern, sv_vec_t(sv_str_t)* seen,
                         int64_t line, ctx_t* ctx)
{
    if (pattern.tag == S_ATOM) {
        if (is_pattern_wildcard(pattern))
            return;
        if (pattern.atom.kind == TOKEN_LITERAL
            && pattern.atom.literal.kind == LITERAL_IDENTIFIER)
            bind_var(c, pattern.atom.literal.literal, seen, line, ctx);
        else
            bind_literal(c, pattern, line, ctx);
        return;
    }

    if (is_negative_number(pattern)) {
        bind_literal(c, pattern, line, ctx);
        return;
    }
    if (pattern_is_alias(pattern)) {
        bind_alias(c, pattern, seen, line, ctx);
        return;
    }

    token_t head = pattern.cons.arr[0].atom;
    if (head.kind != TOKEN_SP_FUNCTION)
        compiler_malformed(ctx, "destructuring pattern", line);

    if (head.fn == FN_LIST)
        bind_list(c, pattern, seen, line, ctx);
    else if (head.fn == FN_RECORD)
        bind_record(c, pattern, seen, line, ctx);
    else if (head.fn == FN_HASHMAP)
        bind_hashmap(c, pattern, seen, line, ctx);
    else if (head.fn == FN_TUPLE)
        bind_tuple(c, pattern, seen, line, ctx);
    else
        compiler_malformed(ctx, "destructuring pattern", line);
}

/**
 * Destructures the value on the stack top. There is no next alternative to fall
 * through to, so a value that does not fit raises rather than failing over.
 */
static void compile_destructure(compiler_t* c, sexpr_t pattern, int64_t line, ctx_t* ctx)
{
    sv_vec_t(sv_str_t) seen = sv_vec_init(sv_str_t);
    bind_pattern(c, pattern, &seen, line, ctx);
}

static void compile_equal(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "assignment", line);

    if (args[0].tag == S_CONS || is_pattern_wildcard(args[0])
        || args[0].atom.kind != TOKEN_LITERAL
        || args[0].atom.literal.kind != LITERAL_IDENTIFIER) {
        compile_sexpr(c, args[1], false, ctx);
        compile_destructure(c, args[0], line, ctx);
        return;
    }

    sv_str_t id = expect_id(args[0], ctx);
    compile_sexpr(c, args[1], false, ctx);
    add_var(c, id, line, ctx);
}

static void compile_pipe(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, bool is_tail, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "pipe", line);
    compile_sexpr(c, args[0], false, ctx);

    sexpr_t rhs = args[1];
    if (rhs.tag != S_CONS || rhs.cons.size == 0)
        compiler_malformed(ctx, "pipe", line);

    sv_str_t fn_name = expect_id(rhs.cons.arr[0], ctx);
    for (int64_t i = 1; i < rhs.cons.size; i++)
        compile_sexpr(c, rhs.cons.arr[i], false, ctx);
    compile_id(c, fn_name, line, ctx);
    uint8_t op = is_tail ? OP_TAIL_CALL : OP_CALL;
    emit_narrow(c, ctx, op, rhs.cons.size, "arguments", line);
}

static uint32_t record_field_id(compiler_t* c, sv_str_t name, int64_t line, ctx_t* ctx)
{
    bool existed = false;
    uint32_t id = names_add(c->record_fields, name, &existed, ctx, &ctx->alloc);
    if (id > UINT8_MAX) {
        char msg[96];
        snprintf(msg, sizeof(msg), "More than %d record fields at line %" PRId64, UINT8_MAX + 1, line);
        compiler_error(ctx, C_ERR_LIMIT_EXCEEDED, msg);
    }
    return id;
}

static void compile_tuple(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n < 1 || n > UINT8_MAX)
        compiler_malformed(ctx, "tuple", line);

    for (int64_t i = 0; i < n; i++)
        compile_sexpr(c, args[i], false, ctx);
    emit2(c, ctx, OP_TUPLE, (uint8_t)n, line);
}

static void compile_record(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n > 0 && args[n - 1].tag == S_ATOM && args[n - 1].atom.kind == TOKEN_DOT_DOT)
        reject_pattern_only(ctx, args[n - 1].atom.line);

    const sexpr_t* base = spread_of(args, n);
    int64_t n_kvs = base == NULL ? n : n - 1;
    if (n_kvs % 2 != 0 || n_kvs / 2 > UINT8_MAX)
        compiler_malformed(ctx, "record", line);

    struct { uint32_t id; sv_str_t name; const sexpr_t* value; } fields[UINT8_MAX];
    int64_t n_fields = n_kvs / 2;
    for (int64_t i = 0; i < n_fields; i++) {
        sv_str_t name = expect_id(args[2 * i], ctx);
        uint32_t id = record_field_id(c, name, line, ctx);

        int64_t j = i;
        for (; j > 0 && fields[j - 1].id > id; j--)
            fields[j] = fields[j - 1];
        fields[j].id = id;
        fields[j].name = name;
        fields[j].value = &args[2 * i + 1];
    }

    for (int64_t i = 1; i < n_fields; i++)
        if (fields[i - 1].id == fields[i].id)
            compiler_error_name(ctx, C_ERR_REDEFINED, line, "Record field", fields[i].name);

    for (int64_t i = 0; i < n_fields; i++) {
        add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = (double)fields[i].id }, line);
        compile_sexpr(c, *fields[i].value, false, ctx);
    }
    if (base == NULL) {
        emit2(c, ctx, OP_RECORD, (uint8_t)n_fields, line);
        return;
    }

    // id_1 v_1 ... base => record_update n
    compile_sexpr(c, *base, false, ctx);
    emit2(c, ctx, OP_RECORD_UPDATE, (uint8_t)n_fields, line);
}

static void compile_dot(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "field access", line);

    sv_str_t name = expect_id(args[1], ctx);
    uint32_t id = record_field_id(c, name, line, ctx);

    compile_sexpr(c, args[0], false, ctx);
    emit2(c, ctx, OP_RECORD_GET, (uint8_t)id, line);
}

static void compile_double_colon(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "field access", line);

    sv_str_t module_name = expect_id(args[0], ctx);
    sv_str_t var_name = expect_id(args[1], ctx);

    value_t module_name_value = value_init_str(module_name, c->scratch);
    sv_opt_t(value_t) module_path = thm_get(c->module_scope->var_to_modules, module_name_value);
    if (!module_path.is_some)
        compiler_error_name(ctx, C_ERR_UNDEFINED_VARIABLE, line, "Undefined variable", module_name);

    sv_opt_t(uint32_t) id = globals_get(c->globals, var_name, AS_STR(module_path.value), c->scratch);
    if (!id.is_some)
        compiler_error_name(ctx, C_ERR_UNDEFINED_VARIABLE, line, "Undefined variable", var_name);

    emit_wide(c, ctx, OP_GET_GLOBAL, id.value, line);
}

static void compile_record_get_or_nil(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "field access", line);

    sv_str_t name = expect_id(args[1], ctx);
    uint32_t id = record_field_id(c, name, line, ctx);

    compile_sexpr(c, args[0], false, ctx);
    emit2(c, ctx, OP_RECORD_GET_OR_UNDEF, (uint8_t)id, line);
}

static void compile_hashmap_get_or_nil(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "field access", line);

    compile_sexpr(c, args[0], false, ctx);
    compile_sexpr(c, args[1], false, ctx);
    emit(c, ctx, OP_HASHMAP_GET_OR_UNDEF, line);
}

static void compile_length(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 1)
        compiler_malformed(ctx, "length", line);

    compile_sexpr(c, args[0], false, ctx);
    emit(c, ctx, OP_LENGTH, line);
}

static void compile_binary_op(compiler_t* c, uint8_t instruction, const char* what,
                              const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, what, line);
    compile_sexpr(c, args[0], false, ctx);
    compile_sexpr(c, args[1], false, ctx);
    emit(c, ctx, instruction, line);
}

static void compile_operator(compiler_t* c, operator_kind op, const sexpr_t* args,
                             int64_t n, int64_t line, bool is_tail, ctx_t* ctx)
{
    uint8_t instruction = 0;
    switch (op) {
        case OPERATOR_PLUS: instruction = OP_ADD; break;
        case OPERATOR_MINUS: instruction = n == 1 ? OP_NEGATE : OP_SUB; break;
        case OPERATOR_SLASH: instruction = OP_DIV; break;
        case OPERATOR_STAR: instruction = OP_MUL; break;
        case OPERATOR_EQUAL: compile_equal(c, args, n, line, ctx); return;
        case OPERATOR_EQUAL_EQUAL: compile_binary_op(c, OP_EQUALS, "comparison", args, n, line, ctx); return;
        case OPERATOR_BANG_EQUAL: compile_binary_op(c, OP_NOT_EQUALS, "comparison", args, n, line, ctx); return;
        case OPERATOR_GREATER: compile_binary_op(c, OP_GREATER, "comparison", args, n, line, ctx); return;
        case OPERATOR_GREATER_EQUAL: compile_binary_op(c, OP_GREATER_EQUAL, "comparison", args, n, line, ctx); return;
        case OPERATOR_LESS: compile_binary_op(c, OP_LESS, "comparison", args, n, line, ctx); return;
        case OPERATOR_LESS_EQUAL: compile_binary_op(c, OP_LESS_EQUAL, "comparison", args, n, line, ctx); return;
        case OPERATOR_LEFT_BRACKET: compile_binary_op(c, OP_INDEX, "index", args, n, line, ctx); return;
        case OPERATOR_PIPE_FORWARD: compile_pipe(c, args, n, line, is_tail, ctx); return;
        case OPERATOR_DOT: compile_dot(c, args, n, line, ctx); return;
        case OPERATOR_DOUBLE_COLON: compile_double_colon(c, args, n, line, ctx); return;
        case OPERATOR_LEFT_PAREN: {
            char msg[96];
            snprintf(msg, sizeof(msg), "Operator not implemented at line %" PRId64, line);
            compiler_error(ctx, C_ERR_NOT_IMPLEMENTED, msg);
        }
    }

    for (int64_t i = 0; i < n; i++)
        compile_sexpr(c, args[i], false, ctx);
    emit(c, ctx, instruction, line);
}

static void compile_if(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, bool is_tail, ctx_t* ctx)
{
    if (n != 2 && n != 3)
        compiler_malformed(ctx, "if", line);

    init_scope(c);
    compile_sexpr(c, args[0], false, ctx);

    int64_t j1 = jump_emit(ctx, fnb_add_jump_if_false(&c->builder, line, &ctx->alloc), line);
    compile_sexpr(c, args[1], is_tail, ctx);

    int64_t j2 = jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), line);
    patch_jump(c, ctx, j1, line);
    if (n == 3)
        compile_sexpr(c, args[2], is_tail, ctx);
    else
        add_const(c, ctx, (value_t){ .kind = VALUE_NIL }, line);

    patch_jump(c, ctx, j2, line);
    deinit_scope(c, ctx);
}

#define LOOP_SETUP()                                                                                          \
    const sexpr_t* binding = args[0].cons.arr;                                                                \
    sv_str_t collection_name = sv_str_init(" $collection ");                                                  \
    sv_str_t iter_name = sv_str_init(" $iter ");                                                              \
                                                                                                              \
    init_scope(c);                                                                                            \
                                                                                                              \
    compile_sexpr(c, binding[1], false, ctx);                                                                 \
    emit(c, ctx, OP_SET_LOCAL, line);                                                                         \
    uint32_t collection_slot = locals_add(c->locals, collection_name, ctx, line);                             \
    emit_wide(c, ctx, OP_GET_LOCAL, collection_slot, line);                                                   \
    emit(c, ctx, OP_ITER_CREATE, line);                                                                       \
    emit(c, ctx, OP_SET_LOCAL, line);                                                                         \
    uint32_t iter_slot = locals_add(c->locals, iter_name, ctx, line);                                         \
                                                                                                              \
    init_scope(c);                                                                                            \
    int64_t loop_start = c->builder.fn.chunk.bytecode.size;                                                   \
                                                                                                              \
    emit_wide(c, ctx, OP_GET_LOCAL, iter_slot, line);                                                         \
    emit(c, ctx, OP_ITER_NEXT, line);                                                                         \
                                                                                                              \
    bool destructure = binding[0].tag == S_CONS;                                                              \
    sv_str_t id = destructure ? sv_str_init(" $item ") : expect_id(binding[0], ctx);                          \
    emit(c, ctx, OP_SET_LOCAL, line);                                                                         \
    uint32_t id_slot = locals_add(c->locals, id, ctx, line);                                                  \
    emit_wide(c, ctx, OP_GET_LOCAL, id_slot, line);                                                           \
                                                                                                              \
    int64_t j1 = jump_emit(ctx, fnb_add_jump_if_false(&c->builder, line, &ctx->alloc), line)

#define LOOP_BODY()                                                                                           \
    if (destructure) {                                                                                        \
        init_scope(c);                                                                                        \
        emit_wide(c, ctx, OP_GET_LOCAL, id_slot, line);                                                       \
        compile_destructure(c, binding[0], line, ctx);                                                        \
        emit(c, ctx, OP_POP, line);                                                                           \
    }                                                                                                         \
    compile_sexpr(c, args[1], false, ctx);                                                                    \
    if (destructure)                                                                                          \
        deinit_scope(c, ctx);                                                                                 \
    emit_pop_locals(c, ctx, names_count(c->locals->name_indexes), 0);                                         \
    if (!fnb_add_jump_back(&c->builder, loop_start, line, &ctx->alloc))                                       \
        compiler_oom(ctx, line);                                                                              \
    patch_jump(c, ctx, j1, line)

static void compile_for(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    assert(n == 2);

    add_const(c, ctx, value_nil, line);

    LOOP_SETUP();
    emit(c, ctx, OP_POP, line);
    LOOP_BODY();

    deinit_scope(c, ctx);
    deinit_scope(c, ctx);
}

static void compile_map(compiler_t* c, const sexpr_t* args, int64_t n,
                        vm_instructions op_code, int64_t line, ctx_t* ctx)
{
    assert(n == 2);

    LOOP_SETUP();
    LOOP_BODY();

    emit_wide(c, ctx, OP_GET_LOCAL, collection_slot, line);
    emit(c, ctx, OP_LENGTH, line);
    emit(c, ctx, op_code, line);

    deinit_scope(c, ctx);
    deinit_scope(c, ctx);
}

_Noreturn static void reject_pattern_only(ctx_t* ctx, int64_t line)
{
    char msg[96];
    snprintf(msg, sizeof(msg),
             "'..' is only valid in a pattern or on the left of '=' at line %" PRId64, line);
    compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
}

_Noreturn static void reject_shape(ctx_t* ctx, const char* what, int64_t line)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "%s at line %" PRId64, what, line);
    compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
}

static void compile_list(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    const sexpr_t* tail = spread_of(args, n);
    int64_t fixed = tail == NULL ? n : n - 1;
    for (int64_t i = 0; i < fixed; i++) {
        if (spread_of(&args[i], 1) != NULL)
            compiler_malformed(ctx, "list spread", line);
        compile_sexpr(c, args[i], false, ctx);
    }
    if (tail == NULL) {
        check_limit(ctx, n, UINT32_MAX, "list elements", line);
        emit_wide(c, ctx, OP_LIST, (uint32_t)n, line);
        return;
    }

    // e_1 ... e_n tail => list_prepend n
    compile_sexpr(c, *tail, false, ctx);
    check_limit(ctx, fixed, UINT32_MAX, "list elements", line);
    emit_wide(c, ctx, OP_LIST_PREPEND, (uint32_t)fixed, line);
}

static void compile_hashmap(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n > 0 && args[n - 1].tag == S_ATOM && args[n - 1].atom.kind == TOKEN_DOT_DOT)
        reject_pattern_only(ctx, args[n - 1].atom.line);

    const sexpr_t* base = spread_of(args, n);
    int64_t n_kvs = base == NULL ? n : n - 1;
    if (n_kvs % 2 != 0)
        compiler_malformed(ctx, "hashmap", line);
    check_limit(ctx, n_kvs / 2, UINT32_MAX, "hashmap entries", line);

    for (int64_t i = 0; i < n_kvs; i++)
        compile_sexpr(c, args[i], false, ctx);
    if (base == NULL) {
        emit_wide(c, ctx, OP_HASHMAP, (uint32_t)(n_kvs / 2), line);
        return;
    }

    // k_1 v_1 ... base => hashmap_update n
    compile_sexpr(c, *base, false, ctx);
    emit_wide(c, ctx, OP_HASHMAP_UPDATE, (uint32_t)(n_kvs / 2), line);
}

static void compile_and_or(compiler_t* c, bool is_and, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, is_and ? "and" : "or", line);
    compile_sexpr(c, args[0], false, ctx);
    emit(c, ctx, OP_DUP, line);

    int64_t j1 = jump_emit(ctx, fnb_add_jump_if_false(&c->builder, line, &ctx->alloc), line);

    if (is_and) {
        emit(c, ctx, OP_POP, line);
        compile_sexpr(c, args[1], false, ctx);
        patch_jump(c, ctx, j1, line);
        return;
    }

    int64_t j2 = jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), line);
    patch_jump(c, ctx, j1, line);
    emit(c, ctx, OP_POP, line);
    compile_sexpr(c, args[1], false, ctx);
    patch_jump(c, ctx, j2, line);
}

static void compile_not(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 1)
        compiler_malformed(ctx, "not", line);
    compile_sexpr(c, args[0], false, ctx);
    emit(c, ctx, OP_NOT, line);
}

static void compile_do(compiler_t* c, const sexpr_t* args, int64_t n, bool is_tail, ctx_t* ctx)
{
    init_scope(c);
    for (int64_t i = 0; i < n; i++) {
        bool is_last = i == n - 1;
        compile_sexpr(c, args[i], is_last && is_tail, ctx);
        if (!is_last)
            emit(c, ctx, OP_POP, 0);
    }
    deinit_scope(c, ctx);
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

/**
 * Compiles a function body in a child compiler, linked as c->child until adopt_fn takes the function.
 */
static void compile_fn_body(compiler_t* c, const sexpr_t* cls, const sexpr_t* params, sexpr_t body,
                                   sv_str_t name, int64_t upvalue_offset, transient_hashmap_t members,
                                   int64_t line, ctx_t* ctx)
{
    compiler_t* fc = compiler_init(c->current_path, c->arena, c->scratch, ctx);
    fc->members = members;
    fc->globals = c->globals;
    fc->record_fields = c->record_fields;
    fc->atoms = c->atoms;
    fc->module_scope = c->module_scope;
    c->child = fc;

    if (cls != NULL) {
        fc->upvalues.offset = upvalue_offset;
        check_limit(ctx, cls->cons.size, UINT8_MAX, "closure upvalues", line);
        for (int64_t i = 0; i < cls->cons.size; i++)
            locals_add(&fc->upvalues, expect_id(cls->cons.arr[i], ctx), ctx, line);
    }

    check_limit(ctx, params->cons.size, UINT8_MAX, "parameters", line);
    init_scope(fc);
    for (int64_t i = 0; i < params->cons.size; i++) {
        char slot[24];
        sv_str_t p = params->cons.arr[i].tag == S_CONS
            ? param_slot(slot, sizeof(slot), (int)i)
            : expect_id(params->cons.arr[i], ctx);
        locals_add(fc->locals, p, ctx, line);
    }
    locals_add(fc->locals, name, ctx, line);

    for (int64_t i = 0; i < params->cons.size; i++) {
        if (params->cons.arr[i].tag != S_CONS)
            continue;

        char slot[24];
        compile_id(fc, param_slot(slot, sizeof(slot), (int)i), line, ctx);
        compile_destructure(fc, params->cons.arr[i], line, ctx);
        emit(fc, ctx, OP_POP, line);
    }

    compile_sexpr(fc, body, true, ctx);
    emit(fc, ctx, OP_RETURN, 0);

    fn_t* fn = &fc->builder.fn;
    fn->name = sv_str_copy(name, &ctx->alloc);
    if (fn->name.chars == NULL)
        compiler_oom(ctx, line);
    fn->arity = (uint8_t)params->cons.size;
    fn->line = line;
    modules_deinit(fc, ctx);
    scopes_deinit(fc, ctx);
}

/**
 * Moves the child's function into the parent's chunk and unlinks the child. Returns its index.
 */
static uint32_t adopt_fn(compiler_t* c, int64_t line, ctx_t* ctx)
{
    sv_opt_t(uint32_t) i = fnb_add_function(&c->builder, c->child->builder.fn, &ctx->alloc);
    if (!i.is_some)
        compiler_oom(ctx, line);
    c->child->builder.fn = (fn_t){0};
    c->child = NULL;
    return i.value;
}

static void compile_upvalue_loads(compiler_t* c, const sexpr_t* cls, int64_t line, ctx_t* ctx)
{
    if (cls == NULL)
        return;
    for (int64_t i = 0; i < cls->cons.size; i++)
        compile_id(c, expect_id(cls->cons.arr[i], ctx), line, ctx);
}

static void compile_fun_group(compiler_t* c, const sexpr_t* members, int64_t n, int64_t line, ctx_t* ctx);

static void compile_fun(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n > 0 && args[0].tag == S_CONS) {
        compile_fun_group(c, args, n, line, ctx);
        return;
    }
    if (n != 3 && n != 4)
        compiler_malformed(ctx, "fun", line);

    bool has_cls = n == 4;
    const sexpr_t* cls = has_cls ? &args[1] : NULL;
    const sexpr_t* params = has_cls ? &args[2] : &args[1];
    sexpr_t body = has_cls ? args[3] : args[2];
    if ((has_cls && cls->tag != S_CONS) || params->tag != S_CONS)
        compiler_malformed(ctx, "fun", line);

    sv_str_t name = expect_id(args[0], ctx);
    compile_fn_body(c, cls, params, body, name, 0, (transient_hashmap_t){0}, line, ctx);
    compile_upvalue_loads(c, cls, line, ctx);

    uint32_t fi = adopt_fn(c, line, ctx);
    emit_wide(c, ctx, OP_LOAD_CLOSURE, fi, 0);
    emit(c, ctx, has_cls ? (uint8_t)cls->cons.size : 0, 0);
    add_var(c, name, line, ctx);
}

static void compile_fun_group(compiler_t* c, const sexpr_t* members, int64_t n, int64_t line, ctx_t* ctx)
{
    int64_t first = c->builder.fn.chunk.functions.size;
    check_limit(ctx, first, UINT32_MAX, "functions", line);
    check_limit(ctx, n, UINT8_MAX, "closure group members", line);

    transient_hashmap_t member_names = {0};
    for (int64_t i = 0; i < n; i++) {
        if (members[i].tag != S_CONS || (members[i].cons.size != 3 && members[i].cons.size != 4))
            compiler_malformed(ctx, "fun group", line);
        sv_str_t name = expect_id(members[i].cons.arr[0], ctx);
        bool existed = false;
        names_add(&member_names, name, &existed, ctx, c->scratch);
        if (existed)
            compiler_error_name(ctx, C_ERR_REDEFINED, line, "Group member", name);
    }

    int64_t upvalue_base = 0;
    for (int64_t i = 0; i < n; i++) {
        const sexpr_t* item = members[i].cons.arr;
        bool has_cls = members[i].cons.size == 4;
        const sexpr_t* cls = has_cls ? &item[1] : NULL;
        const sexpr_t* params = has_cls ? &item[2] : &item[1];
        sexpr_t body = has_cls ? item[3] : item[2];
        if ((has_cls && cls->tag != S_CONS) || params->tag != S_CONS)
            compiler_malformed(ctx, "fun group", line);

        sv_str_t name = expect_id(item[0], ctx);
        compile_fn_body(c, cls, params, body, name, upvalue_base, member_names, line, ctx);
        adopt_fn(c, line, ctx);

        upvalue_base += has_cls ? cls->cons.size : 0;
    }

    for (int64_t i = 0; i < n; i++) {
        bool has_cls = members[i].cons.size == 4;
        compile_upvalue_loads(c, has_cls ? &members[i].cons.arr[1] : NULL, line, ctx);
    }

    check_limit(ctx, upvalue_base, UINT8_MAX, "closure group upvalues", line);
    emit_wide(c, ctx, OP_CREATE_GROUP, (uint32_t)first, line);
    emit2(c, ctx, (uint8_t)n, (uint8_t)upvalue_base, line);

    for (int64_t i = 0; i < n; i++) {
        add_var(c, expect_id(members[i].cons.arr[0], ctx), line, ctx);
        if (i < n - 1)
            emit(c, ctx, OP_POP, line);
    }
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
static void compile_fatbar(compiler_t* c, const sexpr_t* args, int64_t n,
                           int64_t line, bool is_tail, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "alternative", line);

    fail_target_t target = {
        .jumps = sv_vec_init(int64_t),
        .locals = live_locals(c),
        .next = c->fail_targets,
    };
    c->fail_targets = &target;

    init_scope(c);
    compile_sexpr(c, args[0], is_tail, ctx);
    deinit_scope(c, ctx);
    int64_t over = jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), line);

    c->fail_targets = target.next;
    for (int64_t i = 0; i < target.jumps.size; i++)
        patch_jump(c, ctx, target.jumps.arr[i], line);

    init_scope(c);
    compile_sexpr(c, args[1], is_tail, ctx);
    deinit_scope(c, ctx);
    patch_jump(c, ctx, over, line);
}

/**
 * `$fail`: unwind the locals bound since the enclosing alternative, then jump to
 * its second arm.
 */
static void compile_fail(compiler_t* c, int64_t line, ctx_t* ctx)
{
    fail_target_t* target = c->fail_targets;
    if (target == NULL)
        compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, "No alternative to fail to");

    emit_pop_locals(c, ctx, live_locals(c) - target->locals, line);

    int64_t j = jump_emit(ctx, fnb_add_jump(&c->builder, line, &ctx->alloc), line);
    sv_vec_push(&target->jumps, j, NULL, c->scratch);
}

static bool is_form(sv_str_t name, const char* form)
{
    return sv_str_comp(name, sv_str_init(form));
}

/**
 * `(is-tuple? c k)` and `(is-record? c k)` put the size in the operand, and
 * `(is-record? c)` matches a record of any size.
 */
static void compile_sized_test(compiler_t* c, sv_str_t name, const sexpr_t* args, int64_t n,
                               int64_t line, ctx_t* ctx)
{
    bool tuple = is_form(name, "is-tuple?");
    if (n == 1 && !tuple) {
        compile_sexpr(c, args[0], false, ctx);
        emit(c, ctx, OP_IS_RECORD_ANY, line);
        return;
    }
    if (n != 2 || args[1].tag != S_ATOM || args[1].atom.literal.kind != LITERAL_NUMBER)
        compiler_malformed(ctx, tuple ? "is-tuple?" : "is-record?", line);

    compile_sexpr(c, args[0], false, ctx);
    emit_narrow(c, ctx, tuple ? OP_IS_TUPLE : OP_IS_RECORD,
                (int64_t)args[1].atom.literal.number,
                tuple ? "tuple elements" : "record fields", line);
}

/**
 * `(has-field? c name)` interns the field the way a field access does, so the id
 * lands in the operand.
 */
static void compile_has_field(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 2)
        compiler_malformed(ctx, "has-field?", line);

    sv_str_t name = expect_id(args[1], ctx);
    uint32_t id = record_field_id(c, name, line, ctx);

    compile_sexpr(c, args[0], false, ctx);
    emit2(c, ctx, OP_HAS_FIELD, (uint8_t)id, line);
}

/**
 * `(list-uncons subject head tail body)` binds both parts from one opcode. The
 * binds are raw so neither value is left on the stack.
 */
static void compile_uncons(compiler_t* c, const sexpr_t* args, int64_t n, int64_t line, ctx_t* ctx)
{
    if (n != 3)
        compiler_malformed(ctx, "list-uncons", line);

    sv_str_t head = expect_id(args[1], ctx);
    sv_str_t tail = expect_id(args[2], ctx);

    compile_sexpr(c, args[0], false, ctx);
    emit(c, ctx, OP_LIST_UNCONS, line);

    if (c->locals) {
        emit(c, ctx, OP_SET_LOCAL, line);
        locals_add(c->locals, head, ctx, line);
        emit(c, ctx, OP_SET_LOCAL, line);
        locals_add(c->locals, tail, ctx, line);
    } else {
        emit(c, ctx, OP_SET_GLOBAL, line);
        globals_add(&c->globals, head, sv_str_init(c->current_path), line, ctx, c->scratch);
        emit(c, ctx, OP_SET_GLOBAL, line);
        globals_add(&c->globals, tail, sv_str_init(c->current_path), line, ctx, c->scratch);
    }
}

/**
 * Compiles a form the match lowering emits; false when the name is not one. These
 * names hold characters the scanner rejects, so user code can never reach them.
 */
static bool compile_internal(compiler_t* c, sv_str_t name, const sexpr_t* args, int64_t n,
                             int64_t line, ctx_t* ctx)
{
    for (int64_t i = 0; i < (int64_t)(sizeof(TYPE_TESTS) / sizeof(TYPE_TESTS[0])); i++) {
        if (!is_form(name, TYPE_TESTS[i].name))
            continue;
        if (n != 1)
            compiler_malformed(ctx, TYPE_TESTS[i].name, line);
        compile_sexpr(c, args[0], false, ctx);
        emit(c, ctx, TYPE_TESTS[i].op, line);
        return true;
    }

    if (is_form(name, "is-tuple?") || is_form(name, "is-record?")) {
        compile_sized_test(c, name, args, n, line, ctx);
    } else if (is_form(name, "has-field?")) {
        compile_has_field(c, args, n, line, ctx);
    } else if (is_form(name, "match-fail")) {
        if (n != 1)
            compiler_malformed(ctx, "match-fail", line);
        compile_sexpr(c, args[0], false, ctx);
        emit(c, ctx, OP_NO_MATCH, line);
    } else if (is_form(name, "is-hashmap?") && n == 1) {
        compile_sexpr(c, args[0], false, ctx);
        emit(c, ctx, OP_IS_HASHMAP_ANY, line);
    } else if (is_form(name, "is-hashmap?") || is_form(name, "has-key?")) {
        if (n != 2)
            compiler_malformed(ctx, "keyed test", line);
        bool has_key = is_form(name, "has-key?");
        compile_sexpr(c, args[has_key ? 0 : 1], false, ctx);
        compile_sexpr(c, args[has_key ? 1 : 0], false, ctx);
        emit(c, ctx, has_key ? OP_HAS_KEY : OP_IS_HASHMAP, line);
    } else if (is_form(name, "list-uncons")) {
        compile_uncons(c, args, n, line, ctx);
    } else {
        return false;
    }
    return true;
}

static void compile_call(compiler_t* c, sv_str_t fn_name, const sexpr_t* args, int64_t n,
                         int64_t line, bool is_tail, ctx_t* ctx)
{
    for (int64_t i = 0; i < n; i++)
        compile_sexpr(c, args[i], false, ctx);
    compile_id(c, fn_name, line, ctx);
    uint8_t op = is_tail ? OP_TAIL_CALL : OP_CALL;
    emit_narrow(c, ctx, op, n, "arguments", line);
}

static void compile_literal(compiler_t* c, literal_t lit, int64_t line, ctx_t* ctx)
{
    switch (lit.kind) {
        case LITERAL_NUMBER:
            add_const(c, ctx, (value_t){ .kind = VALUE_NUMBER, .number = lit.number }, line);
            break;
        case LITERAL_ATOM: {
            bool existed = false;
            uint32_t id = names_add(c->atoms, lit.str, &existed, ctx, &ctx->alloc);
            add_const(c, ctx, (value_t){ .kind = VALUE_ATOM, .number = (double)id }, line);
            break;
        }
        case LITERAL_TRUE:
            add_const(c, ctx, (value_t){ .kind = VALUE_BOOL, .boolean = true }, line);
            break;
        case LITERAL_FALSE:
            add_const(c, ctx, (value_t){ .kind = VALUE_BOOL, .boolean = false }, line);
            break;
        case LITERAL_NIL:
            add_const(c, ctx, (value_t){ .kind = VALUE_NIL }, line);
            break;
        case LITERAL_STRING: {
            value_t s = value_init_str(lit.str, &ctx->alloc);
            if (s.obj.cell == NULL)
                compiler_oom(ctx, line);
            add_const(c, ctx, s, line);
            break;
        }
        case LITERAL_IDENTIFIER:
            compile_id(c, lit.literal, line, ctx);
            break;
    }
}

static void compile_atom(compiler_t* c, token_t token, ctx_t* ctx)
{
    if (token.kind != TOKEN_LITERAL) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Unexpected token at line %" PRId64, token.line);
        compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, msg);
    }
    compile_literal(c, token.literal, token.line, ctx);
}

/* Puts a copy of the key and the value in a module table, which owns both. */
static void module_put(transient_hashmap_t* t, sv_str_t key, value_t value, int64_t line, ctx_t* ctx)
{
    value_t k = value_init_str(key, &ctx->alloc);
    bool ok = k.obj.cell != NULL && thm_put(t, (kv_t){ .key = k, .value = value }, &ctx->alloc);
    value_free(&k, &ctx->alloc);
    value_free(&value, &ctx->alloc);
    if (!ok)
        compiler_oom(ctx, line);
}

/* Compiles the module unless it already is, and binds its alias. Returns whether it was compiled now. */
static bool compile_import(compiler_t* c, const sexpr_t* args, int64_t line, ctx_t* ctx)
{
    sv_str_t name = expect_id(args[0], ctx);

    // get import file name
    char current_dir[FILENAME_MAX];
    size_t length;
    cwk_path_get_dirname(c->current_path, &length);
    current_dir[length] = '\0';
    memcpy(current_dir, c->current_path, length);

    char* import_path = sv_str_to_c_str(args[1].atom.literal.str, c->scratch);
    char import_full_path[FILENAME_MAX];
    cwk_path_join(current_dir, import_path, import_full_path, sizeof(import_full_path));
    sv_str_t full_path = sv_str_init(import_full_path);
    value_t path_key = value_init_str(full_path, c->scratch);

    bool is_new = !thm_get(c->modules.compiled_modules, path_key).is_some;
    if (is_new) {
        if (thm_get(c->modules.to_be_compiled_modules, path_key).is_some)
            compiler_error(ctx, C_ERR_IMPORT_CICLE, "Import cicle detected");
        module_put(&c->modules.to_be_compiled_modules, full_path, value_nil, line, ctx);

        char* source_code = read_file(import_full_path, c->scratch);
        if (source_code == NULL)
            compiler_oom(ctx, line);

        const char* current_path = c->current_path;
        c->current_path = import_full_path;
        if (compile_source(c, source_code, ctx))
            emit(c, ctx, OP_POP, 0);
        c->current_path = current_path;

        module_put(&c->modules.compiled_modules, full_path, value_nil, line, ctx);
        thm_delete(&c->modules.to_be_compiled_modules, path_key, &ctx->alloc);
    }

    value_t path_value = value_init_str(full_path, &ctx->alloc);
    if (path_value.obj.cell == NULL)
        compiler_oom(ctx, line);
    module_put(&c->module_scope->var_to_modules, name, path_value, line, ctx);
    return is_new;
}

static void compile_cons(compiler_t* c, const sexpr_t* cons, int64_t n, bool is_tail, ctx_t* ctx)
{
    if (n == 0)
        return;

    sexpr_t head = cons[0];
    if (head.tag == S_CONS) {
        for (int64_t i = 1; i < n; i++)
            compile_sexpr(c, cons[i], false, ctx);
        compile_cons(c, head.cons.arr, head.cons.size, false, ctx);
        uint8_t op = is_tail ? OP_TAIL_CALL : OP_CALL;
        emit_narrow(c, ctx, op, n - 1, "arguments", sexpr_line(head));
        return;
    }

    token_t a = head.atom;
    if (a.kind == TOKEN_OPERATOR) {
        compile_operator(c, a.operator, cons + 1, n - 1, a.line, is_tail, ctx);
        return;
    }
    if (a.kind == TOKEN_SP_FUNCTION) {
        switch (a.fn) {
            case FN_IF: compile_if(c, cons + 1, n - 1, a.line, is_tail, ctx); return;
            case FN_FOR: compile_for(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_MAP: compile_map(c, cons + 1, n - 1, OP_LIST_STACK, a.line, ctx); return;
            case FN_MAPF: compile_map(c, cons + 1, n - 1, OP_LIST_FILTER, a.line, ctx); return;
            case FN_LIST: compile_list(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_HASHMAP: compile_hashmap(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_RECORD: compile_record(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_TUPLE: compile_tuple(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_FUN: compile_fun(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_LENGTH: compile_length(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_RECORD_GET_OR_NIL: compile_record_get_or_nil(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_HASHMAP_GET_OR_NIL: compile_hashmap_get_or_nil(c, cons + 1, n - 1, a.line, ctx); return;
            case FN_IMPORT:
                compile_import(c, cons + 1, a.line, ctx);
                add_const(c, ctx, value_nil, a.line);
                return;
            case FN_MATCH: compiler_error(ctx, C_ERR_UNEXPECTED_SEXPR, "Unlowered match expression");
            case FN_REDUCE:
            case FN_WHILE: {
                char msg[96];
                snprintf(msg, sizeof(msg), "Compiler '%s' not implemented at line %" PRId64, special_fn_text(a.fn), a.line);
                compiler_error(ctx, C_ERR_NOT_IMPLEMENTED, msg);
            }
        }
    }
    if (a.kind == TOKEN_KEYWORD && a.keyword == KEYWORD_DO) {
        compile_do(c, cons + 1, n - 1, is_tail, ctx);
        return;
    }
    if (a.kind == TOKEN_KEYWORD && (a.keyword == KEYWORD_AND || a.keyword == KEYWORD_OR)) {
        compile_and_or(c, a.keyword == KEYWORD_AND, cons + 1, n - 1, a.line, ctx);
        return;
    }
    if (a.kind == TOKEN_KEYWORD && a.keyword == KEYWORD_NOT) {
        compile_not(c, cons + 1, n - 1, a.line, ctx);
        return;
    }
    if (a.kind == TOKEN_PIPE) {
        compile_fatbar(c, cons + 1, n - 1, a.line, is_tail, ctx);
        return;
    }
    if (a.kind == TOKEN_LITERAL && a.literal.kind == LITERAL_IDENTIFIER) {
        if (!compile_internal(c, a.literal.literal, cons + 1, n - 1, a.line, ctx))
            compile_call(c, a.literal.literal, cons + 1, n - 1, a.line, is_tail, ctx);
        return;
    }

    char msg[96];
    snprintf(msg, sizeof(msg), "Value is not callable at line %" PRId64, a.line);
    compiler_error(ctx, C_ERR_NOT_CALLABLE, msg);
}

static void compile_sexpr(compiler_t* c, sexpr_t sexpr, bool is_tail, ctx_t* ctx)
{
    if (sexpr.tag == S_ATOM) {
        compile_atom(c, sexpr.atom, ctx);
        return;
    }

    if (sexpr.cons.size > 1 && sexpr.cons.arr[0].tag == S_ATOM) {
        token_t head = sexpr.cons.arr[0].atom;
        if (head.kind == TOKEN_SP_FUNCTION && head.fn == FN_MATCH) {
            compile_sexpr(c, match_compile(sexpr, ctx, c->scratch), is_tail, ctx);
            return;
        }
    }

    compile_cons(c, sexpr.cons.arr, sexpr.cons.size, is_tail, ctx);
}

static void add_native_fn(compiler_t* c, native_fn_t fn, ctx_t* ctx)
{
    globals_add(&c->globals, sv_str_init(fn.name), sv_str_init(""), 0, ctx, c->scratch);
    value_t fn_val = value_init_native(fn, &ctx->alloc);
    if (fn_val.obj.cell == NULL)
        compiler_oom(ctx, 0);
    int success = 0;
    sv_vec_push(&c->global_values, fn_val, &success, &ctx->alloc);
    if (!success) {
        value_free(&fn_val, &ctx->alloc);
        compiler_oom(ctx, 0);
    }
}

vm_t compile(const char* base_path, compile_opts_t opts, ctx_t* ctx)
{
    return compile_files(&base_path, 1, opts, ctx);
}

/**
 * Runs the compilation under one error jump.
 */
static vm_t compile_guarded(const char** files, int64_t count, compile_opts_t opts, ctx_t* ctx,
                            sv_arena_t* arena, const sv_allocator_t* scratch)
{
    jmp_buf on_error;
    compiler_t* volatile compiler = NULL;  // read after a jump

    arena->error_buffer = &on_error;
    ctx->on_error = &on_error;
    switch (setjmp(on_error)) {
        case 1:
            error_set_oom(&ctx->err, (int)C_ERR_OOM, 0, &ctx->alloc);
            /* fallthrough */
        case 2:
            if (compiler != NULL)
                compiler_abort(compiler, ctx);
            return (vm_t){0};
    }

    transient_hashmap_t* record_fields = sv_arena_calloc(arena, sizeof(transient_hashmap_t));
    transient_hashmap_t* atoms = sv_arena_calloc(arena, sizeof(transient_hashmap_t));
    compiler_t* c = compiler_init("", arena, scratch, ctx);
    c->record_fields = record_fields;
    c->atoms = atoms;
    compiler = c;

    /* Reserved for the ids map iteration builds its records with. */
    record_field_id(c, sv_str_init("key"), 0, ctx);
    record_field_id(c, sv_str_init("value"), 0, ctx);

    native_fn_t native_fns[] = {
        { .arity = 1, .name = "print", .fn = ntv_print },
        { .arity = 1, .name = "println", .fn = ntv_println },
        { .arity = 1, .name = "print_vals", .fn = ntv_print_arr },
        { .arity = 2, .name = "randi", .fn = ntv_randi },
        { .arity = 2, .name = "randf", .fn = ntv_randf },
    };
    for (size_t i = 0; i < sizeof(native_fns) / sizeof(native_fns[0]); i++)
        add_native_fn(c, native_fns[i], ctx);
    for (int64_t i = 0; i < opts.native_count; i++)
        add_native_fn(c, opts.native_funs[i], ctx);

    bool has_value = false;
    for (int64_t i = 0; i < count; i++) {
        char* source_code = read_file(files[i], scratch);
        if (source_code == NULL)
            compiler_oom(ctx, 0);
        if (has_value)
            emit(c, ctx, OP_POP, 0);
        c->current_path = files[i];
        has_value = compile_source(c, source_code, ctx);
    }
    emit(c, ctx, OP_RETURN, 0);

    // the vm owns the result from here on, so these failures must not jump
    vm_t vm = vm_init(fnb_build(&c->builder), opts.max_frames, c->globals, &ctx->alloc);
    vm.globals = c->global_values;
    vm.ctx.alloc = &ctx->alloc;
    modules_deinit(c, ctx);

    int64_t n_fields = names_count(*record_fields);
    int64_t n_atoms = names_count(*atoms);
    if (n_fields > 0) {
        vm.ctx.record_key_names = names_arr_init(*record_fields, &ctx->alloc);
        vm.ctx.record_names_sizes = (uint32_t)n_fields;
        vm.ctx.record_fields = transient_to_map(record_fields, &ctx->alloc);
    }
    if (n_atoms > 0) {
        vm.ctx.atom_names = names_arr_init(*atoms, &ctx->alloc);
        vm.ctx.atom_names_size = (uint32_t)n_atoms;
    }
    thm_deinit(record_fields, &ctx->alloc);
    thm_deinit(atoms, &ctx->alloc);

    if ((n_fields > 0 && (vm.ctx.record_key_names == NULL || vm.ctx.record_fields.cell == NULL))
        || (n_atoms > 0 && vm.ctx.atom_names == NULL)
    ) {
        error_set_oom(&ctx->err, (int)C_ERR_OOM, 0, &ctx->alloc);
        vm_deinit(&vm);
        return (vm_t){0};
    }

    return vm;
}

vm_t compile_files(const char** files, int64_t count, compile_opts_t opts, ctx_t* ctx)
{
    sv_arena_t arena = sv_arena_init(1 << 16, &ctx->alloc, NULL);
    sv_allocator_t scratch = sv_arena_allocator_init(&arena);
    vm_t vm = compile_guarded(files, count, opts, ctx, &arena, &scratch);
    ctx->on_error = NULL;
    sv_arena_deinit(&arena);
    return vm;
}

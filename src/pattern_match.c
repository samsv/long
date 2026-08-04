#include "pattern_match.h"
#include "compiler.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

#define CLAUSES_START 2
#define FAIL_NAME "$fail"
#define WILDCARD_NAME "_"
#define TEMP_DIGITS 12
#define TEMP_SIZE (TEMP_DIGITS + 2)
#define TEMPS_MAX 1024

typedef sv_vec_t(sexpr_t) cons_t;

typedef enum {
    PAT_UNKNOWN,
    PAT_STR,
    PAT_NUMBER,
    PAT_NIL,
    PAT_BOOL,
    PAT_LIST,
    PAT_HASHMAP,
    PAT_RECORD,
    PAT_TUPLE,
    PAT_VAR,
} pattern_class;

/**
 * A rows by columns pattern matrix. Columns hold the subject each cell is
 * tested against, always an already bound temp identifier. Cells and bodies are
 * pointers into the input tree: moving one out means reading it and writing
 * `nil` back through the pointer, so the input spine never double frees.
 */
/**
 * A pattern to test. Normally the slot holding it in the input tree. A list
 * decomposition also produces the unconsumed remainder of a list pattern, which
 * has no slot of its own, so it is the list pattern plus a count of elements
 * already consumed.
 */
typedef struct {
    sexpr_t* slot;
    int64_t taken;
} cell_t;

/**
 * A column subject and what is already known about the values reaching it, so a
 * test the decomposition has already made is not repeated.
 */
typedef struct {
    sexpr_t subject;
    pattern_class known;
} col_t;

typedef struct {
    sexpr_t* slot;
    bool guarded;
} row_t;

typedef struct {
    col_t* cols;
    cell_t* cells;
    row_t* bodies;
    int64_t n_cols;
    int64_t n_rows;
} matrix_t;

#define CELL(m, i, j) ((m)->cells[(i) * (m)->n_cols + (j)])

static sexpr_t id_atom_str(sv_str_t name, int64_t line)
{
    return atom_sexpr((token_t){
        .kind = TOKEN_LITERAL,
        .line = line,
        .literal = { .kind = LITERAL_IDENTIFIER, .literal = name },
    });
}

static sexpr_t id_atom(const char* name, int64_t line)
{
    return id_atom_str(sv_str_init(name), line);
}

static sexpr_t nil_atom(int64_t line)
{
    return atom_sexpr((token_t){
        .kind = TOKEN_LITERAL,
        .line = line,
        .literal = { .kind = LITERAL_NIL },
    });
}

static sexpr_t num_atom(double n, int64_t line)
{
    return atom_sexpr((token_t){
        .kind = TOKEN_LITERAL,
        .line = line,
        .literal = { .kind = LITERAL_NUMBER, .number = n },
    });
}

static sexpr_t op_atom(operator_kind op, int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_OPERATOR, .line = line, .operator = op });
}

static sexpr_t fn_atom(special_fn_kind fn, int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_SP_FUNCTION, .line = line, .fn = fn });
}

static sexpr_t do_atom(int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_KEYWORD, .line = line, .keyword = KEYWORD_DO });
}

static sexpr_t pipe_atom(int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_PIPE, .line = line });
}

static sexpr_t and_atom(int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_KEYWORD, .line = line, .keyword = KEYWORD_AND });
}

static sexpr_t when_atom(int64_t line)
{
    return atom_sexpr((token_t){ .kind = TOKEN_KEYWORD, .line = line, .keyword = KEYWORD_WHEN });
}

static sexpr_t fail_atom(int64_t line)
{
    return id_atom(FAIL_NAME, line);
}

static bool cons_build(sexpr_t* out, const sexpr_t* items, int64_t n, const sv_allocator_t* a)
{
    sv_vec_t(sexpr_t) list = sv_vec_init_capacity(sexpr_t, n, a);
    if (list.arr == NULL)
        return false;

    for (int64_t i = 0; i < n; i++)
        list.arr[list.size++] = items[i];

    *out = cons_sexpr(list);
    return true;
}

static bool match_oom(ctx_t* ctx, int64_t line)
{
    return error_set_oom(&ctx->err, C_ERR_OOM, line, &ctx->alloc);
}

static bool match_redefined(ctx_t* ctx, sv_str_t name, int64_t line)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "Record field '%.*s' redefined at line %" PRId64,
             (int)name.size, name.chars, line);
    return error_set(&ctx->err, C_ERR_REDEFINED, msg, &ctx->alloc);
}

static sexpr_t discard(sexpr_t* s, ctx_t* ctx)
{
    int64_t line = s->tag == S_CONS ? s->cons.arr[0].atom.line : s->atom.line;
    sexpr_free(s, &ctx->alloc);
    return atom_sexpr((token_t){ .kind = TOKEN_ERROR, .line = line });
}

static char temp_names[TEMPS_MAX][TEMP_SIZE];
static int temps_used;

/**
 * Returns an atom naming a fresh temp. A name is a pure function of its index,
 * so the static buffers can be rewritten by a later lowering without
 * invalidating a tree that already views them.
 */
static bool next_temp(sexpr_t* out, int64_t line, ctx_t* ctx)
{
    if (temps_used >= TEMPS_MAX) {
        char msg[96];
        snprintf(msg, sizeof(msg), "More than %d match variables at line %" PRId64,
                 TEMPS_MAX, line);
        return error_set(&ctx->err, C_ERR_NOT_IMPLEMENTED, msg, &ctx->alloc);
    }

    char* buf = temp_names[temps_used];
    snprintf(buf, TEMP_SIZE, "$%0*d", TEMP_DIGITS, ++temps_used);
    *out = id_atom(buf, line);
    return true;
}

static pattern_class pattern_class_of(sexpr_t pattern)
{
    if (pattern.tag == S_CONS) {
        token_t head = pattern.cons.arr[0].atom;
        if (head.kind != TOKEN_SP_FUNCTION)
            return PAT_VAR;

        special_fn_kind fn = head.fn;
        if (fn == FN_LIST)
            return PAT_LIST;
        if (fn == FN_HASHMAP)
            return PAT_HASHMAP;
        if (fn == FN_RECORD)
            return PAT_RECORD;
        return PAT_TUPLE;
    }

    switch (pattern.atom.literal.kind) {
        case LITERAL_STRING: return PAT_STR;
        case LITERAL_NUMBER: return PAT_NUMBER;
        case LITERAL_NIL: return PAT_NIL;
        case LITERAL_TRUE:
        case LITERAL_FALSE: return PAT_BOOL;
        case LITERAL_IDENTIFIER: return PAT_VAR;
    }
    return PAT_VAR;
}

/**
 * True when the list pattern ends in a `(.. t)` tail element.
 */
bool list_has_tail(sexpr_t list)
{
    if (list.cons.size < 2)
        return false;

    sexpr_t last = list.cons.arr[list.cons.size - 1];
    return last.tag == S_CONS && last.cons.arr[0].atom.kind == TOKEN_DOT_DOT;
}

int64_t list_n_fixed(sexpr_t list)
{
    return list.cons.size - 1 - (list_has_tail(list) ? 1 : 0);
}

static cell_t cell_of(sexpr_t* slot)
{
    return (cell_t){ .slot = slot, .taken = 0 };
}

/**
 * The pattern a cell denotes. Only valid for a cell that is not a remainder.
 */
static sexpr_t cell_pattern(cell_t c)
{
    return *c.slot;
}

static pattern_class cell_class(cell_t c)
{
    return c.taken == 0 ? pattern_class_of(*c.slot) : PAT_LIST;
}

/**
 * True when nothing is left of the list pattern, i.e. it matches only the empty
 * list. A remainder that still has a tail variable is never in this state: it is
 * normalised into the variable's own slot when it is built.
 */
static bool cell_is_nil(cell_t c)
{
    sexpr_t list = *c.slot;
    return c.taken == list_n_fixed(list) && !list_has_tail(list);
}

static cell_t cell_head(cell_t c)
{
    return cell_of(&c.slot->cons.arr[1 + c.taken]);
}

static cell_t cell_rest(cell_t c)
{
    sexpr_t list = *c.slot;
    int64_t next = c.taken + 1;
    if (next == list_n_fixed(list) && list_has_tail(list))
        return cell_of(&c.slot->cons.arr[list.cons.size - 1].cons.arr[1]);

    return (cell_t){ .slot = c.slot, .taken = next };
}

static const char* class_predicate(pattern_class class)
{
    switch (class) {
        case PAT_STR: return "is-str?";
        case PAT_NUMBER: return "is-number?";
        case PAT_NIL: return "is-nil?";
        case PAT_BOOL: return "is-bool?";
        case PAT_LIST: return "is-list?";
        case PAT_HASHMAP: return "is-hashmap?";
        case PAT_RECORD: return "is-record?";
        case PAT_TUPLE: return "is-tuple?";
        case PAT_VAR:
        case PAT_UNKNOWN: return "";
    }
    return "";
}

static bool is_var_cell(cell_t c)
{
    return cell_class(c) == PAT_VAR;
}

static bool is_named(sexpr_t e, const char* name)
{
    return e.tag == S_ATOM && e.atom.kind == TOKEN_LITERAL
        && e.atom.literal.kind == LITERAL_IDENTIFIER
        && sv_str_comp(e.atom.literal.literal, sv_str_init(name));
}

static bool is_wildcard(sexpr_t p)
{
    return is_named(p, WILDCARD_NAME);
}

static bool is_fail(sexpr_t e)
{
    return is_named(e, FAIL_NAME);
}

static bool same_literal(sexpr_t a, sexpr_t b)
{
    if (a.atom.literal.kind != b.atom.literal.kind)
        return false;

    switch (a.atom.literal.kind) {
        case LITERAL_NUMBER: return a.atom.literal.number == b.atom.literal.number;
        case LITERAL_STRING: return sv_str_comp(a.atom.literal.str, b.atom.literal.str);
        case LITERAL_NIL:
        case LITERAL_TRUE:
        case LITERAL_FALSE: return true;
        case LITERAL_IDENTIFIER: return false;
    }
    return false;
}

/**
 * True when the record pattern ends in the `..` marker, i.e. it allows fields
 * beyond the ones it mentions.
 */
bool record_is_open(sexpr_t rec)
{
    if (rec.cons.size < 2)
        return false;

    sexpr_t last = rec.cons.arr[rec.cons.size - 1];
    return last.tag == S_ATOM && last.atom.kind == TOKEN_DOT_DOT;
}

bool hashmap_is_open(sexpr_t map)
{
    if (map.cons.size < 2)
        return false;

    sexpr_t last = map.cons.arr[map.cons.size - 1];
    return last.tag == S_ATOM && last.atom.kind == TOKEN_DOT_DOT;
}

int64_t hashmap_n_keys(sexpr_t map)
{
    return (map.cons.size - 1 - (hashmap_is_open(map) ? 1 : 0)) / 2;
}

int64_t record_n_fields(sexpr_t rec)
{
    int64_t n = rec.cons.size - 1 - (record_is_open(rec) ? 1 : 0);
    return n / 2;
}

/**
 * True when two record patterns mention the same fields in the same order with
 * the same openness, so their rows can share one set of columns.
 */
static bool same_record_shape(sexpr_t a, sexpr_t b)
{
    if (record_is_open(a) != record_is_open(b) || record_n_fields(a) != record_n_fields(b))
        return false;

    for (int64_t i = 0; i < record_n_fields(a); i++) {
        sv_str_t na = a.cons.arr[1 + 2 * i].atom.literal.literal;
        sv_str_t nb = b.cons.arr[1 + 2 * i].atom.literal.literal;
        if (!sv_str_comp(na, nb))
            return false;
    }
    return true;
}

static bool same_hashmap_shape(sexpr_t a, sexpr_t b)
{
    if (hashmap_is_open(a) != hashmap_is_open(b) || hashmap_n_keys(a) != hashmap_n_keys(b))
        return false;

    for (int64_t i = 0; i < hashmap_n_keys(a); i++)
        if (!same_literal(a.cons.arr[1 + 2 * i], b.cons.arr[1 + 2 * i]))
            return false;

    return true;
}

static sexpr_t* alloc_sexprs(int64_t n, const sv_allocator_t* a)
{
    return n == 0 ? NULL : sv_malloc(a, sizeof(sexpr_t) * (size_t)n);
}

static row_t* alloc_rows(int64_t n, const sv_allocator_t* a)
{
    return n == 0 ? NULL : sv_malloc(a, sizeof(row_t) * (size_t)n);
}

static cell_t* alloc_cells(int64_t n, const sv_allocator_t* a)
{
    return n == 0 ? NULL : sv_malloc(a, sizeof(cell_t) * (size_t)n);
}

static col_t* alloc_cols(int64_t n, const sv_allocator_t* a)
{
    return n == 0 ? NULL : sv_malloc(a, sizeof(col_t) * (size_t)n);
}

static bool alloc_ok(const void* p, int64_t n)
{
    return n == 0 || p != NULL;
}

static bool matrix_alloc(matrix_t* m, int64_t n_cols, int64_t n_rows, ctx_t* ctx)
{
    *m = (matrix_t){ .n_cols = n_cols, .n_rows = n_rows };
    m->cols = alloc_cols(n_cols, &ctx->alloc);
    m->cells = alloc_cells(n_cols * n_rows, &ctx->alloc);
    m->bodies = alloc_rows(n_rows, &ctx->alloc);

    return alloc_ok(m->cols, n_cols)
        && alloc_ok(m->cells, n_cols * n_rows)
        && alloc_ok(m->bodies, n_rows);
}

/**
 * A borrowed view of the rows after the first n, sharing the parent's storage.
 * It owns nothing and must never reach matrix_free.
 */
static matrix_t matrix_tail(const matrix_t* m, int64_t n)
{
    return (matrix_t){
        .cols = m->cols,
        .cells = m->cells + n * m->n_cols,
        .bodies = m->bodies + n,
        .n_cols = m->n_cols,
        .n_rows = m->n_rows - n,
    };
}

static void matrix_free(matrix_t* m, ctx_t* ctx)
{
    sv_free(&ctx->alloc, m->cols);
    sv_free(&ctx->alloc, m->cells);
    sv_free(&ctx->alloc, m->bodies);
    *m = (matrix_t){ 0 };
}

static sexpr_t take_body(const matrix_t* m, int64_t i, int64_t line)
{
    sexpr_t body = *m->bodies[i].slot;
    *m->bodies[i].slot = nil_atom(line);
    return body;
}

/**
 * Scores each column with one point per non variable cell, stopping at the
 * first variable cell, and returns the highest scoring column. Ties keep the
 * leftmost so the order degrades to left to right.
 */
static int64_t best_column(const matrix_t* m)
{
    int64_t best = 0;
    int64_t best_score = -1;
    for (int64_t j = 0; j < m->n_cols; j++) {
        int64_t score = 0;
        for (int64_t i = 0; i < m->n_rows; i++) {
            if (is_var_cell(CELL(m, i, j)))
                break;
            score++;
        }
        if (score > best_score) {
            best_score = score;
            best = j;
        }
    }
    return best;
}

static bool lower_bind(
    sexpr_t* out,
    sexpr_t name,
    sexpr_t value,
    sexpr_t body,
    int64_t line,
    ctx_t* ctx
) {
    sexpr_t bind = { 0 };

    sexpr_t bind_items[] = { op_atom(OPERATOR_EQUAL, line), name, value };
    if (!cons_build(&bind, bind_items, 3, &ctx->alloc))
        goto error;
    value = (sexpr_t){ 0 };

    sexpr_t do_items[] = { do_atom(line), bind, body };
    if (!cons_build(out, do_items, 3, &ctx->alloc))
        goto error;

    return true;

error:
    sexpr_free(&bind, &ctx->alloc);
    sexpr_free(&value, &ctx->alloc);
    sexpr_free(&body, &ctx->alloc);
    return match_oom(ctx, line);
}

/**
 * Wraps body in the bindings of a decomposition, i.e.
 * `(do (= $2 <read 0>) (= $3 <read 1>) body)`.
 */
static bool wrap_binds(
    sexpr_t* out,
    const col_t* cols,
    sexpr_t* reads,
    int64_t n,
    sexpr_t body,
    int64_t line,
    ctx_t* ctx
) {
    for (int64_t i = n - 1; i >= 0; i--) {
        sexpr_t read = reads[i];
        reads[i] = (sexpr_t){ 0 };

        sexpr_t wrapped;
        if (!lower_bind(&wrapped, cols[i].subject, read, body, line, ctx))
            return false;
        body = wrapped;
    }

    *out = body;
    return true;
}

static bool compile_matrix(sexpr_t* out, const matrix_t*, int64_t, ctx_t*);

/**
 * The builders below move nothing on failure, like cons_build: the caller's
 * cleanup label still owns every subtree it passed in.
 */
static bool build_form(sexpr_t* out, const sexpr_t* items, int64_t n, int64_t line, ctx_t* ctx)
{
    if (cons_build(out, items, n, &ctx->alloc))
        return true;
    return match_oom(ctx, line);
}

static bool build_if(
    sexpr_t* out,
    sexpr_t test,
    sexpr_t then,
    sexpr_t otherwise,
    int64_t line,
    ctx_t* ctx
) {
    sexpr_t items[] = { fn_atom(FN_IF, line), test, then, otherwise };
    return build_form(out, items, 4, line, ctx);
}

/**
 * Builds `(| tried otherwise)`, collapsing whenever one side cannot be reached:
 * a `fail` escaping tried propagates outward anyway.
 */
static bool build_alt(sexpr_t* out, sexpr_t tried, sexpr_t otherwise, int64_t line, ctx_t* ctx)
{
    if (is_fail(otherwise)) {
        *out = tried;
        return true;
    }
    if (is_fail(tried)) {
        *out = otherwise;
        return true;
    }

    sexpr_t items[] = { pipe_atom(line), tried, otherwise };
    return build_form(out, items, 3, line, ctx);
}

static bool build_call1(sexpr_t* out, const char* fn, sexpr_t arg, int64_t line, ctx_t* ctx)
{
    sexpr_t items[] = { id_atom(fn, line), arg };
    return build_form(out, items, 2, line, ctx);
}

static bool build_call2(sexpr_t* out, const char* fn, sexpr_t a1, sexpr_t a2, int64_t line, ctx_t* ctx)
{
    sexpr_t items[] = { id_atom(fn, line), a1, a2 };
    return build_form(out, items, 3, line, ctx);
}

static bool build_op2(
    sexpr_t* out,
    operator_kind op,
    sexpr_t lhs,
    sexpr_t rhs,
    int64_t line,
    ctx_t* ctx
) {
    sexpr_t items[] = { op_atom(op, line), lhs, rhs };
    return build_form(out, items, 3, line, ctx);
}

/**
 * Builds the sub matrix for the rows of a group whose column 0 cells were
 * consumed, replacing that column with n_new columns read from the subject.
 */
static bool specialise(
    matrix_t* out,
    const matrix_t* m,
    int64_t col,
    const int64_t* rows,
    int64_t n_rows,
    const col_t* new_cols,
    int64_t n_new,
    const cell_t* new_cells,
    ctx_t* ctx
) {
    if (!matrix_alloc(out, n_new + m->n_cols - 1, n_rows, ctx))
        return false;

    for (int64_t j = 0; j < n_new; j++)
        out->cols[j] = new_cols[j];
    for (int64_t j = 0, k = n_new; j < m->n_cols; j++)
        if (j != col)
            out->cols[k++] = m->cols[j];

    for (int64_t i = 0; i < n_rows; i++) {
        out->bodies[i] = m->bodies[rows[i]];
        for (int64_t j = 0; j < n_new; j++)
            out->cells[i * out->n_cols + j] = new_cells[i * n_new + j];
        for (int64_t j = 0, k = n_new; j < m->n_cols; j++)
            if (j != col)
                out->cells[i * out->n_cols + k++] = CELL(m, rows[i], j);
    }
    return true;
}

/**
 * The per group scratch for a decomposition: one generated name and one read
 * expression per new column, plus the flat cell pointers for the sub matrix.
 * The names view the static temp table and are never freed. The reads are owned
 * here: decomp_free releases every one still held, so a producer that moves a
 * read out must zero its slot.
 */
typedef struct {
    col_t* cols;
    sexpr_t* reads;
    cell_t* cells;
    int64_t n;
} decomp_t;

static bool decomp_alloc(decomp_t* d, int64_t n_new, int64_t n_rows, int64_t line, ctx_t* ctx)
{
    *d = (decomp_t){ 0 };
    d->cols = alloc_cols(n_new, &ctx->alloc);
    d->reads = alloc_sexprs(n_new, &ctx->alloc);
    d->cells = alloc_cells(n_new * n_rows, &ctx->alloc);

    if (!alloc_ok(d->cols, n_new) || !alloc_ok(d->reads, n_new)
        || !alloc_ok(d->cells, n_new * n_rows))
        return match_oom(ctx, line);

    for (int64_t i = 0; i < n_new; i++)
        d->reads[i] = (sexpr_t){ 0 };

    d->n = n_new;
    return true;
}

static void decomp_free(decomp_t* d, ctx_t* ctx)
{
    for (int64_t i = 0; i < d->n; i++)
        sexpr_free(&d->reads[i], &ctx->alloc);

    sv_free(&ctx->alloc, d->cols);
    sv_free(&ctx->alloc, d->reads);
    sv_free(&ctx->alloc, d->cells);
    *d = (decomp_t){ 0 };
}

/**
 * Where a decomposed pattern keeps its sub patterns and its keys. `key_first`
 * of -1 means the read is indexed by position rather than by a key taken from
 * the pattern.
 */
typedef struct {
    operator_kind read_op;
    int64_t cell_first;
    int64_t cell_stride;
    int64_t key_first;
    int64_t key_stride;
} layout_t;

static const layout_t TUPLE_LAYOUT = { OPERATOR_LEFT_BRACKET, 1, 1, -1, 0 };
static const layout_t RECORD_LAYOUT = { OPERATOR_DOT, 2, 2, 1, 2 };
static const layout_t MAP_LAYOUT = { OPERATOR_LEFT_BRACKET, 2, 2, 1, 2 };

/**
 * Allocates a decomposition and fills it: a fresh temp and a read per position,
 * plus the sub pattern pointers the sub matrix will test. Releases the
 * decomposition on failure.
 */
static bool decomp_build(decomp_t* d, const matrix_t* m, int64_t col, const int64_t* rows,
                         int64_t n_rows, int64_t n_new, sexpr_t shape, const layout_t* layout, int64_t line, ctx_t* ctx)
{
    if (!decomp_alloc(d, n_new, n_rows, line, ctx))
        goto error;

    for (int64_t j = 0; j < n_new; j++) {
        sexpr_t key = layout->key_first < 0
            ? num_atom((double)j, line)
            : shape.cons.arr[layout->key_first + layout->key_stride * j];

        if (!next_temp(&d->cols[j].subject, line, ctx)
            || !build_op2(&d->reads[j], layout->read_op, m->cols[col].subject, key, line, ctx))
            goto error;

        d->cols[j].known = PAT_UNKNOWN;
    }

    for (int64_t r = 0; r < n_rows; r++) {
        sexpr_t* pattern = CELL(m, rows[r], col).slot;
        for (int64_t j = 0; j < n_new; j++)
            d->cells[r * n_new + j] =
                cell_of(&pattern->cons.arr[layout->cell_first + layout->cell_stride * j]);
    }

    return true;

error:
    decomp_free(d, ctx);
    return false;
}

/**
 * True when row `last` is the first occurrence of its shape scanning forward, so
 * each distinct shape yields exactly one group.
 */
static bool cell_same_literal(cell_t a, cell_t b)
{
    return same_literal(cell_pattern(a), cell_pattern(b));
}

static bool cell_same_arity(cell_t a, cell_t b)
{
    return cell_pattern(a).cons.size == cell_pattern(b).cons.size;
}

static bool cell_same_record(cell_t a, cell_t b)
{
    return same_record_shape(cell_pattern(a), cell_pattern(b));
}

static bool cell_same_hashmap(cell_t a, cell_t b)
{
    return same_hashmap_shape(cell_pattern(a), cell_pattern(b));
}

static bool cell_same_class(cell_t a, cell_t b)
{
    return cell_class(a) == cell_class(b);
}

static bool first_of_group(const matrix_t* m, int64_t col, const int64_t* rows, int64_t last, bool (*same)(cell_t, cell_t))
{
    cell_t shape = CELL(m, rows[last], col);
    for (int64_t k = 0; k < last; k++)
        if (same(CELL(m, rows[k], col), shape))
            return false;

    return true;
}

/**
 * Gathers every row sharing the shape into picked and returns how many.
 */
static int64_t pick_group(int64_t* picked, const matrix_t* m, int64_t col, const int64_t* rows,
                          int64_t n_rows, cell_t shape, bool (*same)(cell_t, cell_t))
{
    int64_t n = 0;
    for (int64_t k = 0; k < n_rows; k++)
        if (same(CELL(m, rows[k], col), shape))
            picked[n++] = rows[k];

    return n;
}

/**
 * Compiles the rows of one class whose cells all share a shape, having already
 * bound one temp per decomposed position.
 */
static bool compile_decomposed(sexpr_t* out, const matrix_t* m, int64_t col, const int64_t* rows,
                               int64_t n_rows, decomp_t* d, int64_t line, ctx_t* ctx)
{
    matrix_t sub = { 0 };
    sexpr_t inner = { 0 };

    if (!specialise(&sub, m, col, rows, n_rows, d->cols, d->n, d->cells, ctx)) {
        matrix_free(&sub, ctx);
        return match_oom(ctx, line);
    }

    bool ok = compile_matrix(&inner, &sub, line, ctx);
    matrix_free(&sub, ctx);
    if (!ok)
        return false;

    return wrap_binds(out, d->cols, d->reads, d->n, inner, line, ctx);
}

/**
 * Compiles the rows through the remaining columns after column 0 was consumed
 * without introducing new columns.
 */
static bool compile_dropped(sexpr_t* out, const matrix_t* m, int64_t col,
                            const int64_t* rows, int64_t n_rows, int64_t line, ctx_t* ctx)
{
    matrix_t sub = { 0 };
    if (!specialise(&sub, m, col, rows, n_rows, NULL, 0, NULL, ctx)) {
        matrix_free(&sub, ctx);
        return match_oom(ctx, line);
    }

    bool ok = compile_matrix(out, &sub, line, ctx);
    matrix_free(&sub, ctx);
    return ok;
}

/**
 * One equality test per distinct literal in the group, with every row carrying
 * that literal sharing the arm.
 */
static bool compile_literals(sexpr_t* out, const matrix_t* m, int64_t col,
                             const int64_t* rows, int64_t n_rows, int64_t line, ctx_t* ctx)
{
    int64_t* picked = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_rows);
    if (picked == NULL)
        return match_oom(ctx, line);

    sexpr_t chain = fail_atom(line);
    sexpr_t arm = { 0 };
    sexpr_t test = { 0 };
    bool ok = true;

    for (int64_t last = n_rows - 1; last >= 0 && ok; last--) {
        if (!first_of_group(m, col, rows, last, cell_same_literal))
            continue;

        sexpr_t lit = cell_pattern(CELL(m, rows[last], col));
        int64_t n_picked = pick_group(picked, m, col, rows, n_rows, CELL(m, rows[last], col), cell_same_literal);

        ok = compile_dropped(&arm, m, col, picked, n_picked, line, ctx)
            && build_op2(&test, OPERATOR_EQUAL_EQUAL, m->cols[col].subject, lit, line, ctx)
            && build_if(&chain, test, arm, chain, line, ctx);
        if (ok) {
            test = (sexpr_t){ 0 };
            arm = (sexpr_t){ 0 };
        }
    }

    sv_free(&ctx->alloc, picked);
    if (!ok) {
        sexpr_free(&test, &ctx->alloc);
        sexpr_free(&arm, &ctx->alloc);
        sexpr_free(&chain, &ctx->alloc);
        return false;
    }

    *out = chain;
    return true;
}

/**
 * Groups the rows by tuple arity and binds one temp per position.
 */
static bool compile_tuples(sexpr_t* out, const matrix_t* m, int64_t col, const int64_t* rows,
                           int64_t n_rows, sexpr_t next, int64_t line, ctx_t* ctx)
{
    int64_t* picked = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_rows);
    if (picked == NULL) {
        sexpr_free(&next, &ctx->alloc);
        return match_oom(ctx, line);
    }

    sexpr_t chain = next;
    sexpr_t arm = { 0 };
    sexpr_t test = { 0 };
    bool ok = true;

    for (int64_t last = n_rows - 1; last >= 0 && ok; last--) {
        if (!first_of_group(m, col, rows, last, cell_same_arity))
            continue;

        sexpr_t shape = cell_pattern(CELL(m, rows[last], col));
        int64_t arity = shape.cons.size - 1;
        int64_t n_picked = pick_group(picked, m, col, rows, n_rows, CELL(m, rows[last], col), cell_same_arity);

        decomp_t d = { 0 };
        ok = decomp_build(&d, m, col, picked, n_picked, arity, shape, &TUPLE_LAYOUT, line, ctx);
        if (!ok)
            break;

        ok = compile_decomposed(&arm, m, col, picked, n_picked, &d, line, ctx)
            && build_call2(&test, "is-tuple?", m->cols[col].subject, num_atom((double)arity, line), line, ctx)
            && build_if(&chain, test, arm, chain, line, ctx);
        decomp_free(&d, ctx);
        if (ok) {
            test = (sexpr_t){ 0 };
            arm = (sexpr_t){ 0 };
        }
    }

    sv_free(&ctx->alloc, picked);
    if (!ok) {
        sexpr_free(&test, &ctx->alloc);
        sexpr_free(&arm, &ctx->alloc);
        sexpr_free(&chain, &ctx->alloc);
        return false;
    }

    *out = chain;
    return true;
}

/**
 * Groups the rows by key shape. Exact and open groups can both match one value,
 * so the groups chain through `|` rather than an else arm.
 */
static bool compile_keyed(sexpr_t* out, const matrix_t* m, int64_t col, const int64_t* rows,
                          int64_t n_rows, bool is_record, sexpr_t next, int64_t line, ctx_t* ctx)
{
    int64_t* picked = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_rows);
    if (picked == NULL) {
        sexpr_free(&next, &ctx->alloc);
        return match_oom(ctx, line);
    }

    sexpr_t chain = next;
    sexpr_t arm = { 0 };
    sexpr_t test = { 0 };
    sexpr_t has = { 0 };
    sexpr_t group = { 0 };
    bool ok = true;

    for (int64_t last = n_rows - 1; last >= 0 && ok; last--) {
        bool (*same)(cell_t, cell_t) = is_record ? cell_same_record : cell_same_hashmap;
        if (!first_of_group(m, col, rows, last, same))
            continue;

        sexpr_t shape = cell_pattern(CELL(m, rows[last], col));
        int64_t n_keys = is_record ? record_n_fields(shape) : hashmap_n_keys(shape);
        bool open = is_record ? record_is_open(shape) : hashmap_is_open(shape);
        int64_t n_picked = pick_group(picked, m, col, rows, n_rows, CELL(m, rows[last], col), same);

        if (is_record)
            for (int64_t i = 1; i < n_keys && ok; i++)
                for (int64_t j = 0; j < i && ok; j++)
                    if (sv_str_comp(shape.cons.arr[1 + 2 * i].atom.literal.literal,
                                    shape.cons.arr[1 + 2 * j].atom.literal.literal))
                        ok = match_redefined(ctx, shape.cons.arr[1 + 2 * i].atom.literal.literal,
                                             line);
        if (!ok)
            break;

        decomp_t d = { 0 };
        const layout_t* layout = is_record ? &RECORD_LAYOUT : &MAP_LAYOUT;
        ok = decomp_build(&d, m, col, picked, n_picked, n_keys, shape, layout, line, ctx);
        if (!ok)
            break;

        ok = compile_decomposed(&arm, m, col, picked, n_picked, &d, line, ctx);
        decomp_free(&d, ctx);

        for (int64_t i = n_keys - 1; i >= 0 && ok; i--) {
            sexpr_t key = shape.cons.arr[1 + 2 * i];
            ok = build_call2(&has, is_record ? "has-field?" : "has-key?", m->cols[col].subject, key, line, ctx)
                && build_if(&arm, has, arm, fail_atom(line), line, ctx);
            if (ok)
                has = (sexpr_t){ 0 };
        }

        if (ok) {
            if (open)
                ok = build_call1(&test, is_record ? "is-record?" : "is-hashmap?",
                                 m->cols[col].subject, line, ctx);
            else
                ok = build_call2(&test, is_record ? "is-record?" : "is-hashmap?",
                                 m->cols[col].subject, num_atom((double)n_keys, line), line, ctx);
        }

        ok = ok && build_if(&group, test, arm, fail_atom(line), line, ctx);
        if (ok) {
            test = (sexpr_t){ 0 };
            arm = (sexpr_t){ 0 };
            ok = build_alt(&chain, group, chain, line, ctx);
        }
        if (ok)
            group = (sexpr_t){ 0 };
    }

    sv_free(&ctx->alloc, picked);
    if (!ok) {
        sexpr_free(&has, &ctx->alloc);
        sexpr_free(&group, &ctx->alloc);
        sexpr_free(&test, &ctx->alloc);
        sexpr_free(&arm, &ctx->alloc);
        sexpr_free(&chain, &ctx->alloc);
        return false;
    }

    *out = chain;
    return true;
}

/**
 * Builds the CONS arm: one `list-uncons` form binding the head and the tail,
 * with the tail column known to be a list so its class test is not repeated.
 */
static bool compile_cons_rows(sexpr_t* out, const matrix_t* m, int64_t col, const int64_t* rows,
                              int64_t n_rows, int64_t line, ctx_t* ctx)
{
    matrix_t sub = { 0 };
    sexpr_t inner = { 0 };
    col_t cols[2] = { 0 };
    cell_t* cells = alloc_cells(2 * n_rows, &ctx->alloc);
    if (!alloc_ok(cells, 2 * n_rows))
        goto error;

    if (!next_temp(&cols[0].subject, line, ctx) || !next_temp(&cols[1].subject, line, ctx))
        goto error;

    cols[0].known = PAT_UNKNOWN;
    cols[1].known = PAT_LIST;

    for (int64_t r = 0; r < n_rows; r++) {
        cell_t cell = CELL(m, rows[r], col);
        cells[r * 2] = cell_head(cell);
        cells[r * 2 + 1] = cell_rest(cell);
    }

    if (!specialise(&sub, m, col, rows, n_rows, cols, 2, cells, ctx))
        goto error;

    bool ok = compile_matrix(&inner, &sub, line, ctx);
    matrix_free(&sub, ctx);
    if (!ok)
        goto error;

    sexpr_t items[] = {
        id_atom("list-uncons", line), m->cols[col].subject,
        cols[0].subject, cols[1].subject, inner,
    };
    if (!build_form(out, items, 5, line, ctx)) {
        sexpr_free(&inner, &ctx->alloc);
        sv_free(&ctx->alloc, cells);
        return false;
    }

    sv_free(&ctx->alloc, cells);
    return true;

error:
    matrix_free(&sub, ctx);
    sv_free(&ctx->alloc, cells);
    return match_oom(ctx, line);
}

/**
 * Splits the list rows on the NIL and CONS constructors. A list is either empty
 * or a cons and never both, so the two arms are disjoint and chain through an
 * else rather than through `|`.
 */
static bool compile_lists(sexpr_t* out, const matrix_t* m, int64_t col, const int64_t* rows,
                          int64_t n_rows, int64_t line, ctx_t* ctx)
{
    int64_t* nils = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_rows);
    int64_t* conses = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_rows);
    if (nils == NULL || conses == NULL) {
        sv_free(&ctx->alloc, nils);
        sv_free(&ctx->alloc, conses);
        return match_oom(ctx, line);
    }

    int64_t n_nils = 0;
    int64_t n_conses = 0;
    for (int64_t k = 0; k < n_rows; k++) {
        if (cell_is_nil(CELL(m, rows[k], col)))
            nils[n_nils++] = rows[k];
        else
            conses[n_conses++] = rows[k];
    }

    sexpr_t nil_arm = fail_atom(line);
    sexpr_t cons_arm = fail_atom(line);
    sexpr_t test = { 0 };

    bool ok = n_nils == 0 || compile_dropped(&nil_arm, m, col, nils, n_nils, line, ctx);
    if (ok && n_conses > 0)
        ok = compile_cons_rows(&cons_arm, m, col, conses, n_conses, line, ctx);
    if (ok)
        ok = build_call1(&test, "is-cons?", m->cols[col].subject, line, ctx)
            && build_if(out, test, cons_arm, nil_arm, line, ctx);

    if (!ok) {
        sexpr_free(&test, &ctx->alloc);
        sexpr_free(&cons_arm, &ctx->alloc);
        sexpr_free(&nil_arm, &ctx->alloc);
    }

    sv_free(&ctx->alloc, nils);
    sv_free(&ctx->alloc, conses);
    return ok;
}

static bool compile_class(sexpr_t* out, const matrix_t* m, int64_t col, const int64_t* rows,
                          int64_t n_rows, pattern_class class, sexpr_t next, int64_t line, ctx_t* ctx)
{
    if (class == PAT_TUPLE)
        return compile_tuples(out, m, col, rows, n_rows, next, line, ctx);
    if (class == PAT_RECORD || class == PAT_HASHMAP)
        return compile_keyed(out, m, col, rows, n_rows, class == PAT_RECORD, next, line, ctx);

    sexpr_t arm = { 0 };
    sexpr_t test = { 0 };
    bool ok = class == PAT_NIL
        ? compile_dropped(&arm, m, col, rows, n_rows, line, ctx)
        : class == PAT_LIST
            ? compile_lists(&arm, m, col, rows, n_rows, line, ctx)
            : compile_literals(&arm, m, col, rows, n_rows, line, ctx);

    if (ok && m->cols[col].known == class) {
        sexpr_free(&next, &ctx->alloc);
        *out = arm;
        return true;
    }

    if (ok)
        ok = build_call1(&test, class_predicate(class), m->cols[col].subject, line, ctx);
    if (ok && build_if(out, test, arm, next, line, ctx))
        return true;

    sexpr_free(&test, &ctx->alloc);
    sexpr_free(&arm, &ctx->alloc);
    sexpr_free(&next, &ctx->alloc);
    return false;
}

static bool compile_ctor_group(sexpr_t* out, const matrix_t* m, int64_t col,
                               int64_t n_group, int64_t line, ctx_t* ctx)
{
    int64_t* picked = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_group);
    int64_t* rows = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_group);
    if (picked == NULL || rows == NULL) {
        sv_free(&ctx->alloc, picked);
        sv_free(&ctx->alloc, rows);
        return match_oom(ctx, line);
    }

    for (int64_t i = 0; i < n_group; i++)
        rows[i] = i;

    sexpr_t chain = fail_atom(line);
    bool ok = true;

    for (int64_t last = n_group - 1; last >= 0 && ok; last--) {
        if (!first_of_group(m, col, rows, last, cell_same_class))
            continue;

        cell_t shape = CELL(m, rows[last], col);
        pattern_class class = cell_class(shape);
        int64_t n_picked = pick_group(picked, m, col, rows, n_group, shape, cell_same_class);

        sexpr_t acc = chain;
        chain = (sexpr_t){ 0 };
        ok = compile_class(&chain, m, col, picked, n_picked, class, acc, line, ctx);
    }

    sv_free(&ctx->alloc, picked);
    sv_free(&ctx->alloc, rows);
    if (!ok) {
        sexpr_free(&chain, &ctx->alloc);
        return false;
    }

    *out = chain;
    return true;
}

static bool compile_var_group(sexpr_t* out, const matrix_t* m, int64_t col,
                              int64_t n_group, int64_t line, ctx_t* ctx)
{
    int64_t* picked = sv_malloc(&ctx->alloc, sizeof(int64_t) * (size_t)n_group);
    if (picked == NULL)
        return match_oom(ctx, line);

    for (int64_t i = 0; i < n_group; i++)
        picked[i] = i;

    bool ok = true;
    for (int64_t i = 0; i < n_group && ok; i++) {
        sexpr_t var = cell_pattern(CELL(m, i, col));
        if (is_wildcard(var))
            continue;

        sexpr_t body = take_body(m, i, line);
        sexpr_t wrapped;
        ok = lower_bind(&wrapped, var, m->cols[col].subject, body, line, ctx);
        if (ok)
            *m->bodies[i].slot = wrapped;
    }

    if (ok)
        ok = compile_dropped(out, m, col, picked, n_group, line, ctx);

    sv_free(&ctx->alloc, picked);
    return ok;
}

static bool compile_bodies(sexpr_t* out, const matrix_t* m, int64_t line, ctx_t* ctx)
{
    int64_t last = 0;
    while (last + 1 < m->n_rows && m->bodies[last].guarded)
        last++;

    sexpr_t chain = take_body(m, last, line);
    for (int64_t i = last - 1; i >= 0; i--) {
        sexpr_t body = take_body(m, i, line);
        if (!build_alt(&chain, body, chain, line, ctx)) {
            sexpr_free(&body, &ctx->alloc);
            sexpr_free(&chain, &ctx->alloc);
            return false;
        }
    }

    *out = chain;
    return true;
}

static bool compile_matrix(sexpr_t* out, const matrix_t* m, int64_t line, ctx_t* ctx)
{
    if (m->n_rows == 0) {
        *out = fail_atom(line);
        return true;
    }
    if (m->n_cols == 0)
        return compile_bodies(out, m, line, ctx);

    sexpr_t group = { 0 };
    sexpr_t rest = { 0 };

    int64_t col = best_column(m);
    bool var_run = is_var_cell(CELL(m, 0, col));
    int64_t n_group = 1;
    while (n_group < m->n_rows && is_var_cell(CELL(m, n_group, col)) == var_run)
        n_group++;

    bool ok = var_run
        ? compile_var_group(&group, m, col, n_group, line, ctx)
        : compile_ctor_group(&group, m, col, n_group, line, ctx);

    if (ok) {
        matrix_t tail = matrix_tail(m, n_group);
        ok = compile_matrix(&rest, &tail, line, ctx);
    }

    if (!ok)
        goto error;

    if (build_alt(out, group, rest, line, ctx))
        return true;

error:
    sexpr_free(&group, &ctx->alloc);
    sexpr_free(&rest, &ctx->alloc);
    return false;
}

static bool is_guard(sexpr_t body)
{
    return body.tag == S_CONS && body.cons.arr[0].tag == S_ATOM
        && body.cons.arr[0].atom.kind == TOKEN_KEYWORD
        && body.cons.arr[0].atom.keyword == KEYWORD_WHEN;
}

static bool link_repeat(sexpr_t* slot, sexpr_t* eq, int64_t line, ctx_t* ctx)
{
    sexpr_t dup = { 0 };
    sexpr_t test = { 0 };
    if (!next_temp(&dup, line, ctx)
        || !build_op2(&test, OPERATOR_EQUAL_EQUAL,
                      id_atom_str(slot->atom.literal.literal, line), dup, line, ctx))
        return false;

    *slot = dup;
    if (eq->tag != S_CONS) {
        *eq = test;
        return true;
    }

    sexpr_t joined = { 0 };
    sexpr_t items[] = { and_atom(line), *eq, test };
    if (!cons_build(&joined, items, 3, &ctx->alloc)) {
        sexpr_free(&test, &ctx->alloc);
        return match_oom(ctx, line);
    }

    *eq = joined;
    return true;
}

static bool linearise(sexpr_t* slot, sv_vec_t(sv_str_t)* seen, sexpr_t* eq,
                      int64_t line, ctx_t* ctx)
{
    sexpr_t p = *slot;
    if (p.tag == S_ATOM) {
        if (p.atom.kind != TOKEN_LITERAL || p.atom.literal.kind != LITERAL_IDENTIFIER
            || is_wildcard(p))
            return true;

        for (int64_t i = 0; i < seen->size; i++)
            if (sv_str_comp(seen->arr[i], p.atom.literal.literal))
                return link_repeat(slot, eq, line, ctx);

        int success = 0;
        sv_vec_push(seen, p.atom.literal.literal, &success, &ctx->alloc);
        return success != 0 ? true : match_oom(ctx, line);
    }

    token_t head = p.cons.arr[0].atom;
    if (head.kind != TOKEN_SP_FUNCTION)
        return true;

    if (head.fn == FN_RECORD || head.fn == FN_HASHMAP) {
        int64_t n = head.fn == FN_RECORD ? record_n_fields(p) : hashmap_n_keys(p);
        for (int64_t i = 0; i < n; i++)
            if (!linearise(&p.cons.arr[2 + 2 * i], seen, eq, line, ctx))
                return false;

        return true;
    }

    if (head.fn == FN_LIST) {
        for (int64_t i = 0; i < list_n_fixed(p); i++)
            if (!linearise(&p.cons.arr[1 + i], seen, eq, line, ctx))
                return false;

        if (list_has_tail(p))
            return linearise(&p.cons.arr[p.cons.size - 1].cons.arr[1], seen, eq, line, ctx);

        return true;
    }

    for (int64_t i = 1; i < p.cons.size; i++)
        if (!linearise(&p.cons.arr[i], seen, eq, line, ctx))
            return false;

    return true;
}

static bool lower_repeats(sexpr_t* pattern, row_t* row, int64_t line, ctx_t* ctx)
{
    sv_vec_t(sv_str_t) seen = sv_vec_init(sv_str_t);
    sexpr_t eq = { 0 };

    bool ok = linearise(pattern, &seen, &eq, line, ctx);
    sv_vec_deinit(&seen, &ctx->alloc);
    if (!ok) {
        sexpr_free(&eq, &ctx->alloc);
        return false;
    }
    if (eq.tag != S_CONS)
        return true;

    sexpr_t marker = *row->slot;
    if (is_guard(marker)) {
        sexpr_t joined = { 0 };
        sexpr_t items[] = { and_atom(line), eq, marker.cons.arr[1] };
        if (!cons_build(&joined, items, 3, &ctx->alloc)) {
            sexpr_free(&eq, &ctx->alloc);
            return match_oom(ctx, line);
        }

        marker.cons.arr[1] = joined;
        return true;
    }

    sexpr_t body = *row->slot;
    *row->slot = nil_atom(line);

    sexpr_t wrapped = { 0 };
    sexpr_t items[] = { when_atom(line), eq, body };
    if (!cons_build(&wrapped, items, 3, &ctx->alloc)) {
        *row->slot = body;
        sexpr_free(&eq, &ctx->alloc);
        return match_oom(ctx, line);
    }

    *row->slot = wrapped;
    return true;
}

static bool lower_guard(row_t* row, int64_t line, ctx_t* ctx)
{
    sexpr_t marker = *row->slot;
    if (!is_guard(marker))
        return true;

    sexpr_t guard = marker.cons.arr[1];
    sexpr_t body = marker.cons.arr[2];
    marker.cons.arr[1] = nil_atom(line);
    marker.cons.arr[2] = nil_atom(line);

    sexpr_t lowered = { 0 };
    sexpr_t items[] = { fn_atom(FN_IF, line), guard, body, fail_atom(line) };
    if (!cons_build(&lowered, items, 4, &ctx->alloc)) {
        marker.cons.arr[1] = guard;
        marker.cons.arr[2] = body;
        return match_oom(ctx, line);
    }

    sexpr_free(&marker, &ctx->alloc);
    *row->slot = lowered;
    row->guarded = true;
    return true;
}

static sexpr_t match_lower(sexpr_t s, ctx_t* ctx)
{
    cons_t match = s.cons;
    int64_t line = match.arr[0].atom.line;
    int64_t n_rows = match.size - CLAUSES_START;

    matrix_t m = { 0 };
    sexpr_t subject = { 0 };
    sexpr_t chain = { 0 };
    sexpr_t out = { 0 };

    if (!next_temp(&subject, line, ctx))
        return discard(&s, ctx);

    if (!matrix_alloc(&m, 1, n_rows, ctx)) {
        matrix_free(&m, ctx);
        match_oom(ctx, line);
        return discard(&s, ctx);
    }

    m.cols[0] = (col_t){ .subject = subject, .known = PAT_UNKNOWN };
    bool ok = true;
    for (int64_t i = 0; i < n_rows && ok; i++) {
        sexpr_t* slots = match.arr[CLAUSES_START + i].cons.arr;
        m.cells[i] = cell_of(&slots[1]);
        m.bodies[i] = (row_t){ .slot = &slots[2] };
        ok = lower_repeats(&slots[1], &m.bodies[i], line, ctx)
            && lower_guard(&m.bodies[i], line, ctx);
    }
    if (!ok) {
        matrix_free(&m, ctx);
        return discard(&s, ctx);
    }

    ok = compile_matrix(&chain, &m, line, ctx);
    matrix_free(&m, ctx);
    if (!ok)
        return discard(&s, ctx);

    sexpr_t def = { 0 };
    if (!build_call1(&def, "match-fail", subject, line, ctx)) {
        sexpr_free(&chain, &ctx->alloc);
        return discard(&s, ctx);
    }

    if (!build_alt(&chain, chain, def, line, ctx)) {
        sexpr_free(&chain, &ctx->alloc);
        sexpr_free(&def, &ctx->alloc);
        return discard(&s, ctx);
    }

    sexpr_t scrutinee = match.arr[1];
    match.arr[1] = nil_atom(line);

    if (!lower_bind(&out, subject, scrutinee, chain, line, ctx))
        return discard(&s, ctx);

    sexpr_free(&s, &ctx->alloc);
    return out;
}

sexpr_t match_compile(sexpr_t s, ctx_t* ctx)
{
    if (s.tag != S_CONS || s.cons.size < CLAUSES_START)
        return discard(&s, ctx);

    temps_used = 0;
    return match_lower(s, ctx);
}

/**
 * Rewrites every `(match ...)` node in the tree, children first, so a match
 * nested in a clause body is lowered before its parent. Takes ownership: on
 * failure the whole tree is freed and an error atom is returned.
 */
static bool lower_children(sexpr_t s, ctx_t* ctx)
{
    if (s.tag != S_CONS)
        return true;

    for (int64_t i = 0; i < s.cons.size; i++) {
        if (!lower_children(s.cons.arr[i], ctx))
            return false;

        sexpr_t child = s.cons.arr[i];
        if (child.tag != S_CONS || child.cons.size == 0)
            continue;

        sexpr_t head = child.cons.arr[0];
        if (head.tag != S_ATOM || head.atom.kind != TOKEN_SP_FUNCTION || head.atom.fn != FN_MATCH)
            continue;

        s.cons.arr[i] = match_lower(child, ctx);
        if (is_error_sexpr(s.cons.arr[i]))
            return false;
    }
    return true;
}

sexpr_t match_lower_tree(sexpr_t s, ctx_t* ctx)
{
    temps_used = 0;

    if (!lower_children(s, ctx))
        return discard(&s, ctx);

    if (s.tag != S_CONS || s.cons.size == 0)
        return s;

    sexpr_t head = s.cons.arr[0];
    if (head.tag != S_ATOM || head.atom.kind != TOKEN_SP_FUNCTION || head.atom.fn != FN_MATCH)
        return s;

    return match_lower(s, ctx);
}

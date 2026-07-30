#include "str.h"
#include "../value.h"
#include "../obj.h"

static sv_rc_t(obj_t) str_obj(sv_str_t* s)
{
    return (sv_rc_t(obj_t)){ .cell = (sv_rc_cell_t(obj_t)*)s };
}

static int64_t utf8_len(char lead)
{
    uint8_t b = (uint8_t)lead;
    if ((b & 0x80) == 0x00) return 1;
    if ((b & 0xE0) == 0xC0) return 2;
    if ((b & 0xF0) == 0xE0) return 3;
    if ((b & 0xF8) == 0xF0) return 4;
    return 1;
}

str_iter_t str_iter_init(sv_str_t* s)
{
    sv_rc_borrow(str_obj(s));
    return (str_iter_t){ .str = s, .i = 0 };
}

void str_iter_deinit(str_iter_t* it, const sv_allocator_t* a)
{
    sv_rc_t(obj_t) obj = str_obj(it->str);
    sv_rc_deinit(&obj, a);
}

value_t str_iter_next(str_iter_t* it, const sv_allocator_t* a)
{
    if (it->i >= it->str->size)
        return (value_t){ .kind = VALUE_NIL };

    int64_t len = utf8_len(it->str->chars[it->i]);
    sv_str_t slice = sv_str_slice(*it->str, it->i, it->i + len);
    it->i += len;
    return value_init_str(slice, a);
}

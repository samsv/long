#include "stable_sort.h"
#include <stdlib.h>
#include <string.h>

#define STABLE_SORT_RUN 16

static void swap_items(char* a, char* b, size_t size)
{
    for (size_t k = 0; k < size; k++) {
        char c = a[k];
        a[k] = b[k];
        b[k] = c;
    }
}

static void insertion_sort(
    char* items,
    size_t nmemb,
    size_t size,
    int (*compar)(const void*, const void*)
) {
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0 && compar(items + (j - 1) * size, items + j * size) > 0; j--)
            swap_items(items + (j - 1) * size, items + j * size, size);
    }
}

static void merge(
    char* dst,
    const char* a,
    size_t na,
    const char* b,
    size_t nb,
    size_t size,
    int (*compar)(const void*, const void*)
) {
    while (na > 0 && nb > 0) {
        if (compar(a, b) <= 0) {
            memcpy(dst, a, size);
            a += size;
            na--;
        } else {
            memcpy(dst, b, size);
            b += size;
            nb--;
        }
        dst += size;
    }

    memcpy(dst, a, na * size);
    memcpy(dst + na * size, b, nb * size);
}

void stable_sort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    char* items = base;
    if (nmemb <= STABLE_SORT_RUN) {
        insertion_sort(items, nmemb, size, compar);
        return;
    }

    char* buf = malloc(nmemb * size);
    if (buf == NULL) {
        insertion_sort(items, nmemb, size, compar);
        return;
    }

    for (size_t i = 0; i < nmemb; i += STABLE_SORT_RUN) {
        size_t n = nmemb - i > STABLE_SORT_RUN ? STABLE_SORT_RUN : nmemb - i;
        insertion_sort(items + i * size, n, size, compar);
    }

    char* from = items;
    char* to = buf;
    for (size_t width = STABLE_SORT_RUN; width < nmemb; width *= 2) {
        for (size_t i = 0; i < nmemb; i += 2 * width) {
            size_t mid = nmemb - i > width ? i + width : nmemb;
            size_t hi = nmemb - i > 2 * width ? i + 2 * width : nmemb;
            merge(to + i * size, from + i * size, mid - i, from + mid * size, hi - mid,
                  size, compar);
        }

        char* swap = from;
        from = to;
        to = swap;
    }

    if (from != items)
        memcpy(items, from, nmemb * size);

    free(buf);
}

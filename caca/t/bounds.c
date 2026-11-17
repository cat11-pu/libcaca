/*
 *  bounds       regression tests for canvas size and coordinate validation
 *  Copyright © 2026 libcaca contributors
 *
 *  This program is free software. It comes without any warranty, to
 *  the extent permitted by applicable law.
 */

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "caca.h"

static int failures;

#define CHECK(expr) do { \
    if(!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s (errno=%d)\n", \
                __FILE__, __LINE__, #expr, errno); \
        failures++; \
    } \
} while(0)

static unsigned int canvas_hash(caca_canvas_t *cv)
{
    unsigned int hash = 2166136261u;
    uint32_t const *chars = caca_get_canvas_chars(cv);
    uint32_t const *attrs = caca_get_canvas_attrs(cv);
    int i, cells = caca_get_canvas_width(cv) * caca_get_canvas_height(cv);

    for(i = 0; i < cells; i++)
    {
        hash = (hash ^ chars[i]) * 16777619u;
        hash = (hash ^ attrs[i]) * 16777619u;
    }

    return hash;
}

static void test_dimensions(void)
{
    caca_canvas_t *cv;
    unsigned int before;

    errno = 0;
    CHECK(caca_create_canvas(-1, 1) == NULL);
    CHECK(errno == EINVAL);

    errno = 0;
    CHECK(caca_create_canvas(1, -1) == NULL);
    CHECK(errno == EINVAL);

    errno = 0;
    CHECK(caca_create_canvas(INT_MAX, 2) == NULL);
    CHECK(errno == EOVERFLOW);

    errno = 0;
    CHECK(caca_create_canvas(INT_MAX / 2, 3) == NULL);
    CHECK(errno == EOVERFLOW);

    cv = caca_create_canvas(7, 5);
    CHECK(cv != NULL);
    before = canvas_hash(cv);

    errno = 0;
    CHECK(caca_set_canvas_size(cv, -1, 2) == -1);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(caca_set_canvas_size(cv, INT_MAX, 2) == -1);
    CHECK(errno == EOVERFLOW);

    CHECK(caca_get_canvas_width(cv) == 7);
    CHECK(caca_get_canvas_height(cv) == 5);
    CHECK(canvas_hash(cv) == before);

    CHECK(caca_set_canvas_size(cv, 0, 0) == 0);
    CHECK(caca_get_canvas_width(cv) == 0);
    CHECK(caca_get_canvas_height(cv) == 0);

    CHECK(caca_set_canvas_size(cv, 4, 4) == 0);
    errno = 0;
    CHECK(caca_set_canvas_boundaries(cv, 0, 0, INT_MAX, INT_MAX) == -1);
    CHECK(errno == EOVERFLOW);
    CHECK(caca_get_canvas_width(cv) == 4);
    CHECK(caca_get_canvas_height(cv) == 4);
    caca_free_canvas(cv);
}

static void test_coordinates(void)
{
    caca_canvas_t *cv, *src;
    unsigned int before;
    int x, y, w, h;

    cv = caca_create_canvas(8, 4);
    CHECK(cv != NULL);
    caca_clear_dirty_rect_list(cv);

    caca_draw_line(cv, INT_MIN, INT_MIN, INT_MAX, INT_MAX, '#');
    CHECK(caca_get_char(cv, 0, 0) == '#');
    CHECK(caca_get_char(cv, 3, 3) == '#');

    caca_clear_canvas(cv);
    before = canvas_hash(cv);

    CHECK(caca_draw_box(cv, INT_MAX - 1, INT_MAX - 1,
                        INT_MAX, INT_MAX, '#') == 0);
    CHECK(canvas_hash(cv) == before);

    CHECK(caca_fill_box(cv, INT_MIN, INT_MIN,
                        INT_MAX, INT_MAX, '#') == 0);
    CHECK(canvas_hash(cv) == before);

    src = caca_create_canvas(2, 2);
    caca_put_char(src, 0, 0, 'X');
    CHECK(caca_blit(cv, INT_MAX, INT_MAX, src, NULL) == 0);
    CHECK(canvas_hash(cv) == before);
    CHECK(caca_blit(cv, INT_MIN, INT_MIN, src, NULL) == 0);
    CHECK(canvas_hash(cv) == before);
    caca_set_canvas_handle(src, INT_MIN, INT_MIN);
    CHECK(caca_blit(cv, 0, 0, src, NULL) == 0);
    CHECK(canvas_hash(cv) == before);
    caca_free_canvas(src);

    CHECK(caca_put_str(cv, INT_MIN, 0, "outside") == 7);
    CHECK(canvas_hash(cv) == before);

    caca_clear_dirty_rect_list(cv);
    CHECK(caca_add_dirty_rect(cv, 1, 0, INT_MAX, 1) == 0);
    CHECK(caca_get_dirty_rect_count(cv) == 1);
    CHECK(caca_get_dirty_rect(cv, 0, &x, &y, &w, &h) == 0);
    CHECK(x == 1 && y == 0 && w == 7 && h == 1);

    CHECK(caca_add_dirty_rect(cv, INT_MAX, 0, 1, 1) == -1);
    CHECK(errno == EINVAL);
    CHECK(caca_remove_dirty_rect(cv, INT_MAX, 0, 1, 1) == -1);
    CHECK(errno == EINVAL);

    before = canvas_hash(cv);
    errno = 0;
    CHECK(caca_printf(cv, INT_MIN, 0, "%s", "outside") == -1);
    CHECK(errno == EOVERFLOW);
    CHECK(canvas_hash(cv) == before);

    caca_free_canvas(cv);
}

static void test_ellipses(void)
{
    caca_canvas_t *cv = caca_create_canvas(20, 10);

    errno = 0;
    CHECK(caca_draw_circle(cv, 5, 5, -1, '#') == -1);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(caca_draw_ellipse(cv, 5, 5, INT_MAX, 2, '#') == -1);
    CHECK(errno == EOVERFLOW);
    errno = 0;
    CHECK(caca_fill_ellipse(cv, 5, 5, 2, INT_MAX, '#') == -1);
    CHECK(errno == EOVERFLOW);
    errno = 0;
    CHECK(caca_draw_thin_ellipse(cv, 5, 5, -1, 2) == -1);
    CHECK(errno == EINVAL);

    caca_free_canvas(cv);
}

static void test_transforms(void)
{
    caca_canvas_t *cv = caca_create_canvas(0, 0);

    CHECK(caca_flip(cv) == 0);
    CHECK(caca_flop(cv) == 0);
    CHECK(caca_rotate_180(cv) == 0);
    CHECK(caca_rotate_left(cv) == 0);
    CHECK(caca_rotate_right(cv) == 0);
    CHECK(caca_stretch_left(cv) == 0);
    CHECK(caca_stretch_right(cv) == 0);
    CHECK(caca_get_canvas_width(cv) == 0);
    CHECK(caca_get_canvas_height(cv) == 0);
    caca_free_canvas(cv);

    cv = caca_create_canvas(3, 2);
    caca_put_char(cv, 0, 0, 'A');
    CHECK(caca_rotate_left(cv) == 0);
    CHECK(caca_get_canvas_width(cv) == 4);
    CHECK(caca_get_canvas_height(cv) == 2);
    CHECK(caca_rotate_right(cv) == 0);
    CHECK(caca_get_canvas_width(cv) == 4);
    CHECK(caca_get_canvas_height(cv) == 2);
    caca_free_canvas(cv);
}

static void test_dither(void)
{
    uint32_t pixels[16];
    caca_canvas_t *cv = caca_create_canvas(12, 6);
    caca_dither_t *d = caca_create_dither(32, 4, 4, 16,
        0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000u);
    unsigned int before;
    int i;

    CHECK(cv != NULL);
    CHECK(d != NULL);
    for(i = 0; i < 16; i++)
        pixels[i] = 0xff000000u | ((uint32_t)i * 0x000f0f0fu);

    before = canvas_hash(cv);
    errno = 0;
    CHECK(caca_dither_bitmap(cv, 0, 0, 2, 2, NULL, pixels) == -1);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(caca_dither_bitmap(cv, 0, 0, 0, 2, d, pixels) == -1);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(caca_dither_bitmap(cv, 0, 0, 2, -1, d, pixels) == -1);
    CHECK(errno == EINVAL);
    CHECK(canvas_hash(cv) == before);

    CHECK(caca_dither_bitmap(cv, INT_MAX, INT_MAX, 2, 2, d, pixels) == 0);
    CHECK(canvas_hash(cv) == before);
    CHECK(caca_dither_bitmap(cv, 1, 1, 8, 4, d, pixels) == 0);
    CHECK(canvas_hash(cv) != before);

    caca_free_dither(d);
    caca_free_canvas(cv);
}

static void test_legal_fingerprint(void)
{
    caca_canvas_t *cv = caca_create_canvas(10, 6);
    unsigned int hash;

    caca_set_color_ansi(cv, CACA_WHITE, CACA_BLUE);
    caca_put_str(cv, 1, 1, "libcaca");
    caca_draw_line(cv, 0, 0, 9, 5, '*');
    caca_draw_box(cv, 2, 2, 6, 3, '#');
    caca_fill_triangle(cv, 0, 5, 4, 1, 8, 5, '+');
    caca_flip(cv);
    caca_flop(cv);
    caca_rotate_180(cv);

    hash = canvas_hash(cv);
    CHECK(hash == 0xfd8016d5u);
    caca_free_canvas(cv);
}

int main(void)
{
    test_dimensions();
    test_coordinates();
    test_ellipses();
    test_transforms();
    test_dither();
    test_legal_fingerprint();

    if(failures)
    {
        fprintf(stderr, "%d bounds test(s) failed\n", failures);
        return 1;
    }

    puts("bounds tests passed");
    return 0;
}

#include "config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "caca.h"

struct vt
{
    int width, height;
    uint32_t *cells;
    int x, y;
    int pending;
};

static int failures;

#define CHECK(cond) \
    do { if(!(cond)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        failures++; \
    } } while(0)

static void vt_create(struct vt *vt, int width, int height)
{
    size_t cells = (size_t)width * (size_t)height;
    size_t i;

    memset(vt, 0, sizeof(*vt));
    vt->width = width;
    vt->height = height;
    vt->cells = calloc(cells ? cells : 1, sizeof(uint32_t));
    if(!vt->cells)
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    for(i = 0; i < cells; i++)
        vt->cells[i] = ' ';
}

static void vt_free(struct vt *vt)
{
    free(vt->cells);
    memset(vt, 0, sizeof(*vt));
}

static void vt_clear(struct vt *vt, int width, int height)
{
    size_t cells = (size_t)width * (size_t)height;
    size_t i;

    vt->width = width;
    vt->height = height;
    vt->cells = realloc(vt->cells, (cells ? cells : 1) * sizeof(uint32_t));
    if(!vt->cells)
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
    for(i = 0; i < cells; i++)
        vt->cells[i] = ' ';
    vt->x = vt->y = 0;
    vt->pending = 0;
}

static void vt_put(struct vt *vt, uint32_t ch)
{
    int width = caca_utf32_is_fullwidth(ch) ? 2 : 1;
    size_t index;

    if(vt->width <= 0 || vt->height <= 0)
        return;

    if(vt->pending)
    {
        vt->x = 0;
        vt->y++;
        vt->pending = 0;
        if(vt->y >= vt->height)
            return;
    }

    if(vt->x >= vt->width)
        vt->x = vt->width - 1;

    if(width == 2 && vt->x + 1 >= vt->width)
    {
        ch = ' ';
        width = 1;
    }

    index = (size_t)vt->x + (size_t)vt->y * vt->width;
    if(vt->x && vt->cells[index] == CACA_MAGIC_FULLWIDTH)
        vt->cells[index - 1] = ' ';
    if(vt->x + 1 < vt->width
       && vt->cells[index + 1] == CACA_MAGIC_FULLWIDTH)
        vt->cells[index + 1] = ' ';
    if(width == 2 && vt->x + 2 < vt->width
       && vt->cells[index + 2] == CACA_MAGIC_FULLWIDTH)
        vt->cells[index + 2] = ' ';

    if(width == 2)
    {
        vt->cells[index] = ch;
        vt->cells[index + 1] = CACA_MAGIC_FULLWIDTH;
    }
    else
    {
        if(vt->x + 1 < vt->width
           && vt->cells[index + 1] == CACA_MAGIC_FULLWIDTH)
            vt->cells[index + 1] = ' ';
        vt->cells[index] = ch;
    }

    vt->x += width;
    if(vt->x >= vt->width)
    {
        vt->x = vt->width - 1;
        vt->pending = 1;
    }
}

static int vt_param(int const *param, int count, int index, int fallback)
{
    if(index >= count || param[index] <= 0)
        return fallback;
    return param[index];
}

static void vt_csi(struct vt *vt, char final, int const *param, int count)
{
    int amount, row, column;

    switch(final)
    {
        case 'H':
        case 'f':
            row = vt_param(param, count, 0, 1);
            column = vt_param(param, count, 1, 1);
            if(row > vt->height)
                row = vt->height ? vt->height : 1;
            if(column > vt->width)
                column = vt->width ? vt->width : 1;
            vt->x = column - 1;
            vt->y = row - 1;
            vt->pending = 0;
            break;

        case 'A':
            amount = vt_param(param, count, 0, 1);
            vt->y -= amount;
            if(vt->y < 0)
                vt->y = 0;
            vt->pending = 0;
            break;

        case 'B':
            amount = vt_param(param, count, 0, 1);
            vt->y += amount;
            if(vt->y >= vt->height)
                vt->y = vt->height ? vt->height - 1 : 0;
            vt->pending = 0;
            break;

        case 'C':
            amount = vt_param(param, count, 0, 1);
            vt->x += amount;
            if(vt->x >= vt->width)
                vt->x = vt->width ? vt->width - 1 : 0;
            vt->pending = 0;
            break;

        case 'D':
            amount = vt_param(param, count, 0, 1);
            vt->x -= amount;
            if(vt->x < 0)
                vt->x = 0;
            vt->pending = 0;
            break;

        case 'J':
            if(count && param[0] == 2)
            {
                size_t i, cells = (size_t)vt->width * (size_t)vt->height;
                for(i = 0; i < cells; i++)
                    vt->cells[i] = ' ';
            }
            break;

        default:
            break;
    }
}

static void vt_apply(struct vt *vt, char const *data, size_t bytes)
{
    size_t i = 0;

    while(i < bytes)
    {
        if(data[i] == '\033')
        {
            if(i + 1 < bytes && data[i + 1] == '[')
            {
                int param[8], count = 0, value = 0, have_value = 0;
                size_t j = i + 2;

                while(j < bytes)
                {
                    char ch = data[j];
                    if(ch >= '0' && ch <= '9')
                    {
                        value = value * 10 + ch - '0';
                        have_value = 1;
                        j++;
                    }
                    else if(ch == ';')
                    {
                        if(count < 8)
                            param[count++] = have_value ? value : 0;
                        value = 0;
                        have_value = 0;
                        j++;
                    }
                    else if(ch >= 0x40 && ch <= 0x7e)
                    {
                        if(have_value && count < 8)
                            param[count++] = value;
                        vt_csi(vt, ch, param, count);
                        j++;
                        break;
                    }
                    else
                    {
                        j++;
                    }
                }
                i = j;
            }
            else
            {
                i += i + 1 < bytes ? 2 : 1;
            }
            continue;
        }

        if(data[i] == '\r')
        {
            vt->x = 0;
            vt->pending = 0;
            i++;
        }
        else if(data[i] == '\n')
        {
            vt->y++;
            vt->pending = 0;
            if(vt->y >= vt->height)
                vt->y = vt->height ? vt->height - 1 : 0;
            i++;
        }
        else if((unsigned char)data[i] < 0x20)
        {
            i++;
        }
        else
        {
            size_t used = 0;
            uint32_t ch = caca_utf8_to_utf32(data + i, &used);
            if(!used)
                used = 1;
            vt_put(vt, ch);
            i += used;
        }
    }
}

static int expected_cursor(caca_canvas_t const *cv, int *x, int *y)
{
    int width = caca_get_canvas_width(cv);
    int height = caca_get_canvas_height(cv);

    *x = caca_wherex(cv);
    *y = caca_wherey(cv);

    if(width <= 0)
        *x = 0;
    else if(*x < 0)
        *x = 0;
    else if(*x >= width)
        *x = width - 1;

    if(height <= 0)
        *y = 0;
    else if(*y < 0)
        *y = 0;
    else if(*y >= height)
        *y = height - 1;

    return 0;
}

static int compare_chars(struct vt const *vt, caca_canvas_t const *cv,
                         char const *label)
{
    int x, y;
    size_t i, cells;

    if(vt->width != caca_get_canvas_width(cv)
        || vt->height != caca_get_canvas_height(cv))
    {
        fprintf(stderr, "%s: size mismatch: vt=%dx%d canvas=%dx%d\n",
                label, vt->width, vt->height,
                caca_get_canvas_width(cv), caca_get_canvas_height(cv));
        return 0;
    }

    cells = (size_t)vt->width * (size_t)vt->height;
    for(i = 0; i < cells; i++)
    {
        x = (int)(i % (size_t)vt->width);
        y = (int)(i / (size_t)vt->width);
        if(vt->cells[i] != caca_get_char(cv, x, y))
        {
            fprintf(stderr,
                    "%s: cell mismatch at (%d,%d): vt=U+%04x canvas=U+%04x\n",
                    label, x, y, vt->cells[i], caca_get_char(cv, x, y));
            return 0;
        }
    }

    return 1;
}

static int compare_cursor(struct vt const *vt, caca_canvas_t const *cv,
                          char const *label)
{
    int ex, ey;

    expected_cursor(cv, &ex, &ey);
    if(vt->x != ex || vt->y != ey)
    {
        fprintf(stderr,
                "%s: cursor mismatch: vt=(%d,%d) canvas=(%d,%d)\n",
                label, vt->x, vt->y, ex, ey);
        return 0;
    }

    return 1;
}

static int compare_vt(struct vt const *vt, caca_canvas_t const *cv,
                      char const *label)
{
    int chars_ok = compare_chars(vt, cv, label);
    int cursor_ok = compare_cursor(vt, cv, label);

    return chars_ok && cursor_ok;
}

static void check_refresh(struct vt *vt, caca_diff_t *dd, caca_canvas_t *cv,
                          int x, int y, int width, int height,
                          char const *label, size_t *frame_bytes)
{
    char *buf = NULL;
    size_t bytes = 0;
    int ret;

    ret = caca_diff_refresh(dd, cv, x, y, width, height, &buf, &bytes);
    CHECK(ret == 0);
    CHECK(buf != NULL);
    if(ret == 0)
    {
        CHECK(buf[bytes] == '\0');
        vt_apply(vt, buf, bytes);
        CHECK(compare_vt(vt, cv, label));
    }
    if(frame_bytes)
        *frame_bytes = bytes;
    free(buf);
}

static void fill_canvas(caca_canvas_t *cv)
{
    int x, y;

    for(y = 0; y < caca_get_canvas_height(cv); y++)
        for(x = 0; x < caca_get_canvas_width(cv); x++)
            caca_put_char(cv, x, y, 'a' + (x + 3 * y) % 26);
}

static void test_basic(void)
{
    caca_canvas_t *cv = caca_create_canvas(8, 3);
    caca_diff_t *dd = caca_create_diff();
    struct vt vt;
    size_t full_bytes, diff_bytes;

    vt_create(&vt, 8, 3);
    fill_canvas(cv);
    caca_gotoxy(cv, 2, 1);
    check_refresh(&vt, dd, cv, 0, 0, 8, 3, "basic full", &full_bytes);

    caca_put_char(cv, 3, 1, 'X');
    check_refresh(&vt, dd, cv, 3, 1, 1, 1, "basic one cell", &diff_bytes);
    CHECK(diff_bytes < full_bytes);

    check_refresh(&vt, dd, cv, 3, 1, 1, 1, "basic no-op", &diff_bytes);
    CHECK(diff_bytes == 0);

    caca_gotoxy(cv, 7, 2);
    check_refresh(&vt, dd, cv, 0, 0, 8, 3, "basic cursor", &diff_bytes);
    CHECK(diff_bytes > 0);

    caca_diff_free(dd);
    caca_free_canvas(cv);
    vt_free(&vt);
}

static void test_edges(void)
{
    caca_canvas_t *cv = caca_create_canvas(6, 3);
    caca_diff_t *dd = caca_create_diff();
    struct vt vt;
    char *buf = (char *)1;
    size_t bytes = 123;

    vt_create(&vt, 6, 3);
    fill_canvas(cv);
    check_refresh(&vt, dd, cv, 0, 0, 6, 3, "edges full", NULL);

    caca_put_char(cv, 0, 0, 'A');
    caca_put_char(cv, 2, 1, 'B');
    check_refresh(&vt, dd, cv, -2, -1, 5, 3,
                  "edges clipped rectangle", NULL);

    caca_put_char(cv, 4, 2, 'C');
    check_refresh(&vt, dd, cv, 4, 2, 1, 1,
                  "edges lower right", NULL);

    errno = 0;
    CHECK(caca_diff_refresh(dd, cv, 0, 0, -1, 1, &buf, &bytes) == -1);
    CHECK(errno == EINVAL);
    CHECK(buf == NULL && bytes == 0);

    caca_gotoxy(cv, 1, 1);
    check_refresh(&vt, dd, cv, 100, 100, 4, 4,
                  "edges no intersection", NULL);

    caca_gotoxy(cv, 5, 0);
    check_refresh(&vt, dd, cv, 2147483647, 2147483647,
                 2147483647, 2147483647, "edges overflow rectangle", NULL);

    buf = (char *)1;
    bytes = 123;
    CHECK(caca_diff_refresh(dd, cv, 0, 0, 1, 1, NULL, &bytes) == -1);
    CHECK(caca_diff_refresh(dd, cv, 0, 0, 1, 1, &buf, NULL) == -1);
    CHECK(caca_diff_refresh(NULL, cv, 0, 0, 1, 1, &buf, &bytes) == -1);
    CHECK(caca_diff_refresh(dd, NULL, 0, 0, 1, 1, &buf, &bytes) == -1);
    CHECK(caca_diff_reset(NULL) == -1);
    CHECK(caca_diff_free(NULL) == 0);

    caca_diff_free(dd);
    caca_free_canvas(cv);
    vt_free(&vt);
}

static void test_resize_and_fullwidth(void)
{
    caca_canvas_t *cv = caca_create_canvas(5, 2);
    caca_diff_t *dd = caca_create_diff();
    struct vt vt;

    vt_create(&vt, 5, 2);
    fill_canvas(cv);
    check_refresh(&vt, dd, cv, 0, 0, 5, 2, "resize initial", NULL);

    CHECK(caca_set_canvas_size(cv, 7, 4) == 0);
    vt_clear(&vt, 7, 4);
    fill_canvas(cv);
    check_refresh(&vt, dd, cv, 0, 0, 0, 0, "resize larger", NULL);

    CHECK(caca_set_canvas_size(cv, 3, 1) == 0);
    vt_clear(&vt, 3, 1);
    caca_put_str(cv, 0, 0, "xyz");
    check_refresh(&vt, dd, cv, 0, 0, 3, 1, "resize smaller", NULL);

    caca_free_canvas(cv);
    vt_free(&vt);
    caca_diff_free(dd);

    cv = caca_create_canvas(8, 2);
    dd = caca_create_diff();
    vt_create(&vt, 8, 2);
    fill_canvas(cv);
    check_refresh(&vt, dd, cv, 0, 0, 8, 2, "wide initial", NULL);

    caca_put_char(cv, 2, 0, 0x4e00);
    check_refresh(&vt, dd, cv, 2, 0, 2, 1, "wide insert", NULL);

    caca_put_char(cv, 2, 0, 0x4e01);
    check_refresh(&vt, dd, cv, 2, 0, 1, 1, "wide base only rectangle", NULL);

    caca_put_char(cv, 4, 0, 0x4e02);
    check_refresh(&vt, dd, cv, 3, 0, 2, 1, "wide continuation start", NULL);

    caca_put_char(cv, 3, 0, 'x');
    check_refresh(&vt, dd, cv, 3, 0, 2, 1, "wide overwrite", NULL);

    caca_clear_canvas(cv);
    caca_gotoxy(cv, 6, 1);
    check_refresh(&vt, dd, cv, 0, 0, 8, 2, "wide clear", NULL);

    caca_free_canvas(cv);
    caca_diff_free(dd);
    vt_free(&vt);
}

static void test_empty_canvas(void)
{
    caca_canvas_t *cv = caca_create_canvas(0, 0);
    caca_diff_t *dd = caca_create_diff();
    struct vt vt;

    vt_create(&vt, 0, 0);
    check_refresh(&vt, dd, cv, 0, 0, 0, 0, "empty canvas", NULL);

    caca_diff_free(dd);
    caca_free_canvas(cv);
    vt_free(&vt);
}

static int count_byte(char const *data, size_t bytes, char value)
{
    size_t i;
    int count = 0;

    for(i = 0; i < bytes; i++)
        if(data[i] == value)
            count++;

    return count;
}

static void test_minimal_sequences(void)
{
    caca_canvas_t *cv = caca_create_canvas(12, 1);
    caca_diff_t *dd = caca_create_diff();
    struct vt vt;
    char *buf;
    size_t bytes;

    vt_create(&vt, 12, 1);
    fill_canvas(cv);
    check_refresh(&vt, dd, cv, 0, 0, 12, 1, "minimal full", NULL);

    caca_put_str(cv, 2, 0, "ABCDE");
    CHECK(caca_diff_refresh(dd, cv, 2, 0, 5, 1, &buf, &bytes) == 0);
    vt_apply(&vt, buf, bytes);
    CHECK(compare_vt(&vt, cv, "minimal default run"));
    CHECK(count_byte(buf, bytes, 'm') == 0);
    free(buf);

    CHECK(caca_diff_refresh(dd, cv, 2, 0, 5, 1, &buf, &bytes) == 0);
    CHECK(bytes == 0);
    free(buf);

    caca_set_color_ansi(cv, CACA_RED, CACA_BLUE);
    caca_set_attr(cv, CACA_BOLD);
    caca_put_str(cv, 2, 0, "XY");
    CHECK(caca_diff_refresh(dd, cv, 2, 0, 2, 1, &buf, &bytes) == 0);
    vt_apply(&vt, buf, bytes);
    CHECK(compare_vt(&vt, cv, "minimal styled run"));
    CHECK(count_byte(buf, bytes, 'm') == 2);
    free(buf);

    caca_diff_free(dd);
    caca_free_canvas(cv);
    vt_free(&vt);
}

static uint32_t rng_state = 0x12345678u;

static uint32_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void dirty_bounds(caca_canvas_t *cv, int *x, int *y,
                         int *width, int *height)
{
    int i, count = caca_get_dirty_rect_count(cv);
    int x0 = caca_get_canvas_width(cv), y0 = caca_get_canvas_height(cv);
    int x1 = -1, y1 = -1;

    for(i = 0; i < count; i++)
    {
        int dx, dy, dw, dh;
        caca_get_dirty_rect(cv, i, &dx, &dy, &dw, &dh);
        if(dx < x0) x0 = dx;
        if(dy < y0) y0 = dy;
        if(dx + dw > x1) x1 = dx + dw;
        if(dy + dh > y1) y1 = dy + dh;
    }

    if(x1 <= x0 || y1 <= y0)
    {
        *x = *y = 0;
        *width = *height = 0;
    }
    else
    {
        *x = x0;
        *y = y0;
        *width = x1 - x0;
        *height = y1 - y0;
    }
}

static void test_consistency_10000(void)
{
    int const width = 80, height = 24, iterations = 10000;
    caca_canvas_t *cv = caca_create_canvas(width, height);
    caca_diff_t *dd = caca_create_diff();
    caca_diff_t *full_dd = caca_create_diff();
    struct vt vt;
    size_t diff_total = 0, full_total = 0, empty_frames = 0;
    int iteration, char_mismatches = 0, cursor_mismatches = 0;

    vt_create(&vt, width, height);
    fill_canvas(cv);
    caca_gotoxy(cv, 0, 0);
    check_refresh(&vt, dd, cv, 0, 0, width, height,
                  "consistency initial", NULL);
    caca_clear_dirty_rect_list(cv);

    for(iteration = 0; iteration < iterations; iteration++)
    {
        char *diff_buf = NULL, *full_buf = NULL;
        size_t diff_bytes = 0, full_bytes = 0;
        int edits = 1 + (int)(rng_next() % 5);
        int edit, dx, dy, dw, dh;

        for(edit = 0; edit < edits; edit++)
        {
            int x = (int)(rng_next() % width);
            int y = (int)(rng_next() % height);
            uint32_t ch;

            if(rng_next() & 1)
                ch = 'A' + rng_next() % 26;
            else
                ch = 0x4e00 + rng_next() % 128;

            caca_set_color_ansi(cv,
                (uint8_t)(rng_next() % 16), (uint8_t)(rng_next() % 16));
            caca_set_attr(cv, (uint32_t)(rng_next() & 0xf));
            caca_put_char(cv, x, y, ch);

            if(rng_next() % 3 == 0)
                caca_put_attr(cv, x, y,
                    ((uint32_t)(rng_next() % 16) << 4)
                    | ((uint32_t)(rng_next() % 16) << 18)
                    | (uint32_t)(rng_next() & 0xf));
        }

        caca_gotoxy(cv, (int)(rng_next() % width),
                    (int)(rng_next() % height));
        dirty_bounds(cv, &dx, &dy, &dw, &dh);
        if(caca_diff_refresh(dd, cv, dx, dy, dw, dh,
                             &diff_buf, &diff_bytes) < 0)
        {
            fprintf(stderr, "diff refresh failed at iteration %d\n",
                    iteration);
            failures++;
            free(diff_buf);
            break;
        }

        vt_apply(&vt, diff_buf, diff_bytes);
        {
            int chars_ok = compare_chars(&vt, cv, "consistency");
            int cursor_ok = compare_cursor(&vt, cv, "consistency");

            if(!chars_ok || !cursor_ok)
            {
                if(!chars_ok)
                    char_mismatches++;
                if(!cursor_ok)
                    cursor_mismatches++;
                failures++;
                fprintf(stderr,
                        "consistency stopped at iteration %d "
                        "rect=(%d,%d %dx%d) bytes=%zu\n",
                        iteration, dx, dy, dw, dh, diff_bytes);
                free(diff_buf);
                break;
            }
        }
        if(diff_bytes == 0)
            empty_frames++;
        diff_total += diff_bytes;
        free(diff_buf);

        CHECK(caca_diff_reset(full_dd) == 0);
        if(caca_diff_refresh(full_dd, cv, 0, 0, width, height,
                             &full_buf, &full_bytes) < 0)
        {
            fprintf(stderr, "full refresh failed at iteration %d\n",
                    iteration);
            failures++;
            free(full_buf);
            break;
        }
        full_total += full_bytes;
        free(full_buf);

        caca_clear_dirty_rect_list(cv);
    }

    printf("CONSISTENCY iterations=%d mismatches=%d "
           "cursor_mismatches=%d diff_bytes=%zu full_bytes=%zu "
           "ratio=%.4f empty_frames=%zu\n",
           iterations, char_mismatches, cursor_mismatches,
           diff_total, full_total,
           full_total ? (double)diff_total / (double)full_total : 0.0,
           empty_frames);

    caca_diff_free(full_dd);
    caca_diff_free(dd);
    caca_free_canvas(cv);
    vt_free(&vt);
}

int main(void)
{
    test_basic();
    test_edges();
    test_resize_and_fullwidth();
    test_empty_canvas();
    test_minimal_sequences();
    test_consistency_10000();

    if(failures)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("diffcheck: all tests passed\n");
    return 0;
}

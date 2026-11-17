#include "config.h"

#if !defined(__KERNEL__)

#include <stdlib.h>
#include <string.h>

#include "caca.h"
#include "caca_internals.h"

struct caca_diff
{
    int width, height;
    uint32_t *chars;
    uint32_t *attrs;
    int valid;

    int cursor_x, cursor_y;
    int cursor_known;
    int wrap_pending;

    int term_fg, term_bg, term_flags, term_attr_known;
};

struct output
{
    char *data;
    size_t len, cap;
    int error;
};

struct terminal
{
    int x, y;
    int known;
    int wrap_pending;
    int fg, bg, flags;
    int attr_known;
};

static int output_reserve(struct output *o, size_t extra)
{
    size_t needed, cap;
    char *data;

    if(o->error)
        return -1;

    if(extra > (size_t)-1 - o->len - 1)
    {
        o->error = 1;
        return -1;
    }

    needed = o->len + extra + 1;
    if(needed <= o->cap)
        return 0;

    cap = o->cap ? o->cap : 64;
    while(cap < needed)
    {
        if(cap > (size_t)-1 / 2)
        {
            cap = needed;
            break;
        }
        cap *= 2;
    }

    data = realloc(o->data, cap);
    if(!data)
    {
        o->error = 1;
        return -1;
    }

    o->data = data;
    o->cap = cap;
    return 0;
}

static int output_char(struct output *o, char ch)
{
    if(o->error || output_reserve(o, 1))
        return -1;

    o->data[o->len++] = ch;
    return 0;
}

static int output_data(struct output *o, char const *data, size_t len)
{
    if(o->error || output_reserve(o, len))
        return -1;

    memcpy(o->data + o->len, data, len);
    o->len += len;
    return 0;
}

static int output_string(struct output *o, char const *str)
{
    return output_data(o, str, strlen(str));
}

static int output_uint(struct output *o, unsigned int value)
{
    char buf[16];
    int i = 0;

    do
    {
        buf[i++] = (char)('0' + value % 10);
        value /= 10;
    }
    while(value);

    while(i--)
    {
        if(output_char(o, buf[i]))
            return -1;
    }

    return 0;
}

static int output_absolute(struct terminal *t, struct output *o, int x, int y)
{
    if(output_string(o, "\033["))
        return -1;

    if(x == 0 && y == 0)
    {
        if(output_char(o, 'H'))
            return -1;
    }
    else
    {
        if(output_uint(o, (unsigned int)y + 1)
            || output_char(o, ';')
            || output_uint(o, (unsigned int)x + 1)
            || output_char(o, 'H'))
            return -1;
    }

    t->x = x;
    t->y = y;
    t->known = 1;
    t->wrap_pending = 0;
    return 0;
}

static int output_move(struct terminal *t, struct output *o, int x, int y)
{
    int dx;

    if(!t->known || t->wrap_pending)
        return output_absolute(t, o, x, y);

    if(t->x == x && t->y == y)
        return 0;

    if(t->y != y)
        return output_absolute(t, o, x, y);

    dx = x - t->x;
    if(dx == 0)
        return 0;

    if(x == 0)
    {
        if(output_char(o, '\r'))
            return -1;
    }
    else
    {
        int amount = dx > 0 ? dx : -dx;

        if(output_string(o, "\033["))
            return -1;
        if(amount != 1 && output_uint(o, (unsigned int)amount))
            return -1;
        if(output_char(o, dx > 0 ? 'C' : 'D'))
            return -1;
    }

    t->x = x;
    t->wrap_pending = 0;
    return 0;
}

static void attr_signature(uint32_t attr, int *fg, int *bg, int *flags)
{
    uint8_t ansi_fg = caca_attr_to_ansi_fg(attr);
    uint8_t ansi_bg = caca_attr_to_ansi_bg(attr);

    *fg = ansi_fg < 16 ? ansi_fg : -1;
    *bg = ansi_bg < 16 ? ansi_bg : -1;
    *flags = (int)(attr & (CACA_BOLD | CACA_ITALICS
                            | CACA_UNDERLINE | CACA_BLINK));
}

static int output_sgr(struct terminal *t, struct output *o, uint32_t attr)
{
    int fg, bg, flags, reset, np, i;
    int param[8];

    attr_signature(attr, &fg, &bg, &flags);

    reset = !t->attr_known || (t->flags & ~flags) != 0;
    np = 0;

    if(reset)
        param[np++] = 0;

    if(reset)
    {
        if(flags & CACA_BOLD)
            param[np++] = 1;
        if(flags & CACA_ITALICS)
            param[np++] = 3;
        if(flags & CACA_UNDERLINE)
            param[np++] = 4;
        if(flags & CACA_BLINK)
            param[np++] = 5;
    }
    else
    {
        if((flags & ~t->flags) & CACA_BOLD)
            param[np++] = 1;
        if((flags & ~t->flags) & CACA_ITALICS)
            param[np++] = 3;
        if((flags & ~t->flags) & CACA_UNDERLINE)
            param[np++] = 4;
        if((flags & ~t->flags) & CACA_BLINK)
            param[np++] = 5;
    }

    if((reset || fg != t->fg) && fg >= 0)
        param[np++] = fg < 8 ? 30 + fg : 90 + fg - 8;
    else if(!reset && fg != t->fg && fg < 0)
        param[np++] = 39;

    if((reset || bg != t->bg) && bg >= 0)
        param[np++] = bg < 8 ? 40 + bg : 100 + bg - 8;
    else if(!reset && bg != t->bg && bg < 0)
        param[np++] = 49;

    if(np)
    {
        if(output_string(o, "\033["))
            return -1;

        for(i = 0; i < np; i++)
        {
            if(i && output_char(o, ';'))
                return -1;
            if(output_uint(o, (unsigned int)param[i]))
                return -1;
        }

        if(output_char(o, 'm'))
            return -1;
    }

    t->fg = fg;
    t->bg = bg;
    t->flags = flags;
    t->attr_known = 1;
    return 0;
}

static int output_reset_attr(struct terminal *t, struct output *o)
{
    if(!t->attr_known)
        return 0;

    if(t->fg >= 0 || t->bg >= 0 || t->flags)
    {
        if(output_string(o, "\033[0m"))
            return -1;
    }

    t->fg = t->bg = -1;
    t->flags = 0;
    return 0;
}

static int output_utf32(struct terminal *t, struct output *o,
                        uint32_t ch, int x, int y, int width)
{
    char buf[8];
    size_t bytes;
    int advance;

    advance = caca_utf32_is_fullwidth(ch) ? 2 : 1;
    if(advance == 2 && x + 1 >= width)
    {
        ch = ' ';
        advance = 1;
    }

    bytes = caca_utf32_to_utf8(buf, ch);
    if(output_data(o, buf, bytes))
        return -1;

    if(x + advance >= width)
    {
        t->x = width - 1;
        t->y = y;
        t->wrap_pending = 1;
    }
    else
    {
        t->x = x + advance;
        t->y = y;
        t->wrap_pending = 0;
    }

    return 0;
}

static int cell_equal(caca_diff_t const *dd, uint32_t ch, uint32_t attr,
                      int index)
{
    int oldfg, oldbg, oldflags, fg, bg, flags;

    if(!dd->valid || dd->chars[index] != ch)
        return 0;

    if(ch == CACA_MAGIC_FULLWIDTH)
        return 1;

    attr_signature(dd->attrs[index], &oldfg, &oldbg, &oldflags);
    attr_signature(attr, &fg, &bg, &flags);

    return oldfg == fg && oldbg == bg && oldflags == flags;
}

static int resize_shadow(caca_diff_t *dd, int width, int height)
{
    size_t cells;
    uint32_t *chars = NULL, *attrs = NULL;

    if(width < 0 || height < 0)
    {
        seterrno(EINVAL);
        return -1;
    }

    if(width && height)
    {
        if((size_t)width > (size_t)-1 / sizeof(uint32_t) / (size_t)height)
        {
            seterrno(ENOMEM);
            return -1;
        }

        cells = (size_t)width * (size_t)height;
        chars = malloc(cells * sizeof(uint32_t));
        attrs = malloc(cells * sizeof(uint32_t));
        if(!chars || !attrs)
        {
            free(chars);
            free(attrs);
            seterrno(ENOMEM);
            return -1;
        }
    }

    free(dd->chars);
    free(dd->attrs);
    dd->chars = chars;
    dd->attrs = attrs;
    dd->width = width;
    dd->height = height;
    dd->valid = 0;
    dd->cursor_known = 0;
    dd->wrap_pending = 0;
    return 0;
}

caca_diff_t *caca_create_diff(void)
{
    caca_diff_t *dd = calloc(1, sizeof(*dd));

    if(!dd)
    {
        seterrno(ENOMEM);
        return NULL;
    }

    dd->term_fg = dd->term_bg = -1;
    return dd;
}

int caca_diff_reset(caca_diff_t *dd)
{
    if(!dd)
    {
        seterrno(EINVAL);
        return -1;
    }

    dd->valid = 0;
    dd->cursor_known = 0;
    dd->wrap_pending = 0;
    dd->term_attr_known = 0;
    return 0;
}

int caca_diff_refresh(caca_diff_t *dd, caca_canvas_t const *cv,
                      int x, int y, int width, int height,
                      char **output, size_t *bytes)
{
    struct output o;
    struct terminal t;
    uint32_t const *chars, *attrs;
    unsigned char *left_written = NULL, *right_written = NULL;
    int canvas_width, canvas_height, full, rx, ry, rw, rh, iy;
    int target_x, target_y;
    char *result;

    if(output)
        *output = NULL;
    if(bytes)
        *bytes = 0;

    if(!dd || !cv || !output || !bytes || width < 0 || height < 0)
    {
        seterrno(EINVAL);
        return -1;
    }

    canvas_width = caca_get_canvas_width(cv);
    canvas_height = caca_get_canvas_height(cv);

    full = 0;
    if(dd->width != canvas_width || dd->height != canvas_height)
    {
        if(resize_shadow(dd, canvas_width, canvas_height) < 0)
            return -1;
        full = 1;
    }
    if(!dd->valid)
        full = 1;

    if(full)
    {
        rx = ry = 0;
        rw = canvas_width;
        rh = canvas_height;
    }
    else
    {
        long long x0 = x, y0 = y;
        long long x1 = (long long)x + width;
        long long y1 = (long long)y + height;

        if(x0 < 0)
            x0 = 0;
        if(y0 < 0)
            y0 = 0;
        if(x1 > canvas_width)
            x1 = canvas_width;
        if(y1 > canvas_height)
            y1 = canvas_height;

        if(x1 <= x0 || y1 <= y0)
        {
            rw = rh = 0;
            rx = ry = 0;
        }
        else
        {
            rx = (int)x0;
            ry = (int)y0;
            rw = (int)(x1 - x0);
            rh = (int)(y1 - y0);
        }
    }

    if(rh)
    {
        left_written = calloc((size_t)rh, 1);
        right_written = calloc((size_t)rh, 1);
        if(!left_written || !right_written)
        {
            free(left_written);
            free(right_written);
            seterrno(ENOMEM);
            return -1;
        }
    }

    memset(&o, 0, sizeof(o));
    memset(&t, 0, sizeof(t));
    t.x = dd->cursor_x;
    t.y = dd->cursor_y;
    t.known = dd->cursor_known;
    t.wrap_pending = dd->wrap_pending;
    t.fg = dd->term_fg;
    t.bg = dd->term_bg;
    t.flags = dd->term_flags;
    t.attr_known = dd->term_attr_known;

    chars = caca_get_canvas_chars(cv);
    attrs = caca_get_canvas_attrs(cv);

    for(iy = 0; iy < rh; iy++)
    {
        int row = ry + iy;
        int end = rx + rw;
        int column = rx;

        while(column < end)
        {
            int start, last, cx;

            while(column < end
                   && cell_equal(dd, chars[column + row * canvas_width],
                                 attrs[column + row * canvas_width],
                                 column + row * canvas_width))
                column++;

            if(column == end)
                break;

            start = column;
            while(start > 0
                   && chars[start + row * canvas_width]
                        == CACA_MAGIC_FULLWIDTH)
                start--;

            last = column;
            while(last < end
                   && !cell_equal(dd, chars[last + row * canvas_width],
                                  attrs[last + row * canvas_width],
                                  last + row * canvas_width))
                last++;

            while(last < canvas_width && last > 0
                   && caca_utf32_is_fullwidth(
                        chars[last - 1 + row * canvas_width])
                   && chars[last + row * canvas_width]
                        == CACA_MAGIC_FULLWIDTH)
                last++;

            if(start < rx)
                left_written[iy] = 1;
            if(last > end)
                right_written[iy] = 1;

            if(output_move(&t, &o, start, row))
                goto nomem;

            for(cx = start; cx < last; cx++)
            {
                uint32_t ch = chars[cx + row * canvas_width];

                if(ch == CACA_MAGIC_FULLWIDTH)
                    continue;

                if(output_sgr(&t, &o, attrs[cx + row * canvas_width])
                    || output_utf32(&t, &o, ch, cx, row, canvas_width))
                    goto nomem;
            }

            column = last;
        }
    }

    target_x = caca_wherex(cv);
    target_y = caca_wherey(cv);
    if(canvas_width == 0)
        target_x = 0;
    else if(target_x < 0)
        target_x = 0;
    else if(target_x >= canvas_width)
        target_x = canvas_width - 1;

    if(canvas_height == 0)
        target_y = 0;
    else if(target_y < 0)
        target_y = 0;
    else if(target_y >= canvas_height)
        target_y = canvas_height - 1;

    if(output_reset_attr(&t, &o)
        || (!t.known || t.wrap_pending
            || t.x != target_x || t.y != target_y
            ? output_absolute(&t, &o, target_x, target_y) : 0))
        goto nomem;

    if(o.error || o.len == (size_t)-1)
        goto nomem;

    result = realloc(o.data, o.len + 1);
    if(!result)
        goto nomem;
    result[o.len] = '\0';

    if(full)
    {
        size_t cells = (size_t)canvas_width * (size_t)canvas_height;

        if(cells)
        {
            memcpy(dd->chars, chars, cells * sizeof(uint32_t));
            memcpy(dd->attrs, attrs, cells * sizeof(uint32_t));
        }
        dd->valid = 1;
    }
    else if(rw && rh)
    {
        int row;

        for(row = ry; row < ry + rh; row++)
        {
            memcpy(dd->chars + rx + row * canvas_width,
                   chars + rx + row * canvas_width,
                   (size_t)rw * sizeof(uint32_t));
            memcpy(dd->attrs + rx + row * canvas_width,
                   attrs + rx + row * canvas_width,
                   (size_t)rw * sizeof(uint32_t));
        }

        if(rx > 0)
        {
            for(row = 0; row < rh; row++)
                if(left_written[row])
                {
                    size_t index = (size_t)(rx - 1)
                                    + (size_t)(ry + row) * canvas_width;
                    dd->chars[index] = chars[index];
                    dd->attrs[index] = attrs[index];
                }
        }

        if(rx + rw < canvas_width)
        {
            for(row = 0; row < rh; row++)
                if(right_written[row])
                {
                    size_t index = (size_t)(rx + rw)
                                    + (size_t)(ry + row) * canvas_width;
                    dd->chars[index] = chars[index];
                    dd->attrs[index] = attrs[index];
                }
        }
    }

    dd->cursor_x = t.x;
    dd->cursor_y = t.y;
    dd->cursor_known = 1;
    dd->wrap_pending = 0;
    dd->term_fg = t.fg;
    dd->term_bg = t.bg;
    dd->term_flags = t.flags;
    dd->term_attr_known = t.attr_known;

    free(left_written);
    free(right_written);
    *output = result;
    *bytes = o.len;
    return 0;

nomem:
    free(left_written);
    free(right_written);
    free(o.data);
    seterrno(ENOMEM);
    return -1;
}

int caca_diff_free(caca_diff_t *dd)
{
    if(!dd)
        return 0;

    free(dd->chars);
    free(dd->attrs);
    free(dd);
    return 0;
}

#endif /* !__KERNEL__ */

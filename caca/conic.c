/*
 *  libcaca     Colour ASCII-Art library
 *  Copyright © 2002—2018 Sam Hocevar <sam@hocevar.net>
 *              All Rights Reserved
 *
 *  This library is free software. It comes without any warranty, to
 *  the extent permitted by applicable law. You can redistribute it
 *  and/or modify it under the terms of the Do What the Fuck You Want
 *  to Public License, Version 2, as published by Sam Hocevar. See
 *  http://www.wtfpl.net/ for more details.
 */

/*
 *  This file contains ellipse and circle drawing functions, both filled
 *  and outline.
 */

#include "config.h"

#if !defined(__KERNEL__)
#   include <stdlib.h>
#   include <limits.h>
#endif

#include "caca.h"
#include "caca_internals.h"

#define CACA_MAX_RADIUS 46340

static void ellipsepoints(caca_canvas_t *, int, int, int64_t, int64_t,
                          uint32_t, int);
static int check_radii(int, int);
static int clamp_int64(int64_t);
static void ellipse_line(caca_canvas_t *, int, int, int64_t, int64_t,
                         uint32_t);

/** \brief Draw a circle on the canvas using the given character.
 *
 *  If an error occurs, -1 is returned and \b errno is set accordingly:
 *  - \c EINVAL The radius is negative.
 *  - \c EOVERFLOW The radius is too large to rasterise safely.
 *
 *  \param cv The handle to the libcaca canvas.
 *  \param x Center X coordinate.
 *  \param y Center Y coordinate.
 *  \param r Circle radius.
 *  \param ch UTF-32 character to be used to draw the circle outline.
 *  \return This function always returns 0.
 */
int caca_draw_circle(caca_canvas_t *cv, int x, int y, int r, uint32_t ch)
{
    int test, dx, dy;

    if(check_radii(r, r) < 0)
        return -1;

    /* Optimized Bresenham. Kick ass. */
    for(test = 0, dx = 0, dy = r ; dx <= dy ; dx++)
    {
        ellipsepoints(cv, x, y, dx, dy, ch, 1);
        ellipsepoints(cv, x, y, dy, dx, ch, 1);

        test += test > 0 ? dx - dy-- : dx;
    }

    return 0;
}

/** \brief Fill an ellipse on the canvas using the given character.
 *
 *  If an error occurs, -1 is returned and \b errno is set accordingly:
 *  - \c EINVAL A radius is negative.
 *  - \c EOVERFLOW A radius is too large to rasterise safely.
 *
 *  \param cv The handle to the libcaca canvas.
 *  \param xo Center X coordinate.
 *  \param yo Center Y coordinate.
 *  \param a Ellipse X radius.
 *  \param b Ellipse Y radius.
 *  \param ch UTF-32 character to be used to fill the ellipse.
 *  \return This function always returns 0.
 */
int caca_fill_ellipse(caca_canvas_t *cv, int xo, int yo, int a, int b,
                       uint32_t ch)
{
    int64_t d1, d2, aa, bb, x = 0, y;

    if(check_radii(a, b) < 0)
        return -1;

    aa = (int64_t)a * a;
    bb = (int64_t)b * b;
    y = b;
    d1 = bb - aa * b + aa / 4;

    while(aa * y - aa / 2 > bb * (x + 1))
    {
        if(d1 < 0)
        {
            d1 += bb * (2 * x + 1); /* XXX: "Computer Graphics" has + 3 here. */
        }
        else
        {
            d1 += bb * (2 * x * 1) + aa * (-2 * y + 2);
            ellipse_line(cv, xo, yo, x, y, ch);
            ellipse_line(cv, xo, yo, x, -y, ch);
            y--;
        }
        x++;
    }

    ellipse_line(cv, xo, yo, x, y, ch);
    ellipse_line(cv, xo, yo, x, -y, ch);

    d2 = (int64_t)(bb * (x + 0.5) * (x + 0.5)
                   + aa * (y - 1) * (y - 1) - aa * bb);
    while(y > 0)
    {
        if(d2 < 0)
        {
            d2 += bb * (2 * x + 2) + aa * (-2 * y + 3);
            x++;
        }
        else
        {
            d2 += aa * (-2 * y + 3);
        }

        y--;
        ellipse_line(cv, xo, yo, x, y, ch);
        ellipse_line(cv, xo, yo, x, -y, ch);
    }

    return 0;
}

/** \brief Draw an ellipse on the canvas using the given character.
 *
 *  If an error occurs, -1 is returned and \b errno is set accordingly:
 *  - \c EINVAL A radius is negative.
 *  - \c EOVERFLOW A radius is too large to rasterise safely.
 *
 *  \param cv The handle to the libcaca canvas.
 *  \param xo Center X coordinate.
 *  \param yo Center Y coordinate.
 *  \param a Ellipse X radius.
 *  \param b Ellipse Y radius.
 *  \param ch UTF-32 character to be used to draw the ellipse outline.
 *  \return This function always returns 0.
 */
int caca_draw_ellipse(caca_canvas_t *cv, int xo, int yo, int a, int b,
                       uint32_t ch)
{
    int64_t d1, d2, aa, bb, x = 0, y;

    if(check_radii(a, b) < 0)
        return -1;

    aa = (int64_t)a * a;
    bb = (int64_t)b * b;
    y = b;
    d1 = bb - aa * b + aa / 4;

    ellipsepoints(cv, xo, yo, x, y, ch, 0);

    while(aa * y - aa / 2 > bb * (x + 1))
    {
        if(d1 < 0)
        {
            d1 += bb * (2 * x + 1); /* XXX: "Computer Graphics" has + 3 here. */
        }
        else
        {
            d1 += bb * (2 * x * 1) + aa * (-2 * y + 2);
            y--;
        }
        x++;
        ellipsepoints(cv, xo, yo, x, y, ch, 0);
    }

    d2 = (int64_t)(bb * (x + 0.5) * (x + 0.5)
                   + aa * (y - 1) * (y - 1) - aa * bb);
    while(y > 0)
    {
        if(d2 < 0)
        {
            d2 += bb * (2 * x + 2) + aa * (-2 * y + 3);
            x++;
        }
        else
        {
            d2 += aa * (-2 * y + 3);
        }

        y--;
        ellipsepoints(cv, xo, yo, x, y, ch, 0);
    }

    return 0;
}

/** \brief Draw a thin ellipse on the canvas.
 *
 *  If an error occurs, -1 is returned and \b errno is set accordingly:
 *  - \c EINVAL A radius is negative.
 *  - \c EOVERFLOW A radius is too large to rasterise safely.
 *
 *  \param cv The handle to the libcaca canvas.
 *  \param xo Center X coordinate.
 *  \param yo Center Y coordinate.
 *  \param a Ellipse X radius.
 *  \param b Ellipse Y radius.
 *  \return This function always returns 0.
 */
int caca_draw_thin_ellipse(caca_canvas_t *cv, int xo, int yo, int a, int b)
{
    /* FIXME: this is not correct */
    int64_t d1, d2, aa, bb, x = 0, y;

    if(check_radii(a, b) < 0)
        return -1;

    aa = (int64_t)a * a;
    bb = (int64_t)b * b;
    y = b;
    d1 = bb - aa * b + aa / 4;

    ellipsepoints(cv, xo, yo, x, y, '-', 1);

    while(aa * y - aa / 2 > bb * (x + 1))
    {
        if(d1 < 0)
        {
            d1 += bb * (2 * x + 1); /* XXX: "Computer Graphics" has + 3 here. */
            ellipsepoints(cv, xo, yo, x + 1, y, '0', 1);
        }
        else
        {
            d1 += bb * (2 * x * 1) + aa * (-2 * y + 2);
            y--;
            ellipsepoints(cv, xo, yo, x + 1, y, '1', 1);
        }
        x++;


    }

    d2 = (int64_t)(bb * (x + 0.5) * (x + 0.5)
                   + aa * (y - 1) * (y - 1) - aa * bb);
    while(y > 0)
    {
        if(d2 < 0)
        {
            d2 += bb * (2 * x + 2) + aa * (-2 * y + 3);
            x++;
            ellipsepoints(cv, xo, yo, x , y - 1, '2', 1);
        }
        else
        {
            d2 += aa * (-2 * y + 3);
            ellipsepoints(cv, xo, yo, x , y - 1, '3', 1);
        }

        y--;


    }

    return 0;
}

static int check_radii(int x, int y)
{
    if(x < 0 || y < 0)
    {
        seterrno(EINVAL);
        return -1;
    }

    if(x > CACA_MAX_RADIUS || y > CACA_MAX_RADIUS)
    {
        seterrno(EOVERFLOW);
        return -1;
    }

    return 0;
}

static int clamp_int64(int64_t value)
{
    if(value < INT_MIN)
        return INT_MIN;
    if(value > INT_MAX)
        return INT_MAX;
    return (int)value;
}

static void ellipse_line(caca_canvas_t *cv, int xo, int yo,
                         int64_t x, int64_t y, uint32_t ch)
{
    caca_draw_line(cv, clamp_int64((int64_t)xo - x),
                   clamp_int64((int64_t)yo + y),
                   clamp_int64((int64_t)xo + x),
                   clamp_int64((int64_t)yo + y), ch);
}

static void ellipsepoints(caca_canvas_t *cv, int xo, int yo, int64_t x,
                          int64_t y,
                          uint32_t ch, int thin)
{
    uint8_t b = 0;

    if((int64_t)xo + x >= 0 && (int64_t)xo + x < (int)cv->width)
        b |= 0x1;
    if((int64_t)xo - x >= 0 && (int64_t)xo - x < (int)cv->width)
        b |= 0x2;
    if((int64_t)yo + y >= 0 && (int64_t)yo + y < (int)cv->height)
        b |= 0x4;
    if((int64_t)yo - y >= 0 && (int64_t)yo - y < (int)cv->height)
        b |= 0x8;

    if((b & (0x1|0x4)) == (0x1|0x4)) {
        uint32_t c = ch;

        if(thin) {
            switch(c) {
            case '0':
                c = '-';
                break;
            case '1':
                c = ',';
                break;
            case '2':
                c = '/';
                break;
            case '3':
                c = '|';
                break;
            }

        }
        caca_put_char(cv, xo + x, yo + y, c);
    }
    if((b & (0x2|0x4)) == (0x2|0x4)) {
        uint32_t c = ch;

        if(thin) {
            switch(c) {
            case '0':
                c = '-';
                break;
            case '1':
                c = '.';
                break;
            case '2':
                c = '\\';
                break;
            case '3':
                c = '|';
                break;
            }

        }
        caca_put_char(cv, xo - x, yo + y, c);
    }


    if((b & (0x1|0x8)) == (0x1|0x8)) {
        uint32_t c = ch;

        if(thin) {
            switch(c) {
            case '0':
                c = '-';
                break;
            case '1':
                c = '`';
                break;
            case '2':
                c = '\\';
                break;
            case '3':
                c = '|';
                break;
            }

        }
        caca_put_char(cv, xo + x, yo - y, c);
    }

    if((b & (0x2|0x8)) == (0x2|0x8)) {
        uint32_t c = ch;

        if(thin) {
            switch(c) {
            case '0':
                c = '-';
                break;
            case '1':
                c = '\'';
                break;
            case '2':
                c = '/';
                break;
            case '3':
                c = '|';
                break;
            }

        }
        caca_put_char(cv, xo - x, yo - y, c);
    }
}

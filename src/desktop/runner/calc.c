/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * the calculator mode
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "core/g0wm.h"
#include "core/settings.h"
#include "desktop/runner/calc.h"
#include "desktop/runner/runner.h"
#include "internal.h"

/* +, -, *, /, %, parens, a decimal point and whitespace: nothing here can
 * reach a shell or a file, so a malformed expression can only fail to
 * parse, never do anything. */
struct RunnerCalcState {
    const char* s;
    int depth;
    int failed;
};

/* function declarations */
static double runner_calc_expr(struct RunnerCalcState* c);

/* function implementations */
/* Every recursive call goes through here first, so one depth check bounds
 * atom, term and expr together; runner_buffer is 256 bytes, so even fully
 * parenthesized input like "(((((1)))))" cannot come close to this limit. */
static int runner_calc_enter(struct RunnerCalcState* c)
{
    if (++c->depth > 64) {
        c->failed = 1;
        return 0;
    }
    return 1;
}

static double runner_calc_number(struct RunnerCalcState* c)
{
    double v = 0, frac = 0.1;
    int any = 0;

    while (*c->s >= '0' && *c->s <= '9') {
        v = v * 10 + (*c->s - '0');
        c->s++;
        any = 1;
    }
    if (*c->s == '.') {
        c->s++;
        while (*c->s >= '0' && *c->s <= '9') {
            v += (*c->s - '0') * frac;
            frac *= 0.1;
            c->s++;
            any = 1;
        }
    }
    if (!any)
        c->failed = 1;
    return v;
}

static double runner_calc_atom(struct RunnerCalcState* c)
{
    double v;

    if (!runner_calc_enter(c))
        return 0;
    while (*c->s == ' ' || *c->s == '\t')
        c->s++;
    if (*c->s == '-') {
        c->s++;
        v = -runner_calc_atom(c);
    } else if (*c->s == '+') {
        c->s++;
        v = runner_calc_atom(c);
    } else if (*c->s == '(') {
        c->s++;
        v = runner_calc_expr(c);
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s != ')')
            c->failed = 1;
        else
            c->s++;
    } else {
        v = runner_calc_number(c);
    }
    c->depth--;
    return v;
}

static double runner_calc_term(struct RunnerCalcState* c)
{
    double v = runner_calc_atom(c), rhs;

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '*') {
            c->s++;
            v *= runner_calc_atom(c);
        } else if (*c->s == '/') {
            c->s++;
            rhs = runner_calc_atom(c);
            if (rhs == 0.0) {
                c->failed = 1;
                return 0;
            }
            v /= rhs;
        } else if (*c->s == '%') {
            c->s++;
            rhs = runner_calc_atom(c);
            if (rhs == 0.0) {
                c->failed = 1;
                return 0;
            }
            v = fmod(v, rhs);
        } else {
            break;
        }
    }
    return v;
}

static double runner_calc_expr(struct RunnerCalcState* c)
{
    double v = runner_calc_term(c);

    for (;;) {
        while (*c->s == ' ' || *c->s == '\t')
            c->s++;
        if (*c->s == '+') {
            c->s++;
            v += runner_calc_term(c);
        } else if (*c->s == '-') {
            c->s++;
            v -= runner_calc_term(c);
        } else {
            break;
        }
    }
    return v;
}

/* Only digits, operators, parens and blanks are worth parsing. Outside the
 * calculator mode it also takes an operator, so a bare number is left
 * alone. */
static int runner_is_expr(const char* s, int needop)
{
    int digit = 0, op = !needop;
    char ch;

    for (; *s; s++) {
        ch = *s;
        if (ch >= '0' && ch <= '9')
            digit = 1;
        else if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%')
            op = 1;
        else if (ch != '(' && ch != ')' && ch != '.' && ch != ' ' && ch != '\t')
            return 0;
    }
    return digit && op;
}

/* a double has no more than 17 significant digits */
void runner_calc_format(char* out, size_t n, double v)
{
    snprintf(out, n, "%.*g", (int)MIN(MAX(runner_calc_precision, 1), 17), v);
}

int runner_calc(double* out)
{
    int off, mode = runner_mode(runner_buffer, &off);
    struct RunnerCalcState c = { runner_buffer + off, 0, 0 };
    double v;

    if ((mode != ModeApp && mode != ModeCalc) ||
        !runner_is_expr(c.s, mode == ModeApp))
        return 0;
    v = runner_calc_expr(&c);
    while (*c.s == ' ' || *c.s == '\t')
        c.s++;
    if (c.failed || *c.s != '\0' || !isfinite(v))
        return 0;
    *out = v;
    return 1;
}

/* puts the result in place of the expression, after the prefix if any */
int runner_calc_fill(void)
{
    char res[48];
    double v;
    int off;

    if (!runner_calc(&v))
        return 0;
    runner_mode(runner_buffer, &off);
    runner_calc_format(res, sizeof res, v);
    if (off + strlen(res) >= sizeof runner_buffer)
        return 0;
    memcpy(runner_buffer + off, res, strlen(res) + 1);
    runner_length = (int)strlen(runner_buffer);
    runner_cursor = runner_length;
    return 1;
}

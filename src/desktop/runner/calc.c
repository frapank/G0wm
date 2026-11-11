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
struct CalculatorParser {
    const char* cursor;
    int recursion_depth;
    int has_error;
};

/* function declarations */
static double runner_calc_expr(struct CalculatorParser* state);

/* function implementations */
/* Every recursive call goes through here first, so one depth check bounds
 * atom, term and expr together; runner_buffer is 256 bytes, so even fully
 * parenthesized input like "(((((1)))))" cannot come close to this limit. */
static int runner_calc_enter(struct CalculatorParser* state)
{
    if (++state->recursion_depth > 64) {
        state->has_error = 1;
        return 0;
    }
    return 1;
}

static double runner_calc_number(struct CalculatorParser* state)
{
    double value = 0;
    double frac = 0.1;
    int has_digits = 0;

    while (*state->cursor >= '0' && *state->cursor <= '9') {
        value = value * 10 + (*state->cursor - '0');
        state->cursor++;
        has_digits = 1;
    }
    if (*state->cursor == '.') {
        state->cursor++;
        while (*state->cursor >= '0' && *state->cursor <= '9') {
            value += (*state->cursor - '0') * frac;
            frac *= 0.1;
            state->cursor++;
            has_digits = 1;
        }
    }
    if (!has_digits)
        state->has_error = 1;
    return value;
}

static double runner_calc_atom(struct CalculatorParser* state)
{
    if (!runner_calc_enter(state))
        return 0;
    while (*state->cursor == ' ' || *state->cursor == '\t')
        state->cursor++;
    double value;
    if (*state->cursor == '-') {
        state->cursor++;
        value = -runner_calc_atom(state);
    } else if (*state->cursor == '+') {
        state->cursor++;
        value = runner_calc_atom(state);
    } else if (*state->cursor == '(') {
        state->cursor++;
        value = runner_calc_expr(state);
        while (*state->cursor == ' ' || *state->cursor == '\t')
            state->cursor++;
        if (*state->cursor != ')')
            state->has_error = 1;
        else
            state->cursor++;
    } else {
        value = runner_calc_number(state);
    }
    state->recursion_depth--;
    return value;
}

static double runner_calc_term(struct CalculatorParser* state)
{
    double value = runner_calc_atom(state);

    for (;;) {
        while (*state->cursor == ' ' || *state->cursor == '\t')
            state->cursor++;
        if (*state->cursor == '*') {
            state->cursor++;
            value *= runner_calc_atom(state);
        } else if (*state->cursor == '/') {
            state->cursor++;
            const double rhs = runner_calc_atom(state);
            if (rhs == 0.0) {
                state->has_error = 1;
                return 0;
            }
            value /= rhs;
        } else if (*state->cursor == '%') {
            state->cursor++;
            const double rhs = runner_calc_atom(state);
            if (rhs == 0.0) {
                state->has_error = 1;
                return 0;
            }
            value = fmod(value, rhs);
        } else {
            break;
        }
    }
    return value;
}

static double runner_calc_expr(struct CalculatorParser* state)
{
    double value = runner_calc_term(state);

    for (;;) {
        while (*state->cursor == ' ' || *state->cursor == '\t')
            state->cursor++;
        if (*state->cursor == '+') {
            state->cursor++;
            value += runner_calc_term(state);
        } else if (*state->cursor == '-') {
            state->cursor++;
            value -= runner_calc_term(state);
        } else {
            break;
        }
    }
    return value;
}

/* Only digits, operators, parens and blanks are worth parsing. Outside the
 * calculator mode it also takes an operator, so a bare number is left
 * alone. */
static int runner_is_expr(const char* text, int need_operator)
{
    int digit = 0;
    int has_operator = !need_operator;

    for (; *text; text++) {
        const char character = *text;
        if (character >= '0' && character <= '9')
            digit = 1;
        else if (character == '+' || character == '-' || character == '*' || character == '/' ||
                 character == '%')
            has_operator = 1;
        else if (character != '(' && character != ')' && character != '.' && character != ' ' &&
                 character != '\t')
            return 0;
    }
    return digit && has_operator;
}

/* a double has no more than 17 significant digits */
void runner_calc_format(char* out, size_t size, double value)
{
    snprintf(out, size, "%.*g", (int)MIN(MAX(runner_calc_precision, 1), 17), value);
}

int runner_calc(double* out)
{
    int offset;
    const int mode = runner_mode(runner_buffer, &offset);
    struct CalculatorParser state = { runner_buffer + offset, 0, 0 };

    if ((mode != RUNNER_MODE_APPLICATION && mode != RUNNER_MODE_CALCULATOR) ||
        !runner_is_expr(state.cursor, mode == RUNNER_MODE_APPLICATION))
        return 0;
    double value = runner_calc_expr(&state);
    while (*state.cursor == ' ' || *state.cursor == '\t')
        state.cursor++;
    if (state.has_error || *state.cursor != '\0' || !isfinite(value))
        return 0;
    *out = value;
    return 1;
}

/* puts the result in place of the expression, after the prefix if any */
int runner_calc_fill(void)
{
    double value;

    if (!runner_calc(&value))
        return 0;
    int offset;
    runner_mode(runner_buffer, &offset);
    char res[48];
    runner_calc_format(res, sizeof res, value);
    if (offset + strlen(res) >= sizeof runner_buffer)
        return 0;
    memcpy(runner_buffer + offset, res, strlen(res) + 1);
    runner_length = (int)strlen(runner_buffer);
    runner_cursor = runner_length;
    return 1;
}

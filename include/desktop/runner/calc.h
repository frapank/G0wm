/*
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef G0WM_DESKTOP_RUNNER_CALC_H
#define G0WM_DESKTOP_RUNNER_CALC_H

#include <stddef.h>

int runner_calc(double* out);
void runner_calc_format(char* out, size_t n, double v);

#endif

/*
 * SPDX-FileCopyrightText: 2020 dwl team
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <wlr/types/wlr_xdg_activation_v1.h>

#include "core/activation.h"
#include "g0wm/internal.h"

static char activation_token_tag; /* marks the tokens handed out below */

/* create an xdg-activation token stamped as ours and return its name.
 * Handed to a client when the user acts on its notification, so that
 * the focus request it sends back can be told apart from the others */
const char* activation_token_new(void)
{
    struct wlr_xdg_activation_token_v1* token =
        wlr_xdg_activation_token_v1_create(activation);

    if (!token)
        return nullptr;
    token->data = &activation_token_tag;
    return wlr_xdg_activation_token_v1_get_name(token);
}

/* a window activating with one of these was asked for by the user */
int activation_token_ours(const struct wlr_xdg_activation_token_v1* token)
{
    return token && token->data == &activation_token_tag;
}

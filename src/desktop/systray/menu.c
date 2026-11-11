/*
 * SPDX-FileCopyrightText: janetski
 * SPDX-FileCopyrightText: 2026 frapank
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "desktop/systray/menu.h"

#include <dbus/dbus.h>
#include <wayland-util.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// IWYU pragma: no_include "dbus/dbus-protocol.h"
// IWYU pragma: no_include "dbus/dbus-shared.h"

#define DBUSMENU_IFACE "com.canonical.dbusmenu"
#define LABEL_MAX MENU_LABEL_MAX
/* Entries a level shows, past which the tail is dropped. */
constexpr int ITEMS_MAX = 64;

struct TrayMenu {
    struct wl_array root_items;
    struct wl_array* visible_items; /* the level menu_pick() indexes into */
    DBusConnection* connection;
    char* bus_name;
    char* object_path;
    TrayMenuPresentCallback present_callback;
};

typedef struct {
    char label[LABEL_MAX];
    dbus_int32_t id;
    struct wl_array submenu;
    int has_submenu;
} TrayMenuItem;

static int menu_extract(DBusMessageIter* array_variant, struct wl_array* layout_node);
static int menu_present(TrayMenu* menu, struct wl_array* layout_node);
static void menu_destroy_submenus(struct wl_array* layout_node);

static void menu_item_init(TrayMenuItem* item)
{
    wl_array_init(&item->submenu);
    item->id = -1;
    *item->label = '\0';
    item->has_submenu = 0;
}

static void menu_destroy_submenus(struct wl_array* layout_node)
{
    TrayMenuItem* item;

    wl_array_for_each(item, layout_node) {
        if (item->has_submenu) {
            menu_destroy_submenus(&item->submenu);
            wl_array_release(&item->submenu);
        }
    }
}

static void menu_destroy(TrayMenu* menu)
{
    menu_destroy_submenus(&menu->root_items);
    wl_array_release(&menu->root_items);
    free(menu->bus_name);
    free(menu->object_path);
    free(menu);
}

static void menu_send_clicked(const char* bus_name,
                              const char* object_path,
                              int itemid,
                              DBusConnection* connection)
{
    DBusMessage* message = nullptr;
    DBusMessageIter iter = DBUS_MESSAGE_ITER_INIT_CLOSED;
    DBusMessageIter sub = DBUS_MESSAGE_ITER_INIT_CLOSED;
    const char* data = "";
    const char* eventid = "clicked";
    time_t timestamp;

    timestamp = time(nullptr);

    message = dbus_message_new_method_call(bus_name, object_path, DBUSMENU_IFACE, "Event");
    if (!message)
        goto fail;

    dbus_message_iter_init_append(message, &iter);
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &itemid) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &eventid) ||
        !dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING,
                                          &sub) ||
        !dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &data) ||
        !dbus_message_iter_close_container(&iter, &sub) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &timestamp)) {
        goto fail;
    }

    if (!dbus_connection_send_with_reply(connection, message, nullptr, -1))
        goto fail;

    dbus_message_unref(message);
    return;

fail:
    dbus_message_iter_abandon_container_if_open(&iter, &sub);
    if (message)
        dbus_message_unref(message);
}

/* Hands a level to the presenter. Empty labels are not entries. */
static int menu_present(TrayMenu* menu, struct wl_array* layout_node)
{
    int label_count = 0;

    const char* labels[ITEMS_MAX];
    TrayMenuItem* item;
    wl_array_for_each(item, layout_node) {
        if (!*item->label)
            continue;
        if (label_count == ITEMS_MAX)
            break;
        labels[label_count++] = item->label;
    }

    if (!label_count)
        return -1;

    menu->visible_items = layout_node;
    menu->present_callback(labels, label_count, menu);
    return 0;
}

void menu_pick(TrayMenu* menu, int index)
{
    TrayMenuItem* item;
    int i = 0;

    if (!menu)
        return;
    if (index < 0 || !menu->visible_items)
        goto done;

    wl_array_for_each(item, menu->visible_items) {
        if (!*item->label)
            continue;
        if (i++ != index)
            continue;

        if (item->has_submenu) {
            if (menu_present(menu, &item->submenu) == 0)
                return; /* the presenter answers again, on the submenu */
        } else {
            menu_send_clicked(menu->bus_name, menu->object_path, item->id, menu->connection);
        }
        break;
    }

done:
    menu_destroy(menu);
}

static void menu_item_create(TrayMenuItem* item,
                             dbus_int32_t id,
                             const char* label,
                             int toggle_state,
                             int has_submenu)
{

    if (toggle_state == 0)
        strcpy(item->label, "☐ ");
    else if (toggle_state == 1)
        strcpy(item->label, "✓ ");
    else
        strcpy(item->label, "  ");

    /* Remove "mnemonics" (underscores which mark keyboard shortcuts) */
    char* out = item->label + strlen(item->label);
    for (; *label; label++)
        if (*label != '_')
            *out++ = *label;
    *out = '\0';

    if (has_submenu) {
        item->has_submenu = 1;
        strcat(item->label, " →");
    }

    item->id = id;
}

/**
 * Populates the passed in menuitem based on the dictionary contents.
 *
 * @param[in] dict
 * @param[in] itemid
 * @param[in] mi
 * @param[out] has_submenu
 * @param[out] status <0 on error, 0 on success, >0 if menuitem was skipped
 */
static int menu_read_dict(DBusMessageIter* dict,
                          dbus_int32_t itemid,
                          TrayMenuItem* item,
                          int* has_submenu)
{
    DBusMessageIter member;
    DBusMessageIter val;
    const char* children_display = nullptr;
    const char* label = nullptr;
    const char* toggle_type = nullptr;
    const char* key;
    dbus_bool_t visible = TRUE;
    dbus_bool_t enabled = TRUE;
    dbus_int32_t toggle_state = 1;
    int result;

    do {
        dbus_message_iter_recurse(dict, &member);
        if (dbus_message_iter_get_arg_type(&member) != DBUS_TYPE_STRING) {
            result = -1;
            goto fail;
        }
        dbus_message_iter_get_basic(&member, &key);

        dbus_message_iter_next(&member);
        if (dbus_message_iter_get_arg_type(&member) != DBUS_TYPE_VARIANT) {
            result = -1;
            goto fail;
        }
        dbus_message_iter_recurse(&member, &val);

        if (strcmp(key, "visible") == 0) {
            if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_BOOLEAN) {
                result = -1;
                goto fail;
            }
            dbus_message_iter_get_basic(&val, &visible);

        } else if (strcmp(key, "enabled") == 0) {
            if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_BOOLEAN) {
                result = -1;
                goto fail;
            }
            dbus_message_iter_get_basic(&val, &enabled);

        } else if (strcmp(key, "toggle-type") == 0) {
            if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_STRING) {
                result = -1;
                goto fail;
            }
            dbus_message_iter_get_basic(&val, &toggle_type);

        } else if (strcmp(key, "toggle-state") == 0) {
            if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_INT32) {
                result = -1;
                goto fail;
            }
            dbus_message_iter_get_basic(&val, &toggle_state);

        } else if (strcmp(key, "children-display") == 0) {
            if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_STRING) {
                result = -1;
                goto fail;
            }
            dbus_message_iter_get_basic(&val, &children_display);

            if (strcmp(children_display, "submenu") == 0)
                *has_submenu = 1;

        } else if (strcmp(key, "label") == 0) {
            if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_STRING) {
                result = -1;
                goto fail;
            }
            dbus_message_iter_get_basic(&val, &label);
        }
    } while (dbus_message_iter_next(dict));

    /* Skip hidden etc items */
    if (!label || !visible || !enabled)
        return 1;

    /*
     * Up to 4 bytes for the checkmark prefix ("☐ "/"✓ ") and 4 bytes
     * for the submenu suffix (" →"), both may apply at once, plus
     * 1 for the nul terminator.
     */
    if (strlen(label) + 9 > LABEL_MAX) {
        fprintf(stderr, "Too long menu entry label: %s! Skipping...\n", label);
        return 1;
    }

    if (toggle_type && strcmp(toggle_type, "checkmark") == 0)
        menu_item_create(item, itemid, label, toggle_state, *has_submenu);
    else
        menu_item_create(item, itemid, label, -1, *has_submenu);

    return 0;

fail:
    fprintf(stderr, "Error parsing menu data\n");
    return result;
}

/**
 * Extracts a menuitem from a DBusMessage
 *
 * @param[in] strct
 * @param[in] mi
 * @param[out] status <0 on error, 0 on success, >0 if menuitem was skipped
 */
static int menu_extract_item(DBusMessageIter* structure, TrayMenuItem* item)
{
    DBusMessageIter val;
    DBusMessageIter dict;
    dbus_int32_t itemid;
    int has_submenu = 0;
    int result;

    dbus_message_iter_recurse(structure, &val);
    if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_INT32) {
        result = -1;
        goto fail;
    }
    dbus_message_iter_get_basic(&val, &itemid);

    if (!dbus_message_iter_next(&val) || dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_ARRAY) {
        result = -1;
        goto fail;
    }
    dbus_message_iter_recurse(&val, &dict);
    if (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_DICT_ENTRY) {
        result = -1;
        goto fail;
    }

    result = menu_read_dict(&dict, itemid, item, &has_submenu);
    if (result < 0) {
        goto fail;

    } else if (result == 0 && has_submenu) {
        dbus_message_iter_next(&val);
        if (dbus_message_iter_get_arg_type(&val) != DBUS_TYPE_ARRAY)
            goto fail;
        result = menu_extract(&val, &item->submenu);
        if (result < 0)
            goto fail;
    }

    return result;

fail:
    return result;
}

static int menu_extract(DBusMessageIter* array_variant, struct wl_array* layout_node)
{
    DBusMessageIter variant;
    DBusMessageIter menuitem;
    TrayMenuItem* item;
    int result;

    dbus_message_iter_recurse(array_variant, &variant);
    if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_VARIANT) {
        result = -1;
        goto fail;
    }

    item = wl_array_add(layout_node, sizeof(TrayMenuItem));
    if (!item) {
        result = -ENOMEM;
        goto fail;
    }
    menu_item_init(item);

    do {
        dbus_message_iter_recurse(&variant, &menuitem);
        if (dbus_message_iter_get_arg_type(&menuitem) != DBUS_TYPE_STRUCT) {
            result = -1;
            goto fail;
        }

        result = menu_extract_item(&menuitem, item);
        if (result < 0)
            goto fail;
        else if (result == 0) {
            item = wl_array_add(layout_node, sizeof(TrayMenuItem));
            if (!item) {
                result = -ENOMEM;
                goto fail;
            }
            menu_item_init(item);
        }
        /* r > 0: no action was performed on item */
    } while (dbus_message_iter_next(&variant));

    return 0;

fail:
    return result;
}

static void menu_handle_layout_ready(DBusPendingCall* pending, void* data)
{
    TrayMenu* menu = data;

    DBusMessage* reply = nullptr;
    DBusMessageIter iter;
    DBusMessageIter structure;
    dbus_uint32_t revision;
    int result;

    reply = dbus_pending_call_steal_reply(pending);
    if (!reply || dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
        result = -1;
        goto fail;
    }

    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
        result = -1;
        goto fail;
    }
    dbus_message_iter_get_basic(&iter, &revision);

    if (!dbus_message_iter_next(&iter) ||
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRUCT) {
        result = -1;
        goto fail;
    }
    dbus_message_iter_recurse(&iter, &structure);

    /*
     * id 0 is the root, which contains nothing of interest.
     * Traverse past it.
     */
    if (dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_INT32 ||
        !dbus_message_iter_next(&structure) ||
        dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_ARRAY ||
        !dbus_message_iter_next(&structure) ||
        dbus_message_iter_get_arg_type(&structure) != DBUS_TYPE_ARRAY) {
        result = -1;
        goto fail;
    }

    /* Root traversed over, extract the menu */
    wl_array_init(&menu->root_items);
    result = menu_extract(&structure, &menu->root_items);
    if (result < 0)
        goto fail;

    result = menu_present(menu, &menu->root_items);
    if (result < 0)
        goto fail;

    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
    return;

fail:
    menu_destroy(menu);
    if (reply)
        dbus_message_unref(reply);
    if (pending)
        dbus_pending_call_unref(pending);
}

static int menu_request_layout(TrayMenu* menu)
{
    DBusMessage* message = nullptr;
    DBusMessageIter iter = DBUS_MESSAGE_ITER_INIT_CLOSED;
    DBusMessageIter strings = DBUS_MESSAGE_ITER_INIT_CLOSED;
    DBusPendingCall* pending = nullptr;
    dbus_int32_t parentid;
    dbus_int32_t depth;
    int result;

    parentid = 0;
    depth = -1;

    /* The menu object path reply has not arrived yet. */
    if (!menu->object_path) {
        result = -1;
        goto fail;
    }

    message = dbus_message_new_method_call(menu->bus_name, menu->object_path, DBUSMENU_IFACE,
                                           "GetLayout");
    if (!message) {
        result = -ENOMEM;
        goto fail;
    }

    dbus_message_iter_init_append(message, &iter);
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &parentid) ||
        !dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &depth) ||
        !dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING_AS_STRING,
                                          &strings) ||
        !dbus_message_iter_close_container(&iter, &strings)) {
        result = -ENOMEM;
        goto fail;
    }

    if (!dbus_connection_send_with_reply(menu->connection, message, &pending, -1) ||
        !dbus_pending_call_set_notify(pending, menu_handle_layout_ready, menu, nullptr)) {
        result = -ENOMEM;
        goto fail;
    }

    dbus_message_unref(message);
    return 0;

fail:
    if (pending) {
        dbus_pending_call_cancel(pending);
        dbus_pending_call_unref(pending);
    }
    dbus_message_iter_abandon_container_if_open(&iter, &strings);
    if (message)
        dbus_message_unref(message);
    menu_destroy(menu);
    return result;
}

static void menu_handle_about_to_show(DBusPendingCall* pending, void* data)
{
    TrayMenu* menu = data;

    DBusMessage* reply = nullptr;

    reply = dbus_pending_call_steal_reply(pending);
    if (!reply)
        goto fail;

    dbus_message_unref(reply);
    dbus_pending_call_unref(pending);
    /* destroys the menu itself on failure */
    menu_request_layout(menu);
    return;

fail:
    if (reply)
        dbus_message_unref(reply);
    if (pending)
        dbus_pending_call_unref(pending);
    menu_destroy(menu);
}

void menu_show(DBusConnection* connection,
               const char* bus_name,
               const char* object_path,
               TrayMenuPresentCallback present)
{
    DBusMessage* message = nullptr;
    DBusPendingCall* pending = nullptr;
    TrayMenu* menu = nullptr;
    char* bus_name_copy = nullptr;
    char* object_path_copy = nullptr;
    dbus_int32_t parentid = 0;

    /* the item may expose no menu, or its Menu property is still in flight */
    if (!bus_name || !object_path || !present)
        return;

    menu = calloc(1, sizeof(TrayMenu));
    bus_name_copy = strdup(bus_name);
    object_path_copy = strdup(object_path);
    if (!menu || !bus_name_copy || !object_path_copy)
        goto fail;

    menu->connection = connection;
    menu->bus_name = bus_name_copy;
    menu->object_path = object_path_copy;
    menu->present_callback = present;

    message = dbus_message_new_method_call(menu->bus_name, menu->object_path, DBUSMENU_IFACE,
                                           "AboutToShow");
    if (!message)
        goto fail;

    if (!dbus_message_append_args(message, DBUS_TYPE_INT32, &parentid, DBUS_TYPE_INVALID) ||
        !dbus_connection_send_with_reply(menu->connection, message, &pending, -1) ||
        !dbus_pending_call_set_notify(pending, menu_handle_about_to_show, menu, nullptr)) {
        goto fail;
    }

    dbus_message_unref(message);
    return;

fail:
    if (pending)
        dbus_pending_call_unref(pending);
    if (message)
        dbus_message_unref(message);
    free(bus_name_copy);
    free(object_path_copy);
    free(menu);
}

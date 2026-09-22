#ifndef MENU_H
#define MENU_H

#include <dbus/dbus.h>

/* Longest label a menu entry is shown under, affixes included */
#define MENU_LABEL_MAX 64

typedef struct Menu Menu;

/* Shows a level. The labels only live for the call, and it owes one pick. */
typedef void (*MenuPresentFn)(const char* const* labels, int n, Menu* menu);

/* The menu is built on demand and not kept around */
void menu_show(DBusConnection* conn,
               const char* busname,
               const char* busobj,
               MenuPresentFn present);

/* Takes the entry chosen or opens its submenu, a negative index cancels. */
void menu_pick(Menu* menu, int index);

#endif /* MENU_H */

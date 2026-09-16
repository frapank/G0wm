/* See LICENSE file for copyright and license details. */
#ifndef G0WM_SETTINGS_H
#define G0WM_SETTINGS_H

#include <cJSON.h>

/* the parsed settings.json; NULL when it is missing or broken */
extern cJSON* settings;

void settingsload(void);
int settingswrite(void);

#endif /* G0WM_SETTINGS_H */

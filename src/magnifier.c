/*
 * See LICENSE file for copyright and license details.
 *
 * magnifier: zooms a monitor in around the pointer
 */
#include "g0wm.h"

#define ZOOMMAX 16.0f

/* function implementations */
void magnify(const Arg* arg)
{
    Monitor* m = xytomon(cursor->x, cursor->y);
    float z;

    if (!m && !(m = selmon))
        return;

    /* a step and its opposite cancel out: +0.25 is x1.25, -0.25 is /1.25 */
    if (arg->f == 0)
        z = 1;
    else if (arg->f > 0)
        z = m->zoom * (1 + arg->f);
    else
        z = m->zoom / (1 - arg->f);
    z = z < 1.01f ? 1 : z > ZOOMMAX ? ZOOMMAX : z;
    if (z == m->zoom)
        return;

    m->zoom = z;
    wlr_output_schedule_frame(m->wlr_output);
}

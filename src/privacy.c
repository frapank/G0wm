/*
 * See LICENSE file for copyright and license details.
 *
 * private windows, masked in screen capture but not on screen
 */
#include "g0wm.h"

/* function implementations */

void privacydamage(void)
{
    Monitor* m;

    wl_list_for_each(m, &mons, link)
    {
        wlr_damage_ring_add_whole(&m->scene_output->damage_ring);
        wlr_output_schedule_frame(m->wlr_output);
    }
}

void toggleprivate(const Arg* arg)
{
    Client* sel = focustop(selmon);

    if (!sel)
        return;
    sel->isprivate = !sel->isprivate;
    privacydamage();
    drawbars();
}

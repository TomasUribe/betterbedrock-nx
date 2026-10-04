// SPDX-License-Identifier: GPL-2.0-or-later
// Console-side controls. See nx_control.h. Built only for the Switch.
#ifdef __SWITCH__
#include "nx_control.h"

#include <switch.h>

#define CMD_ATMOSPHERE_RELOAD_HOSTS 65000

uint32_t bl_reload_hosts(void) {
    Service dns;
    Result rc = smGetService(&dns, "sfdnsres");
    if (R_FAILED(rc)) return rc;
    rc = serviceDispatch(&dns, CMD_ATMOSPHERE_RELOAD_HOSTS);
    serviceClose(&dns);
    return rc;
}
#endif

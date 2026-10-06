// SPDX-License-Identifier: GPL-2.0-or-later
// Console-side controls shared by the app and the overlay (libnx only).
#ifndef BBNX_NX_CONTROL_H
#define BBNX_NX_CONTROL_H

#include <stdint.h>

// Asks Atmosphere's dns.mitm to read the hosts file again now (sfdnsres command
// 65000, AtmosphereReloadHostsFile), so changes apply without a restart.
// Returns 0 on success, else the Result.
uint32_t bl_reload_hosts(void);

#endif

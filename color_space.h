#pragma once

/*
 * Session-scoped panel colour-space capability.
 *
 * Both SceLcd and SceOled expose matched Get/SetDisplayColorSpaceMode exports.
 * v1.4 snapshots the firmware-owned mode before changing it, verifies writes
 * with the getter, avoids redundant writes, and restores the original value
 * during teardown.  No registry mutation is involved.
 */
int color_space_apply_config(void);
void color_space_shutdown(void);

int vitabrightColorSpaceGetMode(void);
int vitabrightColorSpaceSetMode(int mode);

#pragma once

extern unsigned int sw_version;

/* g_is_oled: 1 = PCH-1000 OLED, 0 = PCH-2000 LCD. Detected once at boot;
 * userland model arguments are never authoritative. */
extern int g_is_oled;

/* Internal composite reload. Caller must hold the global state lock. Every
 * stage owns and updates only its own diagnostics domain. */
int vitabright_reload_locked(void);

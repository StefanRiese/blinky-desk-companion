/* LVGL configuration for the desktop builds (simulator, screenshots, tests).
 * Only what differs from LVGL's defaults; the device gets its options from sdkconfig.defaults.
 * 32-bit colour here (the device uses RGB565) keeps the PNG and SDL paths simple. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888

#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1

#define LV_USE_OBSERVER 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Set to 1 by CMake when SDL2 is available (buddy_sim). */
#ifndef LV_USE_SDL
#define LV_USE_SDL 0
#endif

#endif /* LV_CONF_H */

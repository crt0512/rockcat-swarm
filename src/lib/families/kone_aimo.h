// what used to be the public api of rossat-swarm: structs and calls with the Kone AIMO Remastered sizes baked in
// nothing outside the library sees this anymore. the Kone AIMO family (and its tests) is built on it, everybody else uses rockcat_profile
#pragma once

#include "rockcat/rockcat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROCKCAT_PROFILE_COUNT   5
#define ROCKCAT_DPI_LEVELS      5
#define ROCKCAT_DPI_MIN         100
#define ROCKCAT_DPI_MAX         16000
#define ROCKCAT_DPI_STEP        50
#define ROCKCAT_LED_COUNT       11
#define ROCKCAT_BUTTON_COUNT    24  /* 12 physical buttons + 12 Easy-Shift[+] layer */

/* Raw report sizes (including report id and, where present, checksum). */
#define ROCKCAT_SETTINGS_SIZE   0x7e /* report 0x06 */
#define ROCKCAT_BUTTONS_SIZE    0x4b /* report 0x07, no checksum */
#define ROCKCAT_MACRO_SIZE      0x7d2 /* report 0x08, sent as two chunks */
#define ROCKCAT_DCU_SIZE        6    /* report 0x0f */

/* LED index layout (verified on hardware, front to back):
 *   0              scroll wheel
 *   1,2,3,4,9      left side           5,6,7,8,10   right side          */
enum {
    ROCKCAT_LED_WHEEL = 0,
    ROCKCAT_LED_LEFT_FRONT = 1,
    ROCKCAT_LED_RIGHT_FRONT = 5
};

typedef struct rockcat_settings {
    uint8_t       profile;           /* 0..4                                   */
    uint8_t       dpi_enabled_mask;  /* +0x04 bit n = DPI level n enabled      */
    uint8_t       dpi_active;        /* +0x05 0..4                             */
    uint16_t      dpi_x[ROCKCAT_DPI_LEVELS]; /* +0x06 real DPI (stored /50)    */
    uint16_t      dpi_y[ROCKCAT_DPI_LEVELS]; /* +0x10                          */
    uint8_t       polling_rate;      /* +0x1a rockcat_polling_rate             */
    uint8_t       angle_snapping;    /* +0x1b 0 = off                          */
    uint8_t       debounce_ms;       /* +0x1d factory 8                        */
    uint16_t      led_enable_mask;   /* +0x1e bit n = LED n enabled (0x7ff)    */
    uint8_t       effect;            /* +0x21 rockcat_effect                   */
    uint8_t       effect_speed;      /* +0x22 factory 6                        */
    uint8_t       brightness;        /* +0x23 0..255                           */
    rockcat_color leds[ROCKCAT_LED_COUNT]; /* +0x3d, 5 bytes each (a,r,g,b,0)  */
    uint8_t       color_theme;       /* +0x74 bit7 = flag, low bits = AIMO theme */
    /* Original report bytes. rockcat_write_settings() starts from this buffer
     * and only overwrites the fields above, so unknown bytes survive a
     * read-modify-write cycle. Zero it to start from scratch. */
    uint8_t       raw[ROCKCAT_SETTINGS_SIZE];
} rockcat_settings;

int rockcat_read_settings(rockcat_device *dev, uint8_t profile, rockcat_settings *out);
int rockcat_write_settings(rockcat_device *dev, const rockcat_settings *in);

/* Raw access; the write variant fixes header and checksum. */
int rockcat_read_settings_raw(rockcat_device *dev, uint8_t profile, uint8_t buf[ROCKCAT_SETTINGS_SIZE]);
int rockcat_write_settings_raw(rockcat_device *dev, uint8_t buf[ROCKCAT_SETTINGS_SIZE]);

/* Pure helpers (no I/O). */
void rockcat_settings_decode(const uint8_t raw[ROCKCAT_SETTINGS_SIZE], rockcat_settings *out);
void rockcat_settings_encode(const rockcat_settings *in, uint8_t raw[ROCKCAT_SETTINGS_SIZE]);
const char *rockcat_led_name(int led);

/* Physical button index; add ROCKCAT_BUTTON_COUNT/2 for the Easy-Shift layer. */
enum {
    ROCKCAT_BTN_LEFT = 0,
    ROCKCAT_BTN_RIGHT = 1,
    ROCKCAT_BTN_MIDDLE = 2,
    ROCKCAT_BTN_TILT_LEFT = 3,
    ROCKCAT_BTN_TILT_RIGHT = 4,
    ROCKCAT_BTN_WHEEL_UP = 5,
    ROCKCAT_BTN_WHEEL_DOWN = 6,
    ROCKCAT_BTN_THUMB_FORWARD = 7,
    ROCKCAT_BTN_THUMB_BACKWARD = 8,
    ROCKCAT_BTN_THUMB_EASYSHIFT = 9,
    ROCKCAT_BTN_DPI_UP = 10,
    ROCKCAT_BTN_DPI_DOWN = 11,
    ROCKCAT_BTN_SHIFT_BASE = 12
};

typedef struct rockcat_buttons {
    uint8_t       profile;
    rockcat_button buttons[ROCKCAT_BUTTON_COUNT];
} rockcat_buttons;

int rockcat_read_buttons(rockcat_device *dev, uint8_t profile, rockcat_buttons *out);
int rockcat_write_buttons(rockcat_device *dev, const rockcat_buttons *in);
void rockcat_buttons_default(uint8_t profile, rockcat_buttons *out);
const char *rockcat_button_name(int index);

int rockcat_read_macro(rockcat_device *dev, uint8_t profile, uint8_t button, rockcat_macro *out);
/* Also remember to assign ROCKCAT_BT_MACRO to the button. */
int rockcat_write_macro(rockcat_device *dev, const rockcat_macro *in);

/* ------------------------------------------------------------------------ */
/* Direct ("software") lighting, as used by Swarm's AIMO engine             */
/* ------------------------------------------------------------------------ */

/* enable != 0 hands the LEDs to the host (report 0x0e); 0 returns control to
 * the profile's lighting effect. */
int rockcat_set_direct_lighting(rockcat_device *dev, int enable);
/* Sends 11 colours (alpha is applied by the library) via report 0x0d. */
int rockcat_set_direct_colors(rockcat_device *dev, const rockcat_color colors[ROCKCAT_LED_COUNT]);

/* Effect the user sees for a profile: the device byte, or - if the device
 * stores 9 - the effect kept on the PC. *by_engine tells which renders it. */
void rockcat_effective_effect(uint16_t pid, const rockcat_settings *s, uint8_t *effect, int *by_engine);
/* Selects an effect for s->profile: updates s->effect (to write with
 * rockcat_write_settings) and the PC-side choice. by_engine is forced on for
 * software-only effects. */
int rockcat_select_effect(uint16_t pid, rockcat_settings *s, uint8_t effect, int by_engine);

/* Pure renderers, usable for previews. t = seconds, activity = 0..1. */
void rockcat_render_frame(const rockcat_settings *s, uint8_t effect, double t, double activity,
                                      rockcat_color out[ROCKCAT_LED_COUNT]);

#ifdef __cplusplus
}
#endif

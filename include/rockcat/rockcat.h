/*
 * rockcat-swarm - open re-implementation of ROCCAT Swarm device control.
 *
 * Public C API. Usable from C and C++ (link against librockcat).
 *
 * Supported devices: ROCCAT Kone AIMO Remastered (USB 1e7d:2e2c).
 *
 * All protocol knowledge comes from reverse engineering ROCCAT Swarm 1.9482
 * (ROCCAT_Swarm_Monitor.exe and the KONE_AIMO 1.0009 module); see
 * docs/PROTOCOL.md for the report layouts.
 *
 * Every function that talks to the device returns ROCKCAT_OK (0) on success or
 * a negative rockcat_error code. Functions are not thread safe for the same
 * rockcat_device handle.
 */
#ifndef ROCKCAT_ROCKCAT_H
#define ROCKCAT_ROCKCAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(ROCKCAT_BUILDING_SHARED)
#  define ROCKCAT_API __declspec(dllexport)
#elif defined(__GNUC__) && defined(ROCKCAT_BUILDING_SHARED)
#  define ROCKCAT_API __attribute__((visibility("default")))
#else
#  define ROCKCAT_API
#endif

#define ROCKCAT_VERSION_MAJOR 2
#define ROCKCAT_VERSION_MINOR 0
#define ROCKCAT_VERSION_PATCH 0

#define ROCKCAT_VID                     0x1e7d
#define ROCKCAT_PID_KONE_AIMO_REMASTERED 0x2e2c

#define ROCKCAT_PROFILE_COUNT   5
#define ROCKCAT_DPI_LEVELS      5
#define ROCKCAT_DPI_MIN         100
#define ROCKCAT_DPI_MAX         16000
#define ROCKCAT_DPI_STEP        50
#define ROCKCAT_LED_COUNT       11
#define ROCKCAT_BUTTON_COUNT    24  /* 12 physical buttons + 12 Easy-Shift[+] layer */
#define ROCKCAT_MACRO_KEYSTROKES 480

/* Raw report sizes (including report id and, where present, checksum). */
#define ROCKCAT_SETTINGS_SIZE   0x7e /* report 0x06 */
#define ROCKCAT_BUTTONS_SIZE    0x4b /* report 0x07, no checksum */
#define ROCKCAT_MACRO_SIZE      0x7d2 /* report 0x08, sent as two chunks */
#define ROCKCAT_DCU_SIZE        6    /* report 0x0f */

typedef enum rockcat_error {
    ROCKCAT_OK              = 0,
    ROCKCAT_ERR_INIT        = -1, /* hidapi could not be initialised        */
    ROCKCAT_ERR_NO_DEVICE   = -2, /* no supported device found / unplugged  */
    ROCKCAT_ERR_ACCESS      = -3, /* hidraw node could not be opened         */
    ROCKCAT_ERR_IO          = -4, /* feature report transfer failed          */
    ROCKCAT_ERR_BUSY        = -5, /* device stayed busy                      */
    ROCKCAT_ERR_REJECTED    = -6, /* device answered "invalid" (status 2)    */
    ROCKCAT_ERR_CHECKSUM    = -7, /* data read back had a bad checksum       */
    ROCKCAT_ERR_ARGUMENT    = -8, /* parameter out of range                  */
    ROCKCAT_ERR_PROTOCOL    = -9, /* unexpected report id / size             */
    ROCKCAT_ERR_UNSUPPORTED = -10 /* this device has no such thing           */
} rockcat_error;

/* ------------------------------------------------------------------------ */
/* Device discovery                                                          */
/* ------------------------------------------------------------------------ */

typedef struct rockcat_device rockcat_device;

typedef struct rockcat_device_info {
    char     path[256];      /* hidraw path of the control interface */
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t release;        /* bcdDevice */
    int      interface_number;
    char     name[64];       /* human readable model name */
    char     serial[64];
} rockcat_device_info;

ROCKCAT_API const char *rockcat_version(void);
ROCKCAT_API const char *rockcat_strerror(int error);

/* Optional: called automatically by rockcat_enumerate()/rockcat_open(). */
ROCKCAT_API int  rockcat_init(void);
ROCKCAT_API void rockcat_exit(void);

/* Fills up to `max` entries, returns the number of supported devices found
 * (may be larger than `max`) or a negative error. */
ROCKCAT_API int rockcat_enumerate(rockcat_device_info *out, int max);

/* Opens a device. `path` may be NULL to open the first supported device. */
ROCKCAT_API int  rockcat_open(const char *path, rockcat_device **out);
ROCKCAT_API void rockcat_close(rockcat_device *dev);
ROCKCAT_API int  rockcat_get_info(rockcat_device *dev, rockcat_device_info *info);

/* Firmware version as reported by USB bcdDevice, e.g. 0x0105 -> "1.05". */
ROCKCAT_API int rockcat_get_firmware_version(rockcat_device *dev, char *buf, size_t len);

/* ------------------------------------------------------------------------ */
/* Profiles (report 0x05)                                                    */
/* ------------------------------------------------------------------------ */

ROCKCAT_API int rockcat_get_active_profile(rockcat_device *dev, uint8_t *profile, uint8_t *count);
ROCKCAT_API int rockcat_set_active_profile(rockcat_device *dev, uint8_t profile);

/* ------------------------------------------------------------------------ */
/* Profile settings (report 0x06, 126 bytes)                                 */
/* ------------------------------------------------------------------------ */

typedef enum rockcat_polling_rate {
    ROCKCAT_POLLING_125HZ  = 0,
    ROCKCAT_POLLING_250HZ  = 1,
    ROCKCAT_POLLING_500HZ  = 2,
    ROCKCAT_POLLING_1000HZ = 3
} rockcat_polling_rate;

/* Lighting effect byte (values from Swarm's CMouse_Effect_Setting table,
 * behaviour verified on a Kone AIMO Remastered with firmware 1.05).
 * "Software" effects are not rendered by the firmware; Swarm - and the
 * rockcat lighting engine below - draw them from the PC. */
typedef enum rockcat_effect {
    ROCKCAT_EFFECT_OFF          = 0, /* firmware */
    ROCKCAT_EFFECT_FULLY_LIT    = 1, /* firmware: static colours */
    ROCKCAT_EFFECT_BLINKING     = 2, /* firmware */
    ROCKCAT_EFFECT_BREATHING    = 3, /* firmware */
    ROCKCAT_EFFECT_HEARTBEAT    = 4, /* software */
    ROCKCAT_EFFECT_COLOR_CYCLE  = 5, /* firmware only, not offered by Swarm */
    ROCKCAT_EFFECT_SNAKE        = 6, /* firmware */
    ROCKCAT_EFFECT_BREATHING_2  = 7, /* software (firmware shows a moving dot) */
    ROCKCAT_EFFECT_HEARTBEAT_2  = 8, /* software (firmware shows a moving dot) */
    ROCKCAT_EFFECT_AIMO         = 9, /* software, uses the colour theme */
    ROCKCAT_EFFECT_COLORWAVE    = 10 /* firmware */
} rockcat_effect;

/* LED index layout (verified on hardware, front to back):
 *   0              scroll wheel
 *   1,2,3,4,9      left side           5,6,7,8,10   right side          */
enum {
    ROCKCAT_LED_WHEEL = 0,
    ROCKCAT_LED_LEFT_FRONT = 1,
    ROCKCAT_LED_RIGHT_FRONT = 5
};

typedef struct rockcat_color {
    uint8_t a, r, g, b;     /* a = per-LED brightness (0..255) */
} rockcat_color;

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

ROCKCAT_API int rockcat_read_settings(rockcat_device *dev, uint8_t profile, rockcat_settings *out);
ROCKCAT_API int rockcat_write_settings(rockcat_device *dev, const rockcat_settings *in);

/* Raw access; the write variant fixes header and checksum. */
ROCKCAT_API int rockcat_read_settings_raw(rockcat_device *dev, uint8_t profile, uint8_t buf[ROCKCAT_SETTINGS_SIZE]);
ROCKCAT_API int rockcat_write_settings_raw(rockcat_device *dev, uint8_t buf[ROCKCAT_SETTINGS_SIZE]);

/* Pure helpers (no I/O). */
ROCKCAT_API void rockcat_settings_decode(const uint8_t raw[ROCKCAT_SETTINGS_SIZE], rockcat_settings *out);
ROCKCAT_API void rockcat_settings_encode(const rockcat_settings *in, uint8_t raw[ROCKCAT_SETTINGS_SIZE]);
ROCKCAT_API int  rockcat_polling_rate_hz(uint8_t polling_rate);
ROCKCAT_API const char *rockcat_effect_name(uint8_t effect);
/* Non-zero if the effect has to be drawn by the host (see rockcat_engine). */
ROCKCAT_API int rockcat_effect_is_software(uint8_t effect);
ROCKCAT_API const char *rockcat_led_name(int led);

/* ------------------------------------------------------------------------ */
/* Button assignment (report 0x07, 75 bytes)                                 */
/* ------------------------------------------------------------------------ */

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

/* Function codes (ROCCAT's common button table, confirmed for the codes used
 * by the Kone AIMO factory layout). */
typedef enum rockcat_button_type {
    ROCKCAT_BT_UNUSED            = 0x00,
    ROCKCAT_BT_CLICK             = 0x01,
    ROCKCAT_BT_MENU              = 0x02,
    ROCKCAT_BT_UNIVERSAL_SCROLL  = 0x03,
    ROCKCAT_BT_DOUBLE_CLICK      = 0x04,
    ROCKCAT_BT_SHORTCUT          = 0x05, /* p1 = modifier mask, p2 = HID key usage */
    ROCKCAT_BT_DISABLED          = 0x06,
    ROCKCAT_BT_BROWSER_FORWARD   = 0x07,
    ROCKCAT_BT_BROWSER_BACKWARD  = 0x08,
    ROCKCAT_BT_TILT_LEFT         = 0x09,
    ROCKCAT_BT_TILT_RIGHT        = 0x0a,
    ROCKCAT_BT_SCROLL_UP         = 0x0d,
    ROCKCAT_BT_SCROLL_DOWN       = 0x0e,
    ROCKCAT_BT_QUICKLAUNCH       = 0x0f,
    ROCKCAT_BT_PROFILE_CYCLE     = 0x10,
    ROCKCAT_BT_PROFILE_UP        = 0x11,
    ROCKCAT_BT_PROFILE_DOWN      = 0x12,
    ROCKCAT_BT_DPI_CYCLE         = 0x14,
    ROCKCAT_BT_DPI_UP            = 0x15,
    ROCKCAT_BT_DPI_DOWN          = 0x16,
    ROCKCAT_BT_WINDOWS_KEY       = 0x1a,
    ROCKCAT_BT_OPEN_DRIVER       = 0x1b,
    ROCKCAT_BT_OPEN_PLAYER       = 0x20,
    ROCKCAT_BT_PREV_TRACK        = 0x21,
    ROCKCAT_BT_NEXT_TRACK        = 0x22,
    ROCKCAT_BT_PLAY_PAUSE        = 0x23,
    ROCKCAT_BT_STOP              = 0x24,
    ROCKCAT_BT_MUTE              = 0x25,
    ROCKCAT_BT_VOLUME_UP         = 0x26,
    ROCKCAT_BT_VOLUME_DOWN       = 0x27,
    ROCKCAT_BT_MACRO             = 0x30,
    ROCKCAT_BT_TIMER             = 0x31,
    ROCKCAT_BT_TIMER_STOP        = 0x32,
    ROCKCAT_BT_EASYAIM_1         = 0x33, /* temporarily switch to DPI level 1..5 */
    ROCKCAT_BT_EASYAIM_2         = 0x34,
    ROCKCAT_BT_EASYAIM_3         = 0x35,
    ROCKCAT_BT_EASYAIM_4         = 0x36,
    ROCKCAT_BT_EASYAIM_5         = 0x37,
    ROCKCAT_BT_EASYSHIFT         = 0x41,
    ROCKCAT_BT_EASYSHIFT_OTHER   = 0x51,
    ROCKCAT_BT_EASYSHIFT_LOCK    = 0x52,
    ROCKCAT_BT_EASYSHIFT_ALL     = 0x53,
    ROCKCAT_BT_EASYSHIFT_LOCK_ALL = 0x54,
    /* 0x8c..0xbb are "host actions": the firmware only reports them to the
     * PC (input report [03 00 f2 N 00] on press, N = type - 0x8b). The
     * rockcat lighting engine implements these two: */
    ROCKCAT_BT_BRIGHTNESS_UP     = 0x8c, /* one brightness step up, needs the engine  */
    ROCKCAT_BT_BRIGHTNESS_DOWN   = 0x8d /* one brightness step down, needs the engine */
} rockcat_button_type;

/* Brightness levels used by the step buttons: 10 steps, logarithmic (each
 * ~1.85x the previous, from the dimmest value 1) so dark and bright ends
 * get equally many steps. */
#define ROCKCAT_BRIGHTNESS_LEVELS 11
#define ROCKCAT_BRIGHTNESS_LEVEL_TABLE {0, 1, 2, 3, 6, 11, 21, 39, 72, 134, 255}

/* Modifier bits for ROCKCAT_BT_SHORTCUT param1 (verified on hardware). */
enum {
    ROCKCAT_MOD_SHIFT = 0x02,
    ROCKCAT_MOD_CTRL  = 0x04,
    ROCKCAT_MOD_ALT   = 0x08,
    ROCKCAT_MOD_WIN   = 0x10
};

typedef struct rockcat_button {
    uint8_t type;   /* rockcat_button_type */
    uint8_t param1;
    uint8_t param2;
} rockcat_button;

typedef struct rockcat_buttons {
    uint8_t       profile;
    rockcat_button buttons[ROCKCAT_BUTTON_COUNT];
} rockcat_buttons;

ROCKCAT_API int rockcat_read_buttons(rockcat_device *dev, uint8_t profile, rockcat_buttons *out);
ROCKCAT_API int rockcat_write_buttons(rockcat_device *dev, const rockcat_buttons *in);
ROCKCAT_API void rockcat_buttons_default(uint8_t profile, rockcat_buttons *out);
ROCKCAT_API const char *rockcat_button_name(int index);
ROCKCAT_API const char *rockcat_button_type_name(uint8_t type);
/* Parses a symbolic type name ("click", "dpi-up", ...) or a number. Returns
 * the type or -1. */
ROCKCAT_API int rockcat_button_type_from_string(const char *s);
/* Iterate the known function codes: for i in [0, count) -> type code. */
ROCKCAT_API int rockcat_button_type_count(void);
ROCKCAT_API int rockcat_button_type_at(int i);
/* CLI spelling of a type ("dpi-up"), or NULL if unknown. */
ROCKCAT_API const char *rockcat_button_type_key(uint8_t type);
/* "ctrl+shift+c", "alt+f4", "volumeup"... -> modifier mask + HID key usage.
 * Returns ROCKCAT_OK or ROCKCAT_ERR_ARGUMENT. */
ROCKCAT_API int rockcat_parse_shortcut(const char *text, uint8_t *modifiers, uint8_t *key);
/* Inverse of rockcat_parse_shortcut; writes e.g. "ctrl+c". */
ROCKCAT_API void rockcat_format_shortcut(uint8_t modifiers, uint8_t key, char *buf, size_t len);

/* ------------------------------------------------------------------------ */
/* Macros (report 0x08, 2002 bytes split into two 1026 byte transfers)       */
/* ------------------------------------------------------------------------ */

enum { ROCKCAT_KEY_PRESS = 1, ROCKCAT_KEY_RELEASE = 2 };

typedef enum rockcat_macro_mode {
    ROCKCAT_MACRO_ON_PRESS      = 0, /* play once per press             */
    ROCKCAT_MACRO_WHILE_PRESSED = 1, /* repeat while the button is held */
    ROCKCAT_MACRO_INFINITE      = 2 /* toggle endless repeat            */
} rockcat_macro_mode;

typedef struct rockcat_keystroke {
    uint8_t  key;       /* HID keyboard usage                                */
    uint8_t  action;    /* ROCKCAT_KEY_PRESS / ROCKCAT_KEY_RELEASE            */
    uint16_t delay_ms;  /* wait after this event                              */
} rockcat_keystroke;

typedef struct rockcat_macro {
    uint8_t  profile;
    uint8_t  button;
    uint8_t  loop;                 /* repeat count                            */
    uint8_t  mode;                 /* rockcat_macro_mode                      */
    char     set_name[41];         /* 40 bytes on the device                  */
    char     name[33];             /* 32 bytes on the device                  */
    uint16_t count;
    rockcat_keystroke keys[ROCKCAT_MACRO_KEYSTROKES];
} rockcat_macro;

ROCKCAT_API int rockcat_read_macro(rockcat_device *dev, uint8_t profile, uint8_t button, rockcat_macro *out);
/* Also remember to assign ROCKCAT_BT_MACRO to the button. */
ROCKCAT_API int rockcat_write_macro(rockcat_device *dev, const rockcat_macro *in);

/* ------------------------------------------------------------------------ */
/* Direct ("software") lighting, as used by Swarm's AIMO engine             */
/* ------------------------------------------------------------------------ */

/* enable != 0 hands the LEDs to the host (report 0x0e); 0 returns control to
 * the profile's lighting effect. */
ROCKCAT_API int rockcat_set_direct_lighting(rockcat_device *dev, int enable);
/* Sends 11 colours (alpha is applied by the library) via report 0x0d. */
ROCKCAT_API int rockcat_set_direct_colors(rockcat_device *dev, const rockcat_color colors[ROCKCAT_LED_COUNT]);

/* ------------------------------------------------------------------------ */
/* Sensor calibration (TDCU, report 0x0f)                                    */
/* ------------------------------------------------------------------------ */

typedef struct rockcat_tdcu {
    uint8_t  mode;   /* 0 = factory, 2 = custom (calibrated)                  */
    uint8_t  state;
    uint16_t value;  /* calibration result reported by the sensor             */
} rockcat_tdcu;

ROCKCAT_API int rockcat_read_tdcu(rockcat_device *dev, rockcat_tdcu *out);
/* Starts calibration: keep moving the mouse on the mousepad until
 * rockcat_calibration_poll() reports completion, then call
 * rockcat_calibration_finish(dev, 1) to store the result (0 discards it). */
ROCKCAT_API int rockcat_calibration_start(rockcat_device *dev);
/* *finished = 1 once the sensor is done (the device leaves its busy state). */
ROCKCAT_API int rockcat_calibration_poll(rockcat_device *dev, int *finished);
ROCKCAT_API int rockcat_calibration_finish(rockcat_device *dev, int save);

/* ------------------------------------------------------------------------ */
/* Lighting engine: AIMO and other software effects                          */
/* ------------------------------------------------------------------------ */

#define ROCKCAT_AIMO_THEMES 10

typedef enum rockcat_engine_mode {
    ROCKCAT_ENGINE_AUTO      = 0, /* follow the active profile's effect       */
    ROCKCAT_ENGINE_AIMO      = 1, /* force AIMO with the profile's theme      */
    ROCKCAT_ENGINE_HEARTBEAT = 2 /* force heartbeat with the profile colours  */
} rockcat_engine_mode;

typedef struct rockcat_engine rockcat_engine;

typedef struct rockcat_engine_status {
    int     running;          /* worker thread alive                         */
    int     rendering;        /* currently driving the LEDs                  */
    int     shift_held;       /* Easy-Shift[+] is pressed                    */
    uint8_t profile;          /* active profile it follows, 0xff = unknown   */
    uint8_t effect;           /* effect being rendered                       */
    int     last_error;       /* last rockcat_error from the worker          */
    rockcat_color leds[ROCKCAT_LED_COUNT]; /* last frame sent                */
} rockcat_engine_status;

/* Starts a background thread with its own device handle. Only one engine per
 * user can run at a time (lock in $XDG_RUNTIME_DIR); a second start returns
 * ROCKCAT_ERR_BUSY. Software effects are rendered at ~30 fps and react to
 * mouse activity; firmware effects are left to the mouse. */
ROCKCAT_API int  rockcat_engine_start(const char *path, rockcat_engine_mode mode, rockcat_engine **out);
/* Stops the thread and hands the LEDs back to the firmware. */
ROCKCAT_API void rockcat_engine_stop(rockcat_engine *engine);
ROCKCAT_API void rockcat_engine_get_status(rockcat_engine *engine, rockcat_engine_status *out);
/* Ask the engine to re-read the active profile (e.g. after writing settings). */
ROCKCAT_API void rockcat_engine_reload(rockcat_engine *engine);

/* Easy-Shift[+] lighting: while the Easy-Shift button is held, the engine
 * shows this instead of the profile's lighting. Stored on the PC in
 * $XDG_CONFIG_HOME/rockcat/<pid>.ini (the mouse has no slot for it). */
typedef enum rockcat_shift_style {
    ROCKCAT_SHIFT_SOLID = 0,
    ROCKCAT_SHIFT_PULSE = 1
} rockcat_shift_style;

typedef struct rockcat_shift_lighting {
    int           enabled;
    uint8_t       style;  /* rockcat_shift_style                         */
    rockcat_color color;  /* a = brightness, independent of the profile  */
} rockcat_shift_lighting;

/* Effects rendered by rockcat. The firmware paints its own, hard-coded
 * Easy-Shift[+] indicator (#059cfa, see docs/PROTOCOL.md) for every effect
 * except AIMO (9). So when a profile is "rendered by rockcat", the mouse
 * stores effect 9 and the effect the user picked is kept on the PC; the
 * engine draws it and can show a custom Easy-Shift[+] colour.
 * rockcat_engine_effect_load() returns ROCKCAT_EFFECT_AIMO when nothing is
 * stored. Software-only effects (rockcat_effect_is_software) are always
 * rendered this way. */
ROCKCAT_API int rockcat_engine_effect_load(uint16_t pid, uint8_t profile, uint8_t *effect);
ROCKCAT_API int rockcat_engine_effect_save(uint16_t pid, uint8_t profile, uint8_t effect);
/* Effect the user sees for a profile: the device byte, or - if the device
 * stores 9 - the effect kept on the PC. *by_engine tells which renders it. */
ROCKCAT_API void rockcat_effective_effect(uint16_t pid, const rockcat_settings *s, uint8_t *effect, int *by_engine);
/* Selects an effect for s->profile: updates s->effect (to write with
 * rockcat_write_settings) and the PC-side choice. by_engine is forced on for
 * software-only effects. */
ROCKCAT_API int rockcat_select_effect(uint16_t pid, rockcat_settings *s, uint8_t effect, int by_engine);

/* where the host side settings live. moves an old ~/.config/rossat over the first time its called */
ROCKCAT_API const char *rockcat_config_dir(void);
ROCKCAT_API int rockcat_shift_lighting_load(uint16_t pid, uint8_t profile, rockcat_shift_lighting *out);
ROCKCAT_API int rockcat_shift_lighting_save(uint16_t pid, uint8_t profile, const rockcat_shift_lighting *in);

/* Pure renderers, usable for previews. t = seconds, activity = 0..1. */
ROCKCAT_API void rockcat_render_frame(const rockcat_settings *s, uint8_t effect, double t, double activity,
                                      rockcat_color out[ROCKCAT_LED_COUNT]);
ROCKCAT_API void rockcat_aimo_theme(int theme, rockcat_color out[3]);

/* ------------------------------------------------------------------------ */
/* Devices and profiles, the api everything is moving to                     */
/* ------------------------------------------------------------------------ */

/* nothing below knows how many leds or buttons "a device" has, you ask. the structs further up with
 * the sizes baked in are the old Kone AIMO only api and go away once cli, gui and engine stopped using them */

typedef enum rockcat_kind { ROCKCAT_KIND_MOUSE, ROCKCAT_KIND_KEYBOARD, ROCKCAT_KIND_PAD, ROCKCAT_KIND_HEADSET } rockcat_kind;

/* how much we trust the support for a device. REPLAY = only ever ran against a recording or a simulated device */
typedef enum rockcat_tested { ROCKCAT_TESTED_NO, ROCKCAT_TESTED_REPLAY, ROCKCAT_TESTED_HARDWARE } rockcat_tested;

enum { /* rockcat_caps.features */
    ROCKCAT_HAS_DPI            = 1 << 0,
    ROCKCAT_HAS_DPI_XY         = 1 << 1, /* x and y can differ */
    ROCKCAT_HAS_MACROS         = 1 << 2,
    ROCKCAT_HAS_EASYSHIFT      = 1 << 3,
    ROCKCAT_HAS_CALIBRATION    = 1 << 4,
    ROCKCAT_HAS_DIRECT_LIGHT   = 1 << 5,
    ROCKCAT_HAS_LED_BRIGHTNESS = 1 << 6, /* the a in rockcat_color means something per led */
    ROCKCAT_HAS_RGB            = 1 << 7,
    ROCKCAT_HAS_DEBOUNCE       = 1 << 8,
    ROCKCAT_HAS_ANGLE_SNAP     = 1 << 9
};

typedef struct rockcat_caps {
    rockcat_kind   kind;
    const char    *family;          /* "kone-aimo", ... */
    rockcat_tested tested;
    const char    *tested_firmware; /* what it was tested with, "" if never */
    uint32_t       features;
    int profile_count;
    int input_count;                /* buttons on a mouse, keys on a keyboard */
    int layer_count;                /* 2 on a mouse: normal and Easy-Shift[+] */
    int led_count;
    int dpi_levels, dpi_min, dpi_max, dpi_step;
    int polling_hz[8];              /* zero terminated */
    uint32_t effects;               /* bit n = rockcat_effect n can be selected */
} rockcat_caps;

/* NULL for a device that was opened raw and isnt in the table */
ROCKCAT_API const rockcat_caps *rockcat_get_caps(rockcat_device *dev);
/* walks the table of supported devices. ROCKCAT_ERR_ARGUMENT once i is past the end */
ROCKCAT_API int rockcat_model_at(int i, uint16_t *product_id, const char **name, const rockcat_caps **caps);
/* what to print next to input / led / layer n. "?" when out of range */
ROCKCAT_API const char *rockcat_input_label(rockcat_device *dev, int input);
ROCKCAT_API const char *rockcat_led_label(rockcat_device *dev, int led);
ROCKCAT_API const char *rockcat_layer_label(rockcat_device *dev, int layer);

/* A profile is read from the device, changed through the calls below and written back. It keeps the raw reports
 * inside, so whatever we dont understand yet goes back to the device exactly as it came. */
typedef struct rockcat_profile rockcat_profile;

typedef enum rockcat_setting {
    ROCKCAT_SET_DPI_ACTIVE,     /* which dpi level is selected, from 0 */
    ROCKCAT_SET_POLLING_HZ,     /* one of caps->polling_hz */
    ROCKCAT_SET_ANGLE_SNAPPING, /* 0 / 1 */
    ROCKCAT_SET_DEBOUNCE_MS,
    ROCKCAT_SET_EFFECT,         /* rockcat_effect as stored on the device, see rockcat_profile_get_effect */
    ROCKCAT_SET_EFFECT_SPEED,
    ROCKCAT_SET_BRIGHTNESS,     /* 0-255 */
    ROCKCAT_SET_COLOR_THEME     /* AIMO theme, from 0 */
} rockcat_setting;

/* what a button or key does, same three bytes the devices use: type (rockcat_button_type) and two parameters */
typedef rockcat_button rockcat_assignment;

ROCKCAT_API int  rockcat_profile_read(rockcat_device *dev, int index, rockcat_profile **out);
/* only sends the reports that were changed since the read */
ROCKCAT_API int  rockcat_profile_write(rockcat_device *dev, rockcat_profile *p);
ROCKCAT_API void rockcat_profile_free(rockcat_profile *p);
ROCKCAT_API int  rockcat_profile_index(const rockcat_profile *p);
/* for restoring a backup into another slot. marks everything as changed */
ROCKCAT_API int  rockcat_profile_set_index(rockcat_profile *p, int index);

/* Every get/set returns ROCKCAT_ERR_UNSUPPORTED when the device has no such thing and ROCKCAT_ERR_ARGUMENT when
 * the index or value is out of range. Setters only change the object, nothing is sent before rockcat_profile_write. */
ROCKCAT_API int rockcat_profile_get_dpi(const rockcat_profile *p, int level, int *x, int *y, int *enabled);
/* x and y get rounded down to caps->dpi_step */
ROCKCAT_API int rockcat_profile_set_dpi(rockcat_profile *p, int level, int x, int y, int enabled);
ROCKCAT_API int rockcat_profile_get_int(const rockcat_profile *p, rockcat_setting which, int *value);
ROCKCAT_API int rockcat_profile_set_int(rockcat_profile *p, rockcat_setting which, int value);
ROCKCAT_API int rockcat_profile_get_led(const rockcat_profile *p, int led, rockcat_color *c, int *enabled);
ROCKCAT_API int rockcat_profile_set_led(rockcat_profile *p, int led, rockcat_color c, int enabled);
ROCKCAT_API int rockcat_profile_get_input(const rockcat_profile *p, int layer, int input, rockcat_assignment *a);
ROCKCAT_API int rockcat_profile_set_input(rockcat_profile *p, int layer, int input, const rockcat_assignment *a);
/* macros are big and slow to fetch, so they are not read along with the profile. get asks the device every time,
 * set parks the macro in the object until the next rockcat_profile_write. neither touches the assignment of the input */
ROCKCAT_API int rockcat_profile_get_macro(rockcat_device *dev, const rockcat_profile *p, int layer, int input, rockcat_macro *m);
ROCKCAT_API int rockcat_profile_set_macro(rockcat_profile *p, int layer, int input, const rockcat_macro *m);

/* The effect the user picked, which is not always the byte on the device: effects the firmware cant draw are stored
 * as AIMO on the device and remembered on the pc (the engine draws them). by_engine says who renders it. */
ROCKCAT_API int rockcat_profile_get_effect(const rockcat_profile *p, int *effect, int *by_engine);
ROCKCAT_API int rockcat_profile_select_effect(rockcat_profile *p, int effect, int by_engine);

/* a profile as bytes, for backups. returns the size needed, writes only if len is big enough */
ROCKCAT_API int rockcat_profile_export(const rockcat_profile *p, uint8_t *buf, size_t len);
/* the other way around. refuses a backup that was made from a different model */
ROCKCAT_API int rockcat_profile_import(rockcat_device *dev, const uint8_t *buf, size_t len, rockcat_profile **out);

/* host controlled lighting: begin takes the leds away from the firmware, frame wants exactly caps->led_count colours */
ROCKCAT_API int rockcat_direct_begin(rockcat_device *dev);
ROCKCAT_API int rockcat_direct_frame(rockcat_device *dev, const rockcat_color *colors, int count);
ROCKCAT_API int rockcat_direct_end(rockcat_device *dev);

/* ------------------------------------------------------------------------ */
/* Low level                                                                 */
/* ------------------------------------------------------------------------ */

/* Raw feature report helpers. buf[0] must be the report id. */
ROCKCAT_API int rockcat_feature_get(rockcat_device *dev, uint8_t *buf, size_t len);
/* Sends a report and waits for the device's ready status (report 0x04). */
ROCKCAT_API int rockcat_feature_set(rockcat_device *dev, const uint8_t *buf, size_t len);
ROCKCAT_API uint16_t rockcat_checksum(const uint8_t *buf, size_t len);

/* the "i know what im doing" drawer. works on any ROCCAT hidraw node, including devices the rest of the api has never heard of */
ROCKCAT_API int rockcat_enumerate_raw(rockcat_device_info *out, int max);
ROCKCAT_API int rockcat_open_raw(const char *path, rockcat_device **out);
/* report ids the node declares in its descriptor. returns how many there are, may be more than max */
ROCKCAT_API int rockcat_report_ids(const char *path, uint8_t *ids, int max);
/* same as rockcat_feature_set but doesnt ask the device if it liked it */
ROCKCAT_API int rockcat_feature_send(rockcat_device *dev, const uint8_t *buf, size_t len);
/* same as rockcat_feature_get but returns how many bytes came back and doesnt care what they are */
ROCKCAT_API int rockcat_feature_read(rockcat_device *dev, uint8_t *buf, size_t len);
/* one input report from the interrupt endpoint. 0 = nothing came within timeout_ms */
ROCKCAT_API int rockcat_read(rockcat_device *dev, uint8_t *buf, size_t len, int timeout_ms);

/* Recordings. Two environment variables, looked at whenever a device gets opened:
 *   ROCKCAT_CAPTURE=file  everything that goes over the wire is appended to file
 *   ROCKCAT_REPLAY=file   no hardware gets touched, file plays the device
 * rockcat_replay_verdict() is ROCKCAT_OK as long as the code sent exactly what the recording has, with finished != 0
 * the recording also has to be used up. always ROCKCAT_OK on a real device. format: docs/CAPTURES.md */
ROCKCAT_API int rockcat_replay_verdict(rockcat_device *dev, int finished);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROCKCAT_ROCKCAT_H */

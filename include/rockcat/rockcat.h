/*
 * rockcat-swarm - open re-implementation of ROCCAT Swarm device control.
 *
 * Public C API. Usable from C and C++ (link against librockcat).
 *
 * Supported devices: ROCCAT Kone AIMO Remastered (USB 1e7d:2e2c).
 *
 * All protocol knowledge comes from reverse engineering ROCCAT Swarm 1.9482
 * (ROCCAT_Swarm_Monitor.exe and the KONE_AIMO 1.0009 module); see
 * docs/protocol/kone-aimo.md for the report layouts.
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
#define ROCKCAT_PID_KOVA_2016_GRAY       0x2cee
#define ROCKCAT_PID_KOVA_2016_WHITE      0x2cef
#define ROCKCAT_PID_KOVA_2016_BLACK      0x2cf0
#define ROCKCAT_PID_RYOS_MK_FX           0x2fda

/* most keystrokes one macro can hold */
#define ROCKCAT_MACRO_KEYSTROKES 480

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

/* Pretend devices, for trying things without the hardware. the gui has them in its debug menu (--debug).
 * a plugged one shows up in rockcat_enumerate like a real one, its path starts with "sim:" */
ROCKCAT_API const char *rockcat_sim_name(int i);  /* NULL past the end */
ROCKCAT_API const char *rockcat_sim_label(int i); /* the model it pretends to be */
ROCKCAT_API int rockcat_sim_plug(const char *name, int plugged);
ROCKCAT_API int rockcat_sim_plugged(const char *name);
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
/* Shared value types: polling rate, effects, colours                        */
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
    ROCKCAT_EFFECT_COLORWAVE    = 10, /* firmware */
    /* keyboards (Ryos MK FX). the first one the keyboard does by itself, the other five Swarm drew from the pc.
     * a profile can say it uses one of those, rockcat does not draw them yet */
    ROCKCAT_EFFECT_FADE         = 11, /* firmware: a key lights up when pressed and fades out */
    ROCKCAT_EFFECT_EQUALIZER    = 12, /* not drawn by anybody yet */
    ROCKCAT_EFFECT_RIPPLE       = 13, /* software: rings run outwards from every key press */
    ROCKCAT_EFFECT_WAVE         = 14, /* software: colours wander across the board */
    ROCKCAT_EFFECT_HEATMAP      = 15, /* software: the keys you use most glow hottest */
    ROCKCAT_EFFECT_GAME_PRESET  = 16, /* not drawn by anybody yet */
    /* effects ROCCAT never shipped, all software */
    ROCKCAT_EFFECT_FIRE         = 17, /* flames rise from the bottom row */
    ROCKCAT_EFFECT_DRIP         = 18, /* every key press lets a drop fall, they collect at the bottom */
    ROCKCAT_EFFECT_COLOR_FLOW   = 19, /* the whole device wanders smoothly through the colours */
    /* more firmware ones of the older keyboards */
    ROCKCAT_EFFECT_IMPULSE      = 20  /* firmware (Sova): the light flares up and dies down again, over and over */
} rockcat_effect;

/* Effects the pc draws have settings of their own (how fast, which colours, ...). They live on the pc, per device
 * and profile, next to the other host side settings, and take effect at once: no profile write needed.
 * A colour is 0xRRGGBB, a toggle 0 / 1, a choice the index into its list. */
typedef enum rockcat_option_kind { ROCKCAT_OPTION_RANGE, ROCKCAT_OPTION_COLOR, ROCKCAT_OPTION_TOGGLE, ROCKCAT_OPTION_CHOICE } rockcat_option_kind;

typedef struct rockcat_effect_option {
    const char *key;     /* name in the config file and for get / set */
    const char *label;   /* for showing to people */
    rockcat_option_kind kind;
    int min, max;        /* RANGE */
    int fallback;        /* what it is until somebody sets it */
    const char *choices; /* CHOICE: "one|two|three" */
} rockcat_effect_option;

/* What a keyboard does when nobody types is its own set of options, the same for every effect the pc draws on a profile.
 * Ask for them with this in place of an effect. */
#define ROCKCAT_EFFECT_OPTIONS_IDLE (-1)

/* points *out at the options of that effect and returns how many there are, 0 for an effect without any */
ROCKCAT_API int rockcat_effect_options(int effect, const rockcat_effect_option **out);
ROCKCAT_API int rockcat_effect_option_get(uint16_t pid, uint8_t profile, int effect, const char *key);
ROCKCAT_API int rockcat_effect_option_set(uint16_t pid, uint8_t profile, int effect, const char *key, int value);

/* lets a running engine play the idle animation once and wake up again, to see what it looks like */
ROCKCAT_API int rockcat_effect_idle_preview(uint16_t pid, uint8_t profile);

typedef struct rockcat_color {
    uint8_t a, r, g, b;     /* a = per-LED brightness (0..255) */
} rockcat_color;

/* The Easy-Shift light layer of a keyboard: other colours while Easy-Shift is held, drawn by the engine (the
 * keyboard has no such thing itself). Kept on the pc per profile. count colours, one per led, a = 0 is a dark key.
 * get fills what was never set with dark keys and enabled = 0 */
ROCKCAT_API int rockcat_shift_layer_get(uint16_t pid, uint8_t profile, int *enabled, rockcat_color *colors, int count);
ROCKCAT_API int rockcat_shift_layer_set(uint16_t pid, uint8_t profile, int enabled, const rockcat_color *colors, int count);
/* which effect the Easy-Shift layer shows: ROCKCAT_EFFECT_FULLY_LIT (its colours, standing still, the default) or one of
 * the effects the pc draws. its settings go through rockcat_effect_option_get/set with profile | ROCKCAT_SHIFT_LAYER */
#define ROCKCAT_SHIFT_LAYER 0x80
ROCKCAT_API int rockcat_shift_layer_effect(uint16_t pid, uint8_t profile);
ROCKCAT_API int rockcat_shift_layer_set_effect(uint16_t pid, uint8_t profile, int effect);

ROCKCAT_API int  rockcat_polling_rate_hz(uint8_t polling_rate);
ROCKCAT_API const char *rockcat_effect_name(uint8_t effect);
/* Non-zero if the effect has to be drawn by the host (see rockcat_engine). This is the answer for the Kone AIMO,
 * a keyboard does heartbeat by itself: ask rockcat_caps.software_effects when you have a device at hand. */
ROCKCAT_API int rockcat_effect_is_software(uint8_t effect);

/* ------------------------------------------------------------------------ */
/* What a button can do                                                      */
/* ------------------------------------------------------------------------ */

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
    /* seen on the Kova 2016 (names from roccat-tools). nobody has tried these on a Kone AIMO */
    ROCKCAT_BT_EASYWHEEL_PROFILE = 0x43, /* hold and turn the wheel: switch profile */
    ROCKCAT_BT_EASYWHEEL_DPI     = 0x44,
    ROCKCAT_BT_EASYWHEEL_VOLUME  = 0x45,
    ROCKCAT_BT_EASYWHEEL_ALT_TAB = 0x46,
    ROCKCAT_BT_HOME              = 0x62,
    ROCKCAT_BT_END               = 0x63,
    ROCKCAT_BT_PAGE_UP           = 0x64,
    ROCKCAT_BT_PAGE_DOWN         = 0x65,
    ROCKCAT_BT_LEFT_CTRL         = 0x66,
    ROCKCAT_BT_LEFT_SHIFT        = 0x67,
    ROCKCAT_BT_LEFT_ALT          = 0x68,
    ROCKCAT_BT_EASYAIM_CUSTOM    = 0x7c, /* p1 = dpi / 50 */
    ROCKCAT_BT_PROFILE_1         = 0xbf, /* jump straight to profile 1..5 */
    ROCKCAT_BT_PROFILE_2         = 0xc0,
    ROCKCAT_BT_PROFILE_3         = 0xc1,
    ROCKCAT_BT_PROFILE_4         = 0xc2,
    ROCKCAT_BT_PROFILE_5         = 0xc3,
    ROCKCAT_BT_EASYAIM_200       = 0xd0, /* what roccat-tools calls it, factory setting of the left forward button */
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
/* Macros                                                                    */
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
} rockcat_engine_status;

/* Starts a background thread with its own device handle. Only one engine per
 * user and device can run at a time (lock in $XDG_RUNTIME_DIR); a second start returns
 * ROCKCAT_ERR_BUSY. path = NULL takes the first mouse the pc can draw on. For a keyboard
 * give its path: it gets an engine of its own that draws wave, ripple and heatmap. Software effects are rendered at ~30 fps and react to
 * mouse activity; firmware effects are left to the mouse. */
ROCKCAT_API int  rockcat_engine_start(const char *path, rockcat_engine_mode mode, rockcat_engine **out);
/* Stops the thread and hands the LEDs back to the firmware. */
ROCKCAT_API void rockcat_engine_stop(rockcat_engine *engine);
ROCKCAT_API void rockcat_engine_get_status(rockcat_engine *engine, rockcat_engine_status *out);
/* the frame the engine sent last, for any number of leds. writes up to max colours, returns how many leds there are */
ROCKCAT_API int rockcat_engine_get_frame(rockcat_engine *engine, rockcat_color *out, int max);
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
 * Easy-Shift[+] indicator (#059cfa, see docs/protocol/kone-aimo.md) for every effect
 * except AIMO (9). So when a profile is "rendered by rockcat", the mouse
 * stores effect 9 and the effect the user picked is kept on the PC; the
 * engine draws it and can show a custom Easy-Shift[+] colour.
 * rockcat_engine_effect_load() returns ROCKCAT_EFFECT_AIMO when nothing is
 * stored. Software-only effects (rockcat_effect_is_software) are always
 * rendered this way. */
ROCKCAT_API int rockcat_engine_effect_load(uint16_t pid, uint8_t profile, uint8_t *effect);
ROCKCAT_API int rockcat_engine_effect_save(uint16_t pid, uint8_t profile, uint8_t effect);
/* where the host side settings live. moves an old ~/.config/rossat over the first time its called */
ROCKCAT_API const char *rockcat_config_dir(void);
ROCKCAT_API int rockcat_shift_lighting_load(uint16_t pid, uint8_t profile, rockcat_shift_lighting *out);
ROCKCAT_API int rockcat_shift_lighting_save(uint16_t pid, uint8_t profile, const rockcat_shift_lighting *in);

ROCKCAT_API void rockcat_aimo_theme(int theme, rockcat_color out[3]);

/* ------------------------------------------------------------------------ */
/* Devices and profiles                                                      */
/* ------------------------------------------------------------------------ */

/* nothing in this header knows how many leds or buttons "a device" has, you ask */

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
    int layer_count;                /* 2 on a mouse: normal and Easy-Shift[+]. 3 on a Ryos, Fn comes on top. not every
                                     * key has something on every layer, then get/set answer ROCKCAT_ERR_UNSUPPORTED */
    int led_count;
    int dpi_levels, dpi_min, dpi_max, dpi_step;
    int polling_hz[8];              /* zero terminated */
    uint32_t effects;               /* bit n = rockcat_effect n can be selected */
    uint32_t software_effects;      /* bit n = the device cant do effect n by itself, the pc has to draw it */
    int effect_speed_min, effect_speed_max; /* what ROCKCAT_SET_EFFECT_SPEED takes */
    int carrier_effect;             /* what the device itself has stored while the pc draws an effect it has no number
                                     * for, -1 if there is no such thing. rockcat_profile_get_effect() hides this */
} rockcat_caps;

/* NULL for a device that was opened raw and isnt in the table */
ROCKCAT_API const rockcat_caps *rockcat_get_caps(rockcat_device *dev);
/* walks the table of supported devices. ROCKCAT_ERR_ARGUMENT once i is past the end */
ROCKCAT_API int rockcat_model_at(int i, uint16_t *product_id, const char **name, const rockcat_caps **caps);
/* what to print next to input / led / layer n. takes caps and not a device so a gui can label things before anything is plugged in. "?" when out of range */
ROCKCAT_API const char *rockcat_input_label(const rockcat_caps *caps, int input);
ROCKCAT_API const char *rockcat_led_label(const rockcat_caps *caps, int led);
ROCKCAT_API const char *rockcat_layer_label(const rockcat_caps *caps, int layer);
/* what the type byte of an assignment means on this device, as text. on a mouse thats rockcat_button_type_name(),
 * on a keyboard the type is a key (its HID usage) or one of rockcat_key_function. "?" for a code nobody knows */
ROCKCAT_API const char *rockcat_assignment_name(const rockcat_caps *caps, uint8_t type);
/* 1 when the type byte is a function of the device, 0 when it is a key (a keyboard key set to be another key) */
ROCKCAT_API int rockcat_assignment_is_function(const rockcat_caps *caps, uint8_t type);

/* Keyboards: which keys there are and where they sit, per regional layout (ROCCAT numbers them: 0 US, 1 DE, 3 FR, 5 nordic, 14 CZ).
 * A key sits at x/y/w/h in a coordinate space of rockcat_keyboard_size(), stretch your picture of the keyboard onto that.
 * led is the keys index in a lighting frame, and at the same time its input number for rockcat_profile_get_input().
 * A key whose led is not below caps->led_count has no light of its own (the thumb keys of the Ryos).
 * All three calls answer 0 / "?" for a device that is not a keyboard. */
typedef struct rockcat_key {
    const char *name;
    int led;
    int x, y, w, h;
} rockcat_key;

/* writes up to max layout ids, returns how many there are */
ROCKCAT_API int rockcat_keyboard_regions(const rockcat_caps *caps, int *ids, int max);
ROCKCAT_API const char *rockcat_keyboard_region_name(const rockcat_caps *caps, int region);
ROCKCAT_API int rockcat_keyboard_size(const rockcat_caps *caps, int *width, int *height);
/* points *keys at the key table of that layout and returns its length */
ROCKCAT_API int rockcat_keyboard_keys(const rockcat_caps *caps, int region, const rockcat_key **keys);

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
    ROCKCAT_SET_COLOR_THEME,    /* AIMO theme, from 0 */
    ROCKCAT_SET_COLOR_FLOW,     /* 0 / 1, the lights wander through the colours by themselves */
    ROCKCAT_SET_LEFT_HANDED,    /* 0 / 1, swaps what the device thinks is the left side */
    /* keyboards: what the lights do when nobody has touched a key for a while */
    ROCKCAT_SET_IDLE_MINUTES,   /* after how many minutes, 0 = never */
    ROCKCAT_SET_IDLE_BRIGHTNESS, /* 0-255, how bright they stay */
    ROCKCAT_SET_IDLE_STYLE,     /* rockcat_idle_style */
    ROCKCAT_SET_MACRO_FEEDBACK  /* 0 / 1, the lights show a macro running */
} rockcat_setting;

typedef enum rockcat_idle_style {
    ROCKCAT_IDLE_DIM         = 0, /* go down to the idle brightness */
    ROCKCAT_IDLE_STARLIT_SKY = 1, /* single keys twinkle */
    ROCKCAT_IDLE_FALL_ASLEEP = 3  /* the lights go out one after the other */
} rockcat_idle_style;

/* What a key of a keyboard can be set to, next to "be this other key" (then the type is the HID usage of that key,
 * 0x04 = A and so on). Codes and names are the ones roccat-tools uses for the Ryos. Only the first block is checked
 * against a real keyboard (they are in the factory layout of a Ryos MK FX). */
typedef enum rockcat_key_function {
    ROCKCAT_KF_DISABLED       = 0x00,
    ROCKCAT_KF_M1             = 0xaa, /* the macro keys as they come: M1..M5 */
    ROCKCAT_KF_M2             = 0xab,
    ROCKCAT_KF_M3             = 0xac,
    ROCKCAT_KF_M4             = 0xad,
    ROCKCAT_KF_M5             = 0xae,
    ROCKCAT_KF_PROFILE_UP     = 0xb0,
    ROCKCAT_KF_PROFILE_DOWN   = 0xb1,
    ROCKCAT_KF_OPEN_PLAYER    = 0xcb,
    ROCKCAT_KF_APP_EMAIL      = 0xc0,
    ROCKCAT_KF_SHIFT_1        = 0xd3, /* shift + 1..5 and ctrl + 1..5, what M1..M5 do out of the box */
    ROCKCAT_KF_CTRL_1         = 0xd8,
    ROCKCAT_KF_OPEN_DRIVER    = 0xdd,
    ROCKCAT_KF_FN             = 0xf1,
    ROCKCAT_KF_EASYSHIFT      = 0xff,
    /* not seen on a keyboard here yet */
    ROCKCAT_KF_PROFILE_CYCLE  = 0xb2,
    ROCKCAT_KF_PROFILE_1      = 0xb3, /* ..5 = 0xb7 */
    ROCKCAT_KF_SHORTCUT       = 0xb8, /* p1 = modifiers (bit 0 ctrl, 1 shift, 2 alt, 3 win), p2 = HID key usage */
    ROCKCAT_KF_QUICKLAUNCH    = 0xb9,
    ROCKCAT_KF_TIMER          = 0xbc,
    ROCKCAT_KF_TIMER_STOP     = 0xbd,
    ROCKCAT_KF_MACRO          = 0xbe,
    ROCKCAT_KF_PLAY_PAUSE     = 0xcc,
    ROCKCAT_KF_NEXT_TRACK     = 0xcd,
    ROCKCAT_KF_PREV_TRACK     = 0xce,
    ROCKCAT_KF_STOP           = 0xcf,
    ROCKCAT_KF_VOLUME_UP      = 0xd0,
    ROCKCAT_KF_VOLUME_DOWN    = 0xd1,
    ROCKCAT_KF_MUTE           = 0xd2
} rockcat_key_function;

/* what a button or key does, same three bytes the devices use: type (rockcat_button_type) and two parameters */
typedef rockcat_button rockcat_assignment;

ROCKCAT_API int  rockcat_profile_read(rockcat_device *dev, int index, rockcat_profile **out);
/* only sends the reports that were changed since the read */
ROCKCAT_API int  rockcat_profile_write(rockcat_device *dev, rockcat_profile *p);
ROCKCAT_API void rockcat_profile_free(rockcat_profile *p);
/* a copy to play with, for previews of changes the user hasnt applied yet. NULL if p is */
ROCKCAT_API rockcat_profile *rockcat_profile_clone(const rockcat_profile *p);
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
/* what an input can be given on a layer, as bits. 0 = it has nothing on that layer */
enum {
    ROCKCAT_ROOM_KEY    = 1, /* a type without parameters: another key, a function of the device */
    ROCKCAT_ROOM_PARAMS = 2, /* a type with its two parameters too: a shortcut, a profile number, ... */
    ROCKCAT_ROOM_MACRO  = 4  /* a macro of its own */
};
ROCKCAT_API int rockcat_profile_input_room(const rockcat_profile *p, int layer, int input);
/* what the input does out of the box on that layer. ROCKCAT_ERR_UNSUPPORTED when the library doesnt know */
ROCKCAT_API int rockcat_profile_factory_input(const rockcat_profile *p, int layer, int input, rockcat_assignment *a);
/* puts every input of every layer back to what the device shipped with */
ROCKCAT_API int rockcat_profile_reset_inputs(rockcat_profile *p);
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

/* every profile of the device in one file. returns how many went in, or an error (ROCKCAT_ERR_ACCESS: cant write the file) */
ROCKCAT_API int rockcat_backup_save(rockcat_device *dev, const char *path);
/* puts a backup back onto the device: all profiles (profile -1) or just that one. returns how many got written.
 * ROCKCAT_ERR_ACCESS: cant read the file, ROCKCAT_ERR_PROTOCOL: not a backup or cut off, ROCKCAT_ERR_UNSUPPORTED: made
 * with another model. nothing gets written unless every profile in the file fits this device */
ROCKCAT_API int rockcat_backup_restore(rockcat_device *dev, const char *path, int profile);

/* what an effect looks like at time t (seconds) with the colours of this profile, for previews. nothing is sent anywhere.
 * activity is 0..1, how busy the user is. writes up to count colours, returns how many leds there are */
ROCKCAT_API int rockcat_profile_render(const rockcat_profile *p, int effect, double t, double activity, rockcat_color *out, int count);

/* things the device tells us without being asked */
typedef enum rockcat_event_type {
    ROCKCAT_EVENT_NONE,        /* a report came in that means nothing to us */
    ROCKCAT_EVENT_ACTIVITY,    /* its being used. value = the buttons that are down, as a bitmask */
    ROCKCAT_EVENT_EASYSHIFT,   /* Easy-Shift[+] went down or up, see pressed */
    ROCKCAT_EVENT_HOST_ACTION, /* a button that is assigned to something only the pc can do. value = which one, from 1
                                * (on a Ryos: 0x10 = a key set to "open driver") */
    ROCKCAT_EVENT_CHANGED,     /* profile or dpi got switched on the device itself, whatever you read before may be stale */
    ROCKCAT_EVENT_KEY          /* keyboards, only while the active profile has an effect the pc draws: a key went down or up.
                                * value = the key (its led number), see pressed */
} rockcat_event_type;

typedef struct rockcat_event {
    rockcat_event_type type;
    int value;
    int pressed;
} rockcat_event;

/* waits up to timeout_ms for the device to say something. 1 = got a report (ev->type may still be NONE), 0 = nothing came */
ROCKCAT_API int rockcat_poll_event(rockcat_device *dev, rockcat_event *ev, int timeout_ms);

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
 *   ROCKCAT_SIM=kone-aimo-remastered  no hardware either, a simulated mouse that answers whatever you ask (and forgets it on exit)
 * rockcat_replay_verdict() is ROCKCAT_OK as long as the code sent exactly what the recording has, with finished != 0
 * the recording also has to be used up. always ROCKCAT_OK on a real device. format: docs/CAPTURES.md */
ROCKCAT_API int rockcat_replay_verdict(rockcat_device *dev, int finished);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ROCKCAT_ROCKCAT_H */

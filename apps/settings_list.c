/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 Jonathan Gordon
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "config.h"
#include <stdbool.h>
#include <string.h>
#include "ata.h"
#include "lang.h"
#include "talk.h"
#include "lcd.h"
#include "button.h"
#include "backlight.h"
#include "settings.h"
#include "settings_list.h"
#include "sound.h"
#include "dsp.h"
#include "debug.h"
#include "mpeg.h"
#include "audio.h"
#include "power.h"
#include "powermgmt.h"
#include "kernel.h"
#include "lcd-remote.h"
#include "list.h"
#include "rbunicode.h"
#ifdef HAVE_LCD_BITMAP
#include "peakmeter.h"
#endif
#include "menus/eq_menu.h"
#if CONFIG_TUNER
#include "radio.h"
#endif


#define NVRAM(bytes) (bytes<<F_NVRAM_MASK_SHIFT)
/** NOTE: NVRAM_CONFIG_VERSION is in settings_list.h
     and you may need to update it if you edit this file */

#define UNUSED {.RESERVED=NULL}
#define INT(a) {.int_ = a}
#define UINT(a) {.uint_ = a}
#define BOOL(a) {.bool_ = a}
#define CHARPTR(a) {.charptr = a}
#define UCHARPTR(a) {.ucharptr = a}
#define FUNCTYPE(a) {.func = a}
#define NODEFAULT INT(0)

/* in all the following macros the args are:
    - flags: bitwise | or the F_ bits in settings_list.h
    - var: pointer to the variable being changed (usually in global_settings)
    - lang_ig: LANG_* id to display in menus and setting screens for the settings
    - default: the default value for the variable, set if settings are reset
    - name: the name of the setting in config files
    - cfg_vals: comma seperated list of legal values in cfg files.
                NULL if a number is written to the file instead.
    - cb: the callback used by the setting screen.
*/

/* Use for int settings which use the set_sound() function to set them */
#define SOUND_SETTING(flags,var,lang_id,name,setting)                   \
            {flags|F_T_INT|F_T_SOUND, &global_settings.var,             \
                lang_id, NODEFAULT,name,NULL,                           \
                {.sound_setting=(struct sound_setting[]){{setting}}} }

/* Use for bool variables which don't use LANG_SET_BOOL_YES and LANG_SET_BOOL_NO,
                or dont save as "off" or "on" in the cfg */
#define BOOL_SETTING(flags,var,lang_id,default,name,cfgvals,yes,no,cb)      \
            {flags|F_BOOL_SETTING, &global_settings.var,                    \
                lang_id, BOOL(default),name,cfgvals,                        \
                {.bool_setting=(struct bool_setting[]){{cb,yes,no}}} }
                
/* bool setting which does use LANG_YES and _NO and save as "off,on" */
#define OFFON_SETTING(flags,var,lang_id,default,name,cb)                    \
            {flags|F_BOOL_SETTING, &global_settings.var,                    \
                lang_id, BOOL(default),name,off_on,                         \
                {.bool_setting=(struct bool_setting[])                      \
                {{cb,LANG_SET_BOOL_YES,LANG_SET_BOOL_NO}}} }

/* int variable which is NOT saved to .cfg files,
    (Use NVRAM() in the flags to save to the nvram (or nvram.bin file) */
#define SYSTEM_SETTING(flags,var,default) \
            {flags|F_T_INT, &global_status.var,-1, INT(default),    \
                NULL, NULL, UNUSED}

/* setting which stores as a filename in the .cfgvals
    prefix: The absolute path to not save in the variable, e.g /.rockbox/wps_file
    suffx:  The file extention (usually...) e.g .wps_file   */
#define FILENAME_SETTING(flags,var,name,default,prefix,suffix,len)  \
            {flags|F_T_UCHARPTR, &global_settings.var,-1,           \
                CHARPTR(default),name,NULL,                         \
                {.filename_setting=                                        \
                    (struct filename_setting[]){{prefix,suffix,len}}} }

/*  Used for settings which use the set_option() setting screen.
    the ... arg is a list of pointers to strings to display in the setting screen.
    These can either be literal strings, or ID2P(LANG_*) */
#define CHOICE_SETTING(flags,var,lang_id,default,name,cfg_vals,cb,count,...)  \
            {flags|F_CHOICE_SETTING|F_T_INT,  &global_settings.var, lang_id,   \
                INT(default), name, cfg_vals,                     \
                {.choice_setting = (struct choice_setting[]){    \
                    {cb, count, {.desc = (unsigned char*[]){__VA_ARGS__}}}}}}
                    
/* Similar to above, except the strings to display are taken from cfg_vals,
   the ... arg is a list of ID's to talk for the strings... can use TALK_ID()'s */
#define STRINGCHOICE_SETTING(flags,var,lang_id,default,name,cfg_vals,cb,count,...)  \
            {flags|F_CHOICE_SETTING|F_T_INT|F_CHOICETALKS,                  \
                &global_settings.var, lang_id,                  \
                INT(default), name, cfg_vals,                     \
                {.choice_setting = (struct choice_setting[]){    \
                    {cb, count, {.talks = (int[]){__VA_ARGS__}}}}}}
                    
/*  for settings which use the set_int() setting screen.
    unit is the UNIT_ define to display/talk.
    the first one saves a string to the config file,
    the second one saves the variable value to the config file */    
#define INT_SETTING_W_CFGVALS(flags, var, lang_id, default, name, cfg_vals, \
                    unit, min, max, step, formatter, get_talk_id, cb)   \
            {flags|F_INT_SETTING|F_T_INT, &global_settings.var,         \
                lang_id, INT(default), name, cfg_vals,                  \
                 {.int_setting = (struct int_setting[]){                \
                    {cb, unit, min, max, step, formatter, get_talk_id}}}}
#define INT_SETTING(flags, var, lang_id, default, name,                      \
                    unit, min, max, step, formatter, get_talk_id, cb)       \
            {flags|F_INT_SETTING|F_T_INT, &global_settings.var,         \
                lang_id, INT(default), name, NULL,                  \
                 {.int_setting = (struct int_setting[]){                \
                    {cb, unit, min, max, step, formatter, get_talk_id}}}}



/* some sets of values which are used more than once, to save memory */
static const char off_on[] = "off,on";
static const char off_on_ask[] = "off,on,ask";
static const char off_number_spell[] = "off,number,spell";
#ifdef HAVE_LCD_BITMAP
static const char graphic_numeric[] = "graphic,numeric";
#endif

#ifdef HAVE_RECORDING
/* keep synchronous to trig_durations and
   trigger_times in settings_apply_trigger */
static const char trig_durations_conf [] =
                  "0s,1s,2s,5s,10s,15s,20s,25s,30s,1min,2min,5min,10min";
/* these should be in the config.h files */
#if CONFIG_CODEC == MAS3587F
# define DEFAULT_REC_MIC_GAIN 8
# define DEFAULT_REC_LEFT_GAIN 2
# define DEFAULT_REC_RIGHT_GAIN 2
#elif CONFIG_CODEC == SWCODEC
# ifdef HAVE_UDA1380
#  define DEFAULT_REC_MIC_GAIN 16
#  define DEFAULT_REC_LEFT_GAIN 0
#  define DEFAULT_REC_RIGHT_GAIN 0
# elif defined(HAVE_TLV320)
#  define DEFAULT_REC_MIC_GAIN 0
#  define DEFAULT_REC_LEFT_GAIN 0
#  define DEFAULT_REC_RIGHT_GAIN 0
# elif defined(HAVE_WM8975)
#  define DEFAULT_REC_MIC_GAIN 16
#  define DEFAULT_REC_LEFT_GAIN 0
#  define DEFAULT_REC_RIGHT_GAIN 0
# elif defined(HAVE_WM8758)
#  define DEFAULT_REC_MIC_GAIN 16
#  define DEFAULT_REC_LEFT_GAIN 0
#  define DEFAULT_REC_RIGHT_GAIN 0
# elif defined(HAVE_WM8731)
#  define DEFAULT_REC_MIC_GAIN 16
#  define DEFAULT_REC_LEFT_GAIN 0
#  define DEFAULT_REC_RIGHT_GAIN 0
# endif
#endif

static long rectime_getlang(int value)
{
    if (value == 0)
        return LANG_OFF;
    return TALK_ID(value, UNIT_SEC);
}
static void rectime_formatter(char *buffer, size_t buffer_size, 
        int val, const char *unit)
{
    (void)unit;
    if (val == 0)
        strcpy(buffer, str(LANG_OFF));
    else
        snprintf(buffer, buffer_size, "%d s", val);
}

#endif /* HAVE_RECORDING */

#ifdef HAVE_BACKLIGHT
static const char backlight_times_conf [] =
                  "off,on,1,2,3,4,5,6,7,8,9,10,15,20,25,30,45,60,90";
static void backlight_formatter(char *buffer, size_t buffer_size, 
        int val, const char *unit)
{
    (void)unit;
    if (val == 0)
        strcpy(buffer, str(LANG_OFF));
    else if (val == 1)
        strcpy(buffer, str(LANG_ON));
    else
        snprintf(buffer, buffer_size, "%d s", backlight_timeout_value[val]);
}
static long backlight_getlang(int value)
{
    if (value == 0)
        return LANG_OFF;
    else if (value == 1)
        return LANG_ON;
    return TALK_ID(backlight_timeout_value[value], UNIT_SEC);
}
#endif
/* ffwd/rewind and scan acceleration stuff */
static const unsigned char ff_rewind_min_stepvals[] = {1,2,3,4,5,6,8,10,15,20,25,30,45,60};
static long ff_rewind_min_step_getlang(int value)
{
    return TALK_ID(ff_rewind_min_stepvals[value], UNIT_SEC);
}
static void ff_rewind_min_step_formatter(char *buffer, size_t buffer_size, 
        int val, const char *unit)
{
    (void)unit;
    snprintf(buffer, buffer_size, "%ds", ff_rewind_min_stepvals[val]);
}
static long scanaccel_getlang(int value)
{
    if (value == 0)
        return LANG_OFF;
    return TALK_ID(value, UNIT_SEC);
}
static void scanaccel_formatter(char *buffer, size_t buffer_size, 
        int val, const char *unit)
{
    (void)unit;
    if (val == 0)
        strcpy(buffer, str(LANG_OFF));
    else
        snprintf(buffer, buffer_size, "2x/%ds", val);
}

static const unsigned char poweroff_idle_timer_times[] = {0,1,2,3,4,5,6,7,8,9,10,15,30,45,60};
static long poweroff_idle_timer_getlang(int value)
{
    if (value == 0)
        return LANG_OFF;
    return TALK_ID(poweroff_idle_timer_times[value], UNIT_MIN);
}
static void poweroff_idle_timer_formatter(char *buffer, size_t buffer_size, 
        int val, const char *unit)
{
    (void)unit;
    if (val == 0)
        strcpy(buffer, str(LANG_OFF));
    else
        snprintf(buffer, buffer_size, "%dm", poweroff_idle_timer_times[val]);
}

#ifndef HAVE_SCROLLWHEEL
static long listaccel_getlang(int value)
{
    if (value == 0)
        return LANG_OFF;
    return TALK_ID((HZ/2)*value, UNIT_MS);
}
static void listaccel_formatter(char *buffer, size_t buffer_size, 
        int val, const char *unit)
{
    (void)unit;
    if (val == 0)
        strcpy(buffer, str(LANG_OFF));
    else
        snprintf(buffer, buffer_size, "%d ms", 5*HZ*val);
}
#endif /* HAVE_SCROLLWHEEL */

#if CONFIG_CODEC == SWCODEC
static void crossfeed_format(char* buffer, size_t buffer_size, int value,
    const char* unit)
{
    snprintf(buffer, buffer_size, "%s%d.%d %s", value == 0 ? " " : "-",
        value / 10, value % 10, unit);
}
static void crossfeed_cross_set(int val)
{
   (void)val;
   dsp_set_crossfeed_cross_params(global_settings.crossfeed_cross_gain,
                                  global_settings.crossfeed_hf_attenuation,
                                  global_settings.crossfeed_hf_cutoff);
}

static void replaygain_preamp_format(char* buffer, size_t buffer_size, int value,
    const char* unit)
{
    int v = abs(value);

    snprintf(buffer, buffer_size, "%s%d.%d %s", value < 0 ? "-" : "",
        v / 10, v % 10, unit);
}

#endif
#if (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
static void set_mdb_enable(bool value)
{
    sound_set_mdb_enable((int)value);
}
static void set_superbass(bool value)
{
    sound_set_superbass((int)value);
}
#endif

#ifdef HAVE_LCD_CHARCELLS
static void jumpscroll_format(char* buffer, size_t buffer_size, int value,
    const char* unit)
{
    (void)unit;
    switch (value)
    {
        case 0:
            strcpy(buffer, str(LANG_OFF));
            break;
        case 1:
            strcpy(buffer, str(LANG_ONE_TIME));
            break;
        case 2:
        case 3:
        case 4:
            snprintf(buffer, buffer_size, "%d", value);
            break;
        case 5:
            strcpy(buffer, str(LANG_ALWAYS));
            break;
    }
}
static long jumpscroll_getlang(int value)
{
    switch (value)
    {
        case 0:
            return LANG_OFF;
        case 1:
            return LANG_ONE_TIME;
        case 2:
        case 3:
        case 4:
            return TALK_ID(2, UNIT_INT);
        case 5:
            return LANG_ALWAYS;
    }
    return -1;
}
#endif /* HAVE_LCD_CHARCELLS */

#if defined (HAVE_SCROLLWHEEL)      || \
    (CONFIG_KEYPAD == IPOD_3G_PAD)  || \
    (CONFIG_KEYPAD == IPOD_4G_PAD)  || \
    (CONFIG_KEYPAD == IPOD_1G2G_PAD)
#define BALANCE_FLIP F_FLIPLIST
#else
#define BALANCE_FLIP 0
#endif
                    
const struct settings_list settings[] = {
    /* sound settings */
    SOUND_SETTING(0,volume, LANG_VOLUME, "volume", SOUND_VOLUME),
    SOUND_SETTING(BALANCE_FLIP, balance, LANG_BALANCE, "balance", SOUND_BALANCE),
    SOUND_SETTING(0,bass, LANG_BASS, "bass", SOUND_BASS),
    SOUND_SETTING(0,treble, LANG_TREBLE, "treble", SOUND_TREBLE),
    
#if (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
    SOUND_SETTING(0,loudness, LANG_LOUDNESS, "loudness", SOUND_LOUDNESS),
    STRINGCHOICE_SETTING(0,avc,LANG_AUTOVOL,0,"auto volume",
            "off,20ms,2,4,8,", sound_set_avc, 5,
            LANG_OFF,TALK_ID(20, UNIT_MS),TALK_ID(2, UNIT_SEC),
            TALK_ID(4, UNIT_SEC),TALK_ID(8, UNIT_SEC)), 
    OFFON_SETTING(0, superbass, LANG_SUPERBASS, false, "superbass", set_superbass),
#endif
         
    CHOICE_SETTING(0,channel_config,LANG_CHANNEL_CONFIGURATION,0,"channels",
         "stereo,mono,custom,mono left,mono right,karaoke",
         sound_set_channels,
         6, ID2P(LANG_CHANNEL_STEREO), ID2P(LANG_CHANNEL_MONO),
            ID2P(LANG_CHANNEL_CUSTOM), ID2P(LANG_CHANNEL_LEFT),
            ID2P(LANG_CHANNEL_RIGHT), ID2P(LANG_CHANNEL_KARAOKE)),
    SOUND_SETTING(0,stereo_width, LANG_STEREO_WIDTH,
            "stereo_width", SOUND_STEREO_WIDTH),
    /* playback */
    OFFON_SETTING(0, playlist_shuffle, LANG_SHUFFLE, false, "shuffle", NULL),
    SYSTEM_SETTING(NVRAM(4),resume_index,-1),
    SYSTEM_SETTING(NVRAM(4),resume_first_index,0),
    SYSTEM_SETTING(NVRAM(4),resume_offset,-1),
    SYSTEM_SETTING(NVRAM(4),resume_seed,-1),
    CHOICE_SETTING(0, repeat_mode, LANG_REPEAT, REPEAT_ALL, "repeat",
         "off,all,one,shuffle"
#ifdef AB_REPEAT_ENABLE
         ",ab"
#endif
        , NULL,
#ifdef AB_REPEAT_ENABLE
        5,
#else
        4,
#endif
        ID2P(LANG_OFF), ID2P(LANG_ALL), ID2P(LANG_REPEAT_ONE), ID2P(LANG_SHUFFLE)
#ifdef AB_REPEAT_ENABLE
        ,ID2P(LANG_REPEAT_AB)
#endif
    ), /* CHOICE_SETTING( repeat_mode ) */
    /* LCD */
#ifdef HAVE_LCD_CONTRAST
    /* its easier to leave this one un-macro()ed for the time being */
    {F_T_INT|F_DEF_ISFUNC|F_INT_SETTING, &global_settings.contrast,
         LANG_CONTRAST, FUNCTYPE(lcd_default_contrast),
         "contrast", NULL , {.int_setting = (struct int_setting[]){                
            { lcd_set_contrast, UNIT_INT, MIN_CONTRAST_SETTING,
                MAX_CONTRAST_SETTING, 1, NULL, NULL}}}},
#endif
#ifdef HAVE_BACKLIGHT
    INT_SETTING_W_CFGVALS(F_FLIPLIST, backlight_timeout, LANG_BACKLIGHT, 6,
        "backlight timeout", backlight_times_conf, UNIT_SEC,
        0, 18, 1, backlight_formatter, backlight_getlang, 
        backlight_set_timeout),
#if CONFIG_CHARGING
    INT_SETTING_W_CFGVALS(F_FLIPLIST, backlight_timeout_plugged,
        LANG_BACKLIGHT_ON_WHEN_CHARGING, 11,
        "backlight timeout plugged", backlight_times_conf, UNIT_SEC,
        0, 18, 1, backlight_formatter, backlight_getlang, 
        backlight_set_timeout_plugged),
#endif
#endif /* HAVE_BACKLIGHT */
#ifdef HAVE_LCD_BITMAP
#ifdef HAVE_LCD_INVERT
    BOOL_SETTING(0, invert, LANG_INVERT, false ,"invert", off_on,
        LANG_INVERT_LCD_INVERSE, LANG_NORMAL, lcd_set_invert_display),
#endif
#ifdef HAVE_LCD_FLIP
    OFFON_SETTING(0,flip_display, LANG_FLIP_DISPLAY, false,"flip display", NULL),
#endif
    /* display */
    BOOL_SETTING(F_TEMPVAR, invert_cursor, LANG_INVERT_CURSOR, true ,"invert cursor", off_on,
        LANG_INVERT_CURSOR_BAR, LANG_INVERT_CURSOR_POINTER, NULL),
    OFFON_SETTING(F_THEMESETTING|F_TEMPVAR, statusbar,
                  LANG_STATUS_BAR, true,"statusbar", NULL),
    OFFON_SETTING(0,scrollbar, LANG_SCROLL_BAR, true,"scrollbar", NULL),
#if CONFIG_KEYPAD == RECORDER_PAD
    OFFON_SETTING(0,buttonbar, LANG_BUTTON_BAR ,true,"buttonbar", NULL),
#endif
    CHOICE_SETTING(0, volume_type, LANG_VOLUME_DISPLAY, 0,
        "volume display", graphic_numeric, NULL, 2,
        ID2P(LANG_DISPLAY_GRAPHIC), ID2P(LANG_DISPLAY_NUMERIC)),
    CHOICE_SETTING(0, battery_display, LANG_BATTERY_DISPLAY, 0,
        "battery display", graphic_numeric, NULL, 2,
        ID2P(LANG_DISPLAY_GRAPHIC), ID2P(LANG_DISPLAY_NUMERIC)),
#if CONFIG_RTC
    CHOICE_SETTING(0, timeformat, LANG_TIMEFORMAT, 0,
        "time format", "24hour,12hour", NULL, 2,
        ID2P(LANG_24_HOUR_CLOCK), ID2P(LANG_12_HOUR_CLOCK)),
#endif
#endif /* HAVE_LCD_BITMAP */
    OFFON_SETTING(0,show_icons, LANG_SHOW_ICONS ,true,"show icons", NULL),
    /* system */
    INT_SETTING_W_CFGVALS(F_FLIPLIST, poweroff, LANG_POWEROFF_IDLE, 10, "idle poweroff",
                    "off,1,2,3,4,5,6,7,8,9,10,15,30,45,60", UNIT_MIN,
                    0, 14, 1, poweroff_idle_timer_formatter,
                    poweroff_idle_timer_getlang, set_poweroff_timeout),
    SYSTEM_SETTING(NVRAM(4),runtime,0),
    SYSTEM_SETTING(NVRAM(4),topruntime,0),

    INT_SETTING(0,max_files_in_playlist,LANG_MAX_FILES_IN_PLAYLIST,
#if MEM > 1
        10000,
#else
        400,
#endif
            "max files in playlist", UNIT_INT,1000,20000,1000,NULL,NULL,NULL),
    INT_SETTING(0,max_files_in_dir,LANG_MAX_FILES_IN_DIR,
#if MEM > 1
        1000,
#else
        200,
#endif
            "max files in dir", UNIT_INT,50,10000,50,NULL,NULL,NULL),
#if BATTERY_CAPACITY_INC > 0
    INT_SETTING(0, battery_capacity, LANG_BATTERY_CAPACITY, BATTERY_CAPACITY_DEFAULT, 
                "battery capacity", UNIT_MAH,
                BATTERY_CAPACITY_MIN, BATTERY_CAPACITY_MAX, BATTERY_CAPACITY_INC,
                NULL, NULL, NULL),
#endif
#if CONFIG_CHARGING
    OFFON_SETTING(NVRAM(1), car_adapter_mode,
        LANG_CAR_ADAPTER_MODE, false, "car adapter mode", NULL),
#endif
    /* tuner */
#if CONFIG_TUNER
    OFFON_SETTING(0,fm_force_mono, LANG_FM_MONO_MODE,
        false, "force fm mono", toggle_mono_mode),
    SYSTEM_SETTING(NVRAM(4),last_frequency,0),
#endif

#if BATTERY_TYPES_COUNT > 1
    CHOICE_SETTING(0, battery_type, LANG_BATTERY_TYPE, 0,
        "battery type","alkaline,nimh", NULL, 2,
        ID2P(LANG_BATTERY_TYPE_ALKALINE), ID2P(LANG_BATTERY_TYPE_NIMH)),
#endif
#ifdef HAVE_REMOTE_LCD
    /* remote lcd */
    INT_SETTING(0, remote_contrast, LANG_CONTRAST, DEFAULT_REMOTE_CONTRAST_SETTING,
        "remote contrast", UNIT_INT, MIN_REMOTE_CONTRAST_SETTING, 
        MAX_REMOTE_CONTRAST_SETTING, 1, NULL, NULL, lcd_remote_set_contrast),
    BOOL_SETTING(0, remote_invert, LANG_INVERT, false ,"remote invert", off_on,
        LANG_INVERT_LCD_INVERSE, LANG_NORMAL, lcd_remote_set_invert_display),
    OFFON_SETTING(0,remote_flip_display, LANG_FLIP_DISPLAY,
        false,"remote flip display", NULL),
    INT_SETTING_W_CFGVALS(F_FLIPLIST, remote_backlight_timeout, LANG_BACKLIGHT, 6,
        "remote backlight timeout", backlight_times_conf, UNIT_SEC,
        0, 18, 1, backlight_formatter, backlight_getlang, 
        remote_backlight_set_timeout),
#if CONFIG_CHARGING
    INT_SETTING_W_CFGVALS(F_FLIPLIST, remote_backlight_timeout_plugged,
        LANG_BACKLIGHT_ON_WHEN_CHARGING, 11,
        "remote backlight timeout plugged", backlight_times_conf, UNIT_SEC,
        0, 18, 1, backlight_formatter, backlight_getlang, 
        remote_backlight_set_timeout_plugged),
#endif
#ifdef HAVE_REMOTE_LCD_TICKING
    OFFON_SETTING(0,remote_reduce_ticking, LANG_REDUCE_TICKING,
        false,"remote reduce ticking", NULL),
#endif
#endif

#ifdef HAVE_BACKLIGHT
    OFFON_SETTING(0,bl_filter_first_keypress,
        LANG_BACKLIGHT_FILTER_FIRST_KEYPRESS, false,
        "backlight filters first keypress", NULL),
#ifdef HAVE_REMOTE_LCD
    OFFON_SETTING(0,remote_bl_filter_first_keypress,
        LANG_BACKLIGHT_FILTER_FIRST_KEYPRESS, false,
        "backlight filters first remote keypress", NULL),
#endif
#endif /* HAVE_BACKLIGHT */

/** End of old RTC config block **/

#ifdef HAVE_BACKLIGHT
    OFFON_SETTING(0,caption_backlight, LANG_CAPTION_BACKLIGHT, 
        false,"caption backlight",NULL),
#ifdef HAVE_REMOTE_LCD
    OFFON_SETTING(0,remote_caption_backlight, LANG_CAPTION_BACKLIGHT, 
        false,"remote caption backlight",NULL),
#endif
#endif /* HAVE_BACKLIGHT */
#ifdef HAVE_BACKLIGHT_BRIGHTNESS
    INT_SETTING(0, brightness, LANG_BRIGHTNESS, DEFAULT_BRIGHTNESS_SETTING,
        "brightness",UNIT_INT, MIN_BRIGHTNESS_SETTING, MAX_BRIGHTNESS_SETTING, 1,
        NULL, NULL, backlight_set_brightness),
#endif
#if defined(HAVE_BACKLIGHT_PWM_FADING)  && !defined(SIMULATOR)
    /* backlight fading */
    STRINGCHOICE_SETTING(0,backlight_fade_in, LANG_BACKLIGHT_FADE_IN, 1,
        "backlight fade in","off,500ms,1s,2s", backlight_set_fade_in, 4,
        LANG_OFF, TALK_ID(500, UNIT_MS),
        TALK_ID(1, UNIT_SEC), TALK_ID(2, UNIT_SEC)),
    STRINGCHOICE_SETTING(0,backlight_fade_out, LANG_BACKLIGHT_FADE_OUT, 1,
        "backlight fade out","off,500ms,1s,2s,3s,5s,10s", backlight_set_fade_out, 7,
        LANG_OFF, TALK_ID(500, UNIT_MS), 
        TALK_ID(1, UNIT_SEC), TALK_ID(2, UNIT_SEC), 
        TALK_ID(3, UNIT_SEC), TALK_ID(5, UNIT_SEC), TALK_ID(10, UNIT_SEC)),
#endif
    INT_SETTING(0, scroll_speed, LANG_SCROLL_SPEED, 9,"scroll speed",                      
                UNIT_INT, 0, 15, 1, NULL, NULL, lcd_scroll_speed),
    INT_SETTING(0, scroll_delay, LANG_SCROLL_DELAY, 1000, "scroll delay",                      
                UNIT_MS, 0, 2500, 100, NULL,
                NULL, lcd_scroll_delay) ,
    INT_SETTING(0, bidir_limit, LANG_BIDIR_SCROLL, 50, "bidir limit",                      
                UNIT_PERCENT, 0, 200, 25, NULL, NULL, lcd_bidir_scroll),
#ifdef HAVE_REMOTE_LCD
    INT_SETTING(0, remote_scroll_speed, LANG_SCROLL_SPEED, 9, "remote scroll speed",                      
                UNIT_INT, 0,15, 1, NULL, NULL, lcd_remote_scroll_speed),
    INT_SETTING(0, remote_scroll_step, LANG_SCROLL_STEP, 6, "remote scroll step",                      
                UNIT_PIXEL, 1, LCD_REMOTE_WIDTH, 1, NULL, NULL, lcd_remote_scroll_step),
    INT_SETTING(0, remote_scroll_delay, LANG_SCROLL_DELAY, 1000, "remote scroll delay",                      
                UNIT_MS, 0, 2500, 100, NULL, NULL, lcd_remote_scroll_delay),
    INT_SETTING(0, remote_bidir_limit, LANG_BIDIR_SCROLL, 50, "remote bidir limit",                      
                UNIT_PERCENT, 0, 200, 25, NULL, NULL, lcd_remote_bidir_scroll),
#endif
#ifdef HAVE_LCD_BITMAP
    OFFON_SETTING(0, offset_out_of_view, LANG_SCREEN_SCROLL_VIEW,
                    false, "Screen Scrolls Out Of View", NULL),
    INT_SETTING(0, scroll_step, LANG_SCROLL_STEP, 6, "scroll step",                      
                    UNIT_PIXEL, 1, LCD_WIDTH, 1, NULL, NULL, lcd_scroll_step),
    INT_SETTING(0, screen_scroll_step, LANG_SCREEN_SCROLL_STEP,
                    16, "screen scroll step",                      
                    UNIT_PIXEL, 1, LCD_WIDTH, 1, NULL, NULL, NULL),
#endif /* HAVE_LCD_BITMAP */
#ifdef HAVE_LCD_CHARCELLS
    INT_SETTING(0, jump_scroll, LANG_JUMP_SCROLL, 0, "jump scroll",                      
                    UNIT_INT, 0, 5, 1, jumpscroll_format, jumpscroll_getlang, lcd_jump_scroll),
    INT_SETTING(0, jump_scroll_delay, LANG_JUMP_SCROLL_DELAY, 500, "jump scroll delay",                      
                    UNIT_MS, 0, 2500, 100, NULL, NULL, lcd_jump_scroll_delay),
#endif
    OFFON_SETTING(0,scroll_paginated,LANG_SCROLL_PAGINATED,
        false,"scroll paginated",NULL),
#ifdef HAVE_LCD_COLOR
    {F_T_INT|F_RGB|F_THEMESETTING ,&global_settings.fg_color,-1,INT(LCD_DEFAULT_FG),
        "foreground color",NULL,UNUSED},
    {F_T_INT|F_RGB|F_THEMESETTING ,&global_settings.bg_color,-1,INT(LCD_DEFAULT_BG),
        "background color",NULL,UNUSED},
#endif
    /* more playback */
    OFFON_SETTING(0,play_selected,LANG_PLAY_SELECTED,true,"play selected",NULL),
    OFFON_SETTING(0,party_mode,LANG_PARTY_MODE,false,"party mode",NULL),
    OFFON_SETTING(0,fade_on_stop,LANG_FADE_ON_STOP,true,"volume fade",NULL),
    INT_SETTING_W_CFGVALS(F_FLIPLIST, ff_rewind_min_step, LANG_FFRW_STEP, FF_REWIND_1000,
        "scan min step", "1,2,3,4,5,6,8,10,15,20,25,30,45,60", UNIT_SEC,
        13, 0, -1, ff_rewind_min_step_formatter,
        ff_rewind_min_step_getlang, NULL),        
    INT_SETTING(0, ff_rewind_accel, LANG_FFRW_ACCEL, 3, "scan accel",
        UNIT_SEC, 16, 0, -1, scanaccel_formatter, scanaccel_getlang, NULL), 
#if (CONFIG_CODEC == SWCODEC) && !defined(HAVE_FLASH_STORAGE)
    STRINGCHOICE_SETTING(0, buffer_margin, LANG_MP3BUFFER_MARGIN, 0,"antiskip",
        "5s,15s,30s,1min,2min,3min,5min,10min",NULL, 8,
        TALK_ID(5, UNIT_SEC), TALK_ID(15, UNIT_SEC),
        TALK_ID(30, UNIT_SEC), TALK_ID(1, UNIT_MIN), TALK_ID(2, UNIT_MIN),
        TALK_ID(3, UNIT_MIN), TALK_ID(5, UNIT_MIN), TALK_ID(10, UNIT_MIN)),
#elif !defined(HAVE_FLASH_STORAGE)
    INT_SETTING(0, buffer_margin, LANG_MP3BUFFER_MARGIN, 0, "antiskip",
                    UNIT_SEC, 0, 7, 1, NULL, NULL, audio_set_buffer_margin),
#endif
    /* disk */
#ifndef HAVE_FLASH_STORAGE
    INT_SETTING(0, disk_spindown, LANG_SPINDOWN, 5, "disk spindown",
                    UNIT_SEC, 3, 254, 1, NULL, NULL, ata_spindown),
#endif /* HAVE_FLASH_STORAGE */
    /* browser */
    CHOICE_SETTING(0, dirfilter, LANG_FILTER, SHOW_SUPPORTED, "show files",
        "all,supported,music,playlists", NULL, 4, ID2P(LANG_ALL),
        ID2P(LANG_FILTER_SUPPORTED), ID2P(LANG_FILTER_MUSIC), ID2P(LANG_PLAYLISTS)),
    OFFON_SETTING(0,sort_case,LANG_SORT_CASE,false,"sort case",NULL),
    CHOICE_SETTING(0, show_filename_ext, LANG_SHOW_FILENAME_EXT, 3,
        "show filename exts", "off,on,unknown,view_all", NULL , 4 ,
        ID2P(LANG_OFF), ID2P(LANG_ON), ID2P(LANG_UNKNOWN_TYPES),
        ID2P(LANG_EXT_ONLY_VIEW_ALL)),
    OFFON_SETTING(0,browse_current,LANG_FOLLOW,false,"follow playlist",NULL),
    OFFON_SETTING(0,playlist_viewer_icons,LANG_SHOW_ICONS,true,
        "playlist viewer icons",NULL),
    OFFON_SETTING(0,playlist_viewer_indices,LANG_SHOW_INDICES,true,
        "playlist viewer indices",NULL),
    CHOICE_SETTING(0, playlist_viewer_track_display, LANG_TRACK_DISPLAY, 0,
                   "playlist viewer track display","track name,full path", NULL, 2,
                    ID2P(LANG_DISPLAY_TRACK_NAME_ONLY), ID2P(LANG_DISPLAY_FULL_PATH)),
    CHOICE_SETTING(0, recursive_dir_insert, LANG_RECURSE_DIRECTORY , RECURSE_OFF,
        "recursive directory insert", off_on_ask, NULL , 3 ,
        ID2P(LANG_OFF), ID2P(LANG_ON), ID2P(LANG_ASK)),
    /* bookmarks */
    CHOICE_SETTING(0, autocreatebookmark, LANG_BOOKMARK_SETTINGS_AUTOCREATE,
        BOOKMARK_NO, "autocreate bookmarks",
        "off,on,ask,recent only - on,recent only - ask", NULL, 5,
        ID2P(LANG_SET_BOOL_NO), ID2P(LANG_SET_BOOL_YES),
        ID2P(LANG_ASK), ID2P(LANG_BOOKMARK_SETTINGS_RECENT_ONLY_YES),
        ID2P(LANG_BOOKMARK_SETTINGS_RECENT_ONLY_ASK)),
    CHOICE_SETTING(0, autoloadbookmark, LANG_BOOKMARK_SETTINGS_AUTOLOAD, 
        BOOKMARK_NO, "autoload bookmarks", off_on_ask, NULL, 3,
        ID2P(LANG_SET_BOOL_NO), ID2P(LANG_SET_BOOL_YES), ID2P(LANG_ASK)),
    CHOICE_SETTING(0, usemrb, LANG_BOOKMARK_SETTINGS_MAINTAIN_RECENT_BOOKMARKS,
        BOOKMARK_NO, "use most-recent-bookmarks", "off,on,unique only", NULL, 3,
        ID2P(LANG_SET_BOOL_NO), ID2P(LANG_SET_BOOL_YES),
        ID2P(LANG_BOOKMARK_SETTINGS_UNIQUE_ONLY)),
#ifdef HAVE_LCD_BITMAP
    /* peak meter */
    STRINGCHOICE_SETTING(0, peak_meter_clip_hold, LANG_PM_CLIP_HOLD, 16,
        "peak meter clip hold",
        "on,1,2,3,4,5,6,7,8,9,10,15,20,25,30,45,60,90,2min"
        ",3min,5min,10min,20min,45min,90min", peak_meter_set_clip_hold,
        25, LANG_PM_ETERNAL,
        TALK_ID(1, UNIT_SEC), TALK_ID(2, UNIT_SEC), TALK_ID(3, UNIT_SEC), 
        TALK_ID(4, UNIT_SEC), TALK_ID(5, UNIT_SEC), TALK_ID(6, UNIT_SEC), 
        TALK_ID(7, UNIT_SEC), TALK_ID(8, UNIT_SEC), TALK_ID(9, UNIT_SEC), 
        TALK_ID(10, UNIT_SEC), TALK_ID(15, UNIT_SEC), TALK_ID(20, UNIT_SEC), 
        TALK_ID(25, UNIT_SEC), TALK_ID(30, UNIT_SEC), TALK_ID(45, UNIT_SEC), 
        TALK_ID(60, UNIT_SEC), TALK_ID(90, UNIT_SEC), TALK_ID(2, UNIT_MIN), 
        TALK_ID(3, UNIT_MIN), TALK_ID(5, UNIT_MIN), TALK_ID(10, UNIT_MIN), 
        TALK_ID(20, UNIT_MIN), TALK_ID(45, UNIT_MIN), TALK_ID(90, UNIT_MIN)),
    STRINGCHOICE_SETTING(0, peak_meter_hold, LANG_PM_PEAK_HOLD, 3,
        "peak meter hold",
        "off,200ms,300ms,500ms,1,2,3,4,5,6,7,8,9,10,15,20,30,1min",
        NULL, 18, LANG_OFF,
        TALK_ID(200, UNIT_MS), TALK_ID(300, UNIT_MS), TALK_ID(500, UNIT_MS), 
        TALK_ID(1, UNIT_SEC), TALK_ID(2, UNIT_SEC), TALK_ID(3, UNIT_SEC), 
        TALK_ID(4, UNIT_SEC), TALK_ID(5, UNIT_SEC), TALK_ID(6, UNIT_SEC), 
        TALK_ID(7, UNIT_SEC), TALK_ID(8, UNIT_SEC), TALK_ID(9, UNIT_SEC), 
        TALK_ID(10, UNIT_SEC), TALK_ID(15, UNIT_SEC), TALK_ID(20, UNIT_SEC), 
        TALK_ID(30, UNIT_SEC), TALK_ID(60, UNIT_SEC)),
    INT_SETTING(0, peak_meter_release, LANG_PM_RELEASE, 8, "peak meter release",                      
                    UNIT_PM_TICK, 1, 0x7e, 1, NULL, NULL,NULL), 
    OFFON_SETTING(0,peak_meter_dbfs,LANG_PM_DBFS,true,"peak meter dbfs",NULL),
    {F_T_INT,&global_settings.peak_meter_min,LANG_PM_MIN,INT(60),"peak meter min",NULL,UNUSED},
    {F_T_INT,&global_settings.peak_meter_max,LANG_PM_MAX,INT(0),"peak meter max",NULL,UNUSED},
#endif
#if (CONFIG_CODEC == MAS3587F) || (CONFIG_CODEC == MAS3539F)
    SOUND_SETTING(0, mdb_strength, LANG_MDB_STRENGTH,
        "mdb strength", SOUND_MDB_STRENGTH),
    SOUND_SETTING(0, mdb_harmonics, LANG_MDB_HARMONICS,
        "mdb harmonics", SOUND_MDB_HARMONICS),
    SOUND_SETTING(0, mdb_center, LANG_MDB_CENTER,
        "mdb center", SOUND_MDB_CENTER),
    SOUND_SETTING(0, mdb_shape, LANG_MDB_SHAPE,
        "mdb shape", SOUND_MDB_SHAPE),
    OFFON_SETTING(0, mdb_enable, LANG_MDB_ENABLE,
        false, "mdb enable", set_mdb_enable),
#endif
#if CONFIG_CODEC == MAS3507D
    OFFON_SETTING(0,line_in,LANG_LINE_IN,false,"line in",NULL),
#endif
    /* voice */
    OFFON_SETTING(F_TEMPVAR, talk_menu, LANG_VOICE_MENU, true, "talk menu", NULL),
    CHOICE_SETTING(0, talk_dir, LANG_VOICE_DIR, 0,
        "talk dir", off_number_spell, NULL, 3,
        ID2P(LANG_OFF), ID2P(LANG_VOICE_NUMBER),
        ID2P(LANG_VOICE_SPELL)),
    OFFON_SETTING(F_TEMPVAR, talk_dir_clip, LANG_VOICE_DIR_TALK, false, "talk dir clip", NULL),
    CHOICE_SETTING(0, talk_file, LANG_VOICE_FILE, 0,
        "talk file", off_number_spell, NULL, 3,
        ID2P(LANG_OFF), ID2P(LANG_VOICE_NUMBER),
        ID2P(LANG_VOICE_SPELL)),
    OFFON_SETTING(F_TEMPVAR, talk_file_clip, LANG_VOICE_FILE_TALK, false, "talk file clip", NULL),

    /* file sorting */
    CHOICE_SETTING(0, sort_file, LANG_SORT_FILE, 0 ,
        "sort files", "alpha,oldest,newest,type", NULL, 4,
        ID2P(LANG_SORT_ALPHA), ID2P(LANG_SORT_DATE),
        ID2P(LANG_SORT_DATE_REVERSE) , ID2P(LANG_SORT_TYPE)),
    CHOICE_SETTING(0, sort_dir, LANG_SORT_DIR, 0 ,
        "sort dirs", "alpha,oldest,newest", NULL, 3,
        ID2P(LANG_SORT_ALPHA), ID2P(LANG_SORT_DATE),
        ID2P(LANG_SORT_DATE_REVERSE)),
    BOOL_SETTING(0, id3_v1_first, LANG_ID3_ORDER, false,
        "id3 tag priority", "v2-v1,v1-v2",
        LANG_ID3_V1_FIRST, LANG_ID3_V2_FIRST, mpeg_id3_options),

#ifdef HAVE_RECORDING
    /* recording */
    STRINGCHOICE_SETTING(F_RECSETTING, rec_timesplit, LANG_SPLIT_TIME, 0,
        "rec timesplit",
        "off,00:05,00:10,00:15,00:30,01:00,01:14,01:20,02:00,"
        "04:00,06:00,08:00,10:00,12:00,18:00,24:00",
        NULL, 16, LANG_OFF,
        TALK_ID(5, UNIT_MIN), TALK_ID(10, UNIT_MIN), TALK_ID(15, UNIT_MIN), 
        TALK_ID(30, UNIT_MIN), TALK_ID(60, UNIT_MIN), TALK_ID(74, UNIT_MIN), 
        TALK_ID(80, UNIT_MIN), TALK_ID(2, UNIT_HOUR), TALK_ID(4, UNIT_HOUR), 
        TALK_ID(6, UNIT_HOUR), TALK_ID(8, UNIT_HOUR), TALK_ID(10, UNIT_HOUR), 
        TALK_ID(12, UNIT_HOUR), TALK_ID(18, UNIT_HOUR), TALK_ID(20, UNIT_HOUR), 
        TALK_ID(24, UNIT_HOUR)),
    STRINGCHOICE_SETTING(F_RECSETTING, rec_sizesplit, LANG_SPLIT_SIZE, 0,
        "rec sizesplit",
        "off,5MB,10MB,15MB,32MB,64MB,75MB,100MB,128MB,"
        "256MB,512MB,650MB,700MB,1GB,1.5GB,1.75GB",
        NULL, 16, LANG_OFF,
        TALK_ID(5, UNIT_MB), TALK_ID(10, UNIT_MB), TALK_ID(15, UNIT_MB), 
        TALK_ID(32, UNIT_MB), TALK_ID(64, UNIT_MB), TALK_ID(75, UNIT_MB), 
        TALK_ID(100, UNIT_MB), TALK_ID(128, UNIT_MB), TALK_ID(256, UNIT_MB), 
        TALK_ID(512, UNIT_MB), TALK_ID(650, UNIT_MB), TALK_ID(700, UNIT_MB), 
        TALK_ID(1024, UNIT_MB), TALK_ID(1536, UNIT_MB), TALK_ID(1792, UNIT_MB)),
    {F_T_INT|F_RECSETTING, &global_settings.rec_channels,
        LANG_CHANNELS, INT(0),
        "rec channels","stereo,mono",UNUSED},
    CHOICE_SETTING(F_RECSETTING, rec_split_type, LANG_SPLIT_TYPE, 0 ,
        "rec split type", "Split, Stop", NULL, 2,
        ID2P(LANG_START_NEW_FILE), ID2P(LANG_STOP_RECORDING)),
    CHOICE_SETTING(F_RECSETTING, rec_split_method, LANG_SPLIT_MEASURE, 0 ,
        "rec split method", "Time,Filesize", NULL, 2,
        ID2P(LANG_TIME), ID2P(LANG_REC_SIZE)),
    {F_T_INT|F_RECSETTING, &global_settings.rec_source,
        LANG_RECORDING_SOURCE, INT(0),
        "rec source",
        &HAVE_MIC_REC_(",mic")
         HAVE_LINE_REC_(",line")
         HAVE_SPDIF_REC_(",spdif")
         HAVE_FMRADIO_REC_(",fmradio")[1]
        ,UNUSED},
    INT_SETTING(F_RECSETTING, rec_prerecord_time, LANG_RECORD_PRERECORD_TIME,
            0, "prerecording time",
            UNIT_SEC, 0, 30, 1, rectime_formatter, rectime_getlang, NULL),
        
    FILENAME_SETTING(F_RECSETTING, rec_directory, "rec path", 
                     REC_BASE_DIR, NULL, NULL, MAX_FILENAME+1),
#ifdef HAVE_BACKLIGHT
    CHOICE_SETTING(F_RECSETTING, cliplight, LANG_CLIP_LIGHT, 0 ,
        "cliplight", "off,main,both,remote", NULL, 
#ifdef HAVE_REMOTE_LCD
        4, ID2P(LANG_OFF), ID2P(LANG_MAIN_UNIT), ID2P(LANG_REMOTE_MAIN),
        ID2P(LANG_REMOTE_UNIT)
#else
        2, ID2P(LANG_OFF), ID2P(LANG_ON)
#endif
        ),
    {F_T_INT|F_RECSETTING,&global_settings.cliplight,LANG_CLIP_LIGHT,INT(0),
        "cliplight","off,main,both,remote",UNUSED},
#endif
#ifdef DEFAULT_REC_MIC_GAIN
    {F_T_INT|F_RECSETTING,&global_settings.rec_mic_gain,
        LANG_GAIN,INT(DEFAULT_REC_MIC_GAIN),
        "rec mic gain",NULL,UNUSED},
#endif /* DEFAULT_REC_MIC_GAIN */
#ifdef DEFAULT_REC_LEFT_GAIN
    {F_T_INT|F_RECSETTING,&global_settings.rec_left_gain,
        LANG_RECORDING_LEFT,INT(DEFAULT_REC_LEFT_GAIN),
        "rec left gain",NULL,UNUSED},
#endif /* DEFAULT_REC_LEFT_GAIN */
#ifdef DEFAULT_REC_RIGHT_GAIN
    {F_T_INT|F_RECSETTING,&global_settings.rec_right_gain,LANG_RECORDING_RIGHT,
        INT(DEFAULT_REC_RIGHT_GAIN),
        "rec right gain",NULL,UNUSED},
#endif /* DEFAULT_REC_RIGHT_GAIN */
#if CONFIG_CODEC == MAS3587F
    {F_T_INT|F_RECSETTING,&global_settings.rec_frequency,
        LANG_RECORDING_FREQUENCY,
        INT(0),"rec frequency","44,48,32,22,24,16",UNUSED},
    INT_SETTING(F_RECSETTING, rec_quality, LANG_RECORDING_QUALITY, 
                5, "rec quality",
                UNIT_INT, 0, 7, 1, NULL, NULL, NULL),
    OFFON_SETTING(F_RECSETTING,rec_editable,LANG_RECORDING_EDITABLE,
                    false,"editable recordings",NULL),
#endif /* CONFIG_CODEC == MAS3587F */
#if CONFIG_CODEC == SWCODEC
    {F_T_INT|F_RECSETTING,&global_settings.rec_frequency,
        LANG_RECORDING_FREQUENCY,INT(REC_FREQ_DEFAULT),
        "rec frequency",REC_FREQ_CFG_VAL_LIST,UNUSED},
    {F_T_INT|F_RECSETTING,&global_settings.rec_format,
        LANG_RECORDING_FORMAT,INT(REC_FORMAT_DEFAULT),
        "rec format",REC_FORMAT_CFG_VAL_LIST,UNUSED},
    /** Encoder settings start - keep these together **/
    /* aiff_enc */
    /* (no settings yet) */
    /* mp3_enc */
    {F_T_INT|F_RECSETTING, &global_settings.mp3_enc_config.bitrate,-1,
        INT(MP3_ENC_BITRATE_CFG_DEFAULT),
        "mp3_enc bitrate",MP3_ENC_BITRATE_CFG_VALUE_LIST,UNUSED},
    /* wav_enc */
    /* (no settings yet) */
    /* wavpack_enc */
    /* (no settings yet) */
    /** Encoder settings end **/
#endif /* CONFIG_CODEC == SWCODEC */
    /* values for the trigger */
    {F_T_INT|F_RECSETTING,&global_settings.rec_start_thres,
        LANG_RECORD_START_THRESHOLD, INT(-35),
        "trigger start threshold",NULL,UNUSED},
    {F_T_INT|F_RECSETTING,&global_settings.rec_stop_thres,
        LANG_RECORD_STOP_THRESHOLD,INT(-45),
        "trigger stop threshold",NULL,UNUSED},
    {F_T_INT|F_RECSETTING,&global_settings.rec_start_duration,
        LANG_MIN_DURATION,INT(0),
        "trigger start duration",trig_durations_conf,UNUSED},
    {F_T_INT|F_RECSETTING,&global_settings.rec_stop_postrec,
        LANG_MIN_DURATION,INT(2),
        "trigger stop postrec",trig_durations_conf,UNUSED},
    {F_T_INT|F_RECSETTING,&global_settings.rec_stop_gap,
        LANG_RECORD_STOP_GAP,INT(1),
        "trigger min gap",trig_durations_conf,UNUSED},
    {F_T_INT|F_RECSETTING,&global_settings.rec_trigger_mode,
        LANG_RECORD_TRIGGER,INT(0),
        "trigger mode","off,once,repeat",UNUSED},
#endif /* HAVE_RECORDING */

#ifdef HAVE_SPDIF_POWER
    OFFON_SETTING(0, spdif_enable, LANG_SPDIF_ENABLE, false,
        "spdif enable", spdif_power_enable),
#endif
    CHOICE_SETTING(0, next_folder, LANG_NEXT_FOLDER, FOLDER_ADVANCE_OFF,
        "folder navigation", "off,on,random",NULL ,3,
        ID2P(LANG_SET_BOOL_NO), ID2P(LANG_SET_BOOL_YES), ID2P(LANG_RANDOM)),
    OFFON_SETTING(0,runtimedb,LANG_RUNTIMEDB_ACTIVE,false,"gather runtime data",NULL),

#if CONFIG_CODEC == SWCODEC
    /* replay gain */
    OFFON_SETTING(0, replaygain, LANG_REPLAYGAIN_ENABLE, false, "replaygain", NULL),
    CHOICE_SETTING(0, replaygain_type, LANG_REPLAYGAIN_MODE, REPLAYGAIN_ALBUM,
        "replaygain type", "track,album,track shuffle", NULL, 3,
        ID2P(LANG_TRACK_GAIN), ID2P(LANG_ALBUM_GAIN), ID2P(LANG_SHUFFLE_GAIN)),
    OFFON_SETTING(0, replaygain_noclip, LANG_REPLAYGAIN_NOCLIP,
        false, "replaygain noclip", NULL),
    INT_SETTING(0, replaygain_preamp, LANG_REPLAYGAIN_PREAMP, 0, "replaygain preamp", 
                UNIT_DB, -120, 120, 5, replaygain_preamp_format, NULL, NULL),
    
    CHOICE_SETTING(0, beep, LANG_BEEP, 0,
        "beep", "off,weak,moderate,strong", NULL, 4,
        ID2P(LANG_OFF), ID2P(LANG_WEAK), ID2P(LANG_MODERATE), ID2P(LANG_STRONG)),

    /* crossfade */
    CHOICE_SETTING(0, crossfade, LANG_CROSSFADE_ENABLE, 0, "crossfade",
        "off,shuffle,track skip,shuffle and track skip,always",NULL, 5,
        ID2P(LANG_OFF), ID2P(LANG_SHUFFLE), ID2P(LANG_TRACKSKIP),
        ID2P(LANG_SHUFFLE_TRACKSKIP), ID2P(LANG_ALWAYS)),
    INT_SETTING(0, crossfade_fade_in_delay, LANG_CROSSFADE_FADE_IN_DELAY, 0,
        "crossfade fade in delay", UNIT_SEC, 0, 7, 1, NULL, NULL, NULL),
    INT_SETTING(0, crossfade_fade_out_delay, LANG_CROSSFADE_FADE_OUT_DELAY, 0,
        "crossfade fade out delay", UNIT_SEC, 0, 7, 1, NULL, NULL, NULL),
    INT_SETTING(0, crossfade_fade_in_duration, LANG_CROSSFADE_FADE_IN_DURATION, 2,
        "crossfade fade in duration", UNIT_SEC, 0, 15, 1, NULL, NULL, NULL),
    INT_SETTING(0, crossfade_fade_out_duration, LANG_CROSSFADE_FADE_OUT_DURATION, 2,
        "crossfade fade out duration", UNIT_SEC, 0, 15, 1, NULL, NULL, NULL),
    CHOICE_SETTING(0, crossfade_fade_out_mixmode, LANG_CROSSFADE_FADE_OUT_MODE,
        0, "crossfade fade out mode", "crossfade,mix", NULL, 2,
        ID2P(LANG_CROSSFADE), ID2P(LANG_MIX)),
        
    /* crossfeed */
    OFFON_SETTING(0,crossfeed, LANG_CROSSFEED, false,
                    "crossfeed", dsp_set_crossfeed),
    INT_SETTING(0, crossfeed_direct_gain, LANG_CROSSFEED_DIRECT_GAIN, 15,
                    "crossfeed direct gain", UNIT_DB, 0, 60, 5,
                    crossfeed_format, NULL, dsp_set_crossfeed_direct_gain),
    INT_SETTING(0, crossfeed_cross_gain, LANG_CROSSFEED_CROSS_GAIN, 60,
                    "crossfeed cross gain", UNIT_DB, 30, 120, 5,
                    crossfeed_format, NULL, crossfeed_cross_set),
    INT_SETTING(0, crossfeed_hf_attenuation, LANG_CROSSFEED_HF_ATTENUATION, 160,
                    "crossfeed hf attenuation", UNIT_DB, 60, 240, 5,
                    crossfeed_format, NULL, crossfeed_cross_set),
    INT_SETTING(0, crossfeed_hf_cutoff, LANG_CROSSFEED_HF_CUTOFF, 700,
                    "crossfeed hf cutoff", UNIT_HERTZ, 500, 2000, 100,
                    NULL, NULL, crossfeed_cross_set),
    /* equalizer */
    OFFON_SETTING(0,eq_enabled,LANG_EQUALIZER_ENABLED,false,"eq enabled",NULL),
    INT_SETTING(0, eq_precut, LANG_EQUALIZER_PRECUT, 0, "eq precut",
                    UNIT_DB, 0, 240, 5, eq_precut_format, NULL, dsp_set_eq_precut),
    /* 0..32768 Hz */
    INT_SETTING(0, eq_band0_cutoff, LANG_EQUALIZER_BAND_CUTOFF, 60, "eq band 0 cutoff",
                    UNIT_HERTZ, EQ_CUTOFF_MIN, EQ_CUTOFF_MAX, EQ_CUTOFF_STEP,
                    NULL, NULL, NULL),
    INT_SETTING(0, eq_band1_cutoff, LANG_EQUALIZER_BAND_CENTER, 200, "eq band 1 cutoff",
                    UNIT_HERTZ, EQ_CUTOFF_MIN, EQ_CUTOFF_MAX, EQ_CUTOFF_STEP,
                    NULL, NULL, NULL),
    INT_SETTING(0, eq_band2_cutoff, LANG_EQUALIZER_BAND_CENTER, 800, "eq band 2 cutoff",
                    UNIT_HERTZ, EQ_CUTOFF_MIN, EQ_CUTOFF_MAX, EQ_CUTOFF_STEP,
                    NULL, NULL, NULL),
    INT_SETTING(0, eq_band3_cutoff, LANG_EQUALIZER_BAND_CENTER, 4000, "eq band 3 cutoff",
                    UNIT_HERTZ, EQ_CUTOFF_MIN, EQ_CUTOFF_MAX, EQ_CUTOFF_STEP,
                    NULL, NULL, NULL),
    INT_SETTING(0, eq_band4_cutoff, LANG_EQUALIZER_BAND_CUTOFF, 12000, "eq band 4 cutoff",
                    UNIT_HERTZ, EQ_CUTOFF_MIN, EQ_CUTOFF_MAX, EQ_CUTOFF_STEP,
                    NULL, NULL, NULL),
    /* 0..64 (or 0.0 to 6.4) */
    INT_SETTING(0, eq_band0_q, LANG_EQUALIZER_BAND_Q, 7, "eq band 0 q",
                    UNIT_INT, EQ_Q_MIN, EQ_Q_MAX, EQ_Q_STEP,
                    eq_q_format, NULL, NULL),
    INT_SETTING(0, eq_band1_q, LANG_EQUALIZER_BAND_Q, 10, "eq band 1 q",
                    UNIT_INT, EQ_Q_MIN, EQ_Q_MAX, EQ_Q_STEP,
                    eq_q_format, NULL, NULL),
    INT_SETTING(0, eq_band2_q, LANG_EQUALIZER_BAND_Q, 10, "eq band 2 q",
                    UNIT_INT, EQ_Q_MIN, EQ_Q_MAX, EQ_Q_STEP,
                    eq_q_format, NULL, NULL),
    INT_SETTING(0, eq_band3_q, LANG_EQUALIZER_BAND_Q, 10, "eq band 3 q",
                    UNIT_INT, EQ_Q_MIN, EQ_Q_MAX, EQ_Q_STEP,
                    eq_q_format, NULL, NULL),
    INT_SETTING(0, eq_band4_q, LANG_EQUALIZER_BAND_Q, 7, "eq band 4 q",
                    UNIT_INT, EQ_Q_MIN, EQ_Q_MAX, EQ_Q_STEP,
                    eq_q_format, NULL, NULL),
    /* -240..240 (or -24db to +24db) */
    INT_SETTING(0, eq_band0_gain, LANG_GAIN, 0, "eq band 0 gain",
                    UNIT_DB, EQ_GAIN_MIN, EQ_GAIN_MAX, EQ_GAIN_STEP,
                    eq_gain_format, NULL, NULL),
    INT_SETTING(0, eq_band1_gain, LANG_GAIN, 0, "eq band 1 gain",
                    UNIT_DB, EQ_GAIN_MIN, EQ_GAIN_MAX, EQ_GAIN_STEP,
                    eq_gain_format, NULL, NULL),
    INT_SETTING(0, eq_band2_gain, LANG_GAIN, 0, "eq band 2 gain",
                    UNIT_DB, EQ_GAIN_MIN, EQ_GAIN_MAX, EQ_GAIN_STEP,
                    eq_gain_format, NULL, NULL),
    INT_SETTING(0, eq_band3_gain, LANG_GAIN, 0, "eq band 3 gain",
                    UNIT_DB, EQ_GAIN_MIN, EQ_GAIN_MAX, EQ_GAIN_STEP,
                    eq_gain_format, NULL, NULL),
    INT_SETTING(0, eq_band4_gain, LANG_GAIN, 0, "eq band 4 gain",
                    UNIT_DB, EQ_GAIN_MIN, EQ_GAIN_MAX, EQ_GAIN_STEP,
                    eq_gain_format, NULL, NULL),

    /* dithering */
    OFFON_SETTING(0, dithering_enabled, LANG_DITHERING,
        false, "dithering enabled", dsp_dither_enable),
#endif
#ifdef HAVE_DIRCACHE
    OFFON_SETTING(0,dircache,LANG_DIRCACHE_ENABLE,false,"dircache",NULL),
    SYSTEM_SETTING(NVRAM(4),dircache_size,0),
#endif

#ifdef HAVE_TAGCACHE
#ifdef HAVE_TC_RAMCACHE
    OFFON_SETTING(0,tagcache_ram,LANG_TAGCACHE_RAM,false,"tagcache_ram",NULL),
#endif
    OFFON_SETTING(0,tagcache_autoupdate,
        LANG_TAGCACHE_AUTOUPDATE,false,"tagcache_autoupdate",NULL),
#endif
    CHOICE_SETTING(0, default_codepage, LANG_DEFAULT_CODEPAGE, 0,
        "default codepage",
#ifdef HAVE_LCD_BITMAP
        /* The order must match with that in unicode.c */
        "iso8859-1,iso8859-7,iso8859-8,cp1251,iso8859-11,cp1256,"
        "iso8859-9,iso8859-2,sjis,gb2312,ksx1001,big5,utf-8",
        set_codepage, 13,
        ID2P(LANG_CODEPAGE_LATIN1), ID2P(LANG_CODEPAGE_GREEK),
        ID2P(LANG_CODEPAGE_HEBREW), ID2P(LANG_CODEPAGE_CYRILLIC),
        ID2P(LANG_CODEPAGE_THAI), ID2P(LANG_CODEPAGE_ARABIC),
        ID2P(LANG_CODEPAGE_TURKISH), ID2P(LANG_CODEPAGE_LATIN_EXTENDED),
        ID2P(LANG_CODEPAGE_JAPANESE), ID2P(LANG_CODEPAGE_SIMPLIFIED),
        ID2P(LANG_CODEPAGE_KOREAN), ID2P(LANG_CODEPAGE_TRADITIONAL),
        ID2P(LANG_CODEPAGE_UTF8)),
#else /* !HAVE_LCD_BITMAP */
        /* The order must match with that in unicode.c */
        "iso8859-1,iso8859-7,cp1251,iso8859-9,iso8859-2,utf-8",
        set_codepage, 6,
        ID2P(LANG_CODEPAGE_LATIN1), ID2P(LANG_CODEPAGE_GREEK),
        ID2P(LANG_CODEPAGE_CYRILLIC), ID2P(LANG_CODEPAGE_TURKISH),
        ID2P(LANG_CODEPAGE_LATIN_EXTENDED), ID2P(LANG_CODEPAGE_UTF8)),
#endif
    OFFON_SETTING(0,warnon_erase_dynplaylist,
        LANG_WARN_ERASEDYNPLAYLIST_MENU,false,
        "warn when erasing dynamic playlist",NULL),

#ifdef HAVE_BACKLIGHT
#ifdef HAS_BUTTON_HOLD
    CHOICE_SETTING(0, backlight_on_button_hold,
        LANG_BACKLIGHT_ON_BUTTON_HOLD, 0, "backlight on button hold",
        "normal,off,on", backlight_set_on_button_hold, 3,
        ID2P(LANG_NORMAL), ID2P(LANG_OFF), ID2P(LANG_ON)),
#endif

#ifdef HAVE_LCD_SLEEP
    STRINGCHOICE_SETTING(0, lcd_sleep_after_backlight_off,
        LANG_LCD_SLEEP_AFTER_BACKLIGHT_OFF, 3,
        "lcd sleep after backlight off",
        "always,never,5,10,15,20,30,45,60,90", lcd_set_sleep_after_backlight_off,
        10, LANG_ALWAYS, LANG_NEVER, TALK_ID(5, UNIT_SEC), TALK_ID(10, UNIT_SEC),
        TALK_ID(15, UNIT_SEC), TALK_ID(20, UNIT_SEC), TALK_ID(30, UNIT_SEC),
        TALK_ID(45, UNIT_SEC),TALK_ID(60, UNIT_SEC), TALK_ID(90, UNIT_SEC)),
#endif
#endif /* HAVE_BACKLIGHT */

#ifdef HAVE_WM8758
    OFFON_SETTING(0,eq_hw_enabled,LANG_EQUALIZER_HARDWARE_ENABLED,false,
        "eq hardware enabled",NULL),

    STRINGCHOICE_SETTING(0, eq_hw_band0_cutoff, LANG_EQUALIZER_BAND_CUTOFF, 1,
        "eq hardware band 0 cutoff", "80Hz,105Hz,135Hz,175Hz", NULL, 4,
        TALK_ID(80, UNIT_HERTZ), TALK_ID(105, UNIT_HERTZ), 
        TALK_ID(135, UNIT_HERTZ), TALK_ID(175, UNIT_HERTZ)),
    INT_SETTING(0, eq_hw_band0_gain, LANG_GAIN, 0, 
        "eq hardware band 0 gain", UNIT_DB, EQ_HW_GAIN_MIN, 
        EQ_HW_GAIN_MAX, EQ_HW_GAIN_STEP, eq_hw_gain_format, NULL, NULL),

    STRINGCHOICE_SETTING(0, eq_hw_band1_center, LANG_EQUALIZER_BAND_CENTER, 1,
        "eq hardware band 1 center", "230Hz,300Hz,385Hz,500Hz", NULL, 4,
        TALK_ID(230, UNIT_HERTZ), TALK_ID(300, UNIT_HERTZ), 
        TALK_ID(385, UNIT_HERTZ), TALK_ID(500, UNIT_HERTZ)),
    CHOICE_SETTING(0, eq_hw_band1_bandwidth, LANG_EQUALIZER_BANDWIDTH, 0, 
        "eq hardware band 1 bandwidth", "narrow,wide", NULL, 2,
        ID2P(LANG_EQUALIZER_HARDWARE_BANDWIDTH_NARROW), 
        ID2P(LANG_EQUALIZER_HARDWARE_BANDWIDTH_WIDE)),
    INT_SETTING(0, eq_hw_band1_gain, LANG_GAIN, 0, 
        "eq hardware band 1 gain", UNIT_DB, EQ_HW_GAIN_MIN, 
        EQ_HW_GAIN_MAX, EQ_HW_GAIN_STEP, eq_hw_gain_format, NULL, NULL),

    STRINGCHOICE_SETTING(0, eq_hw_band2_center, LANG_EQUALIZER_BAND_CENTER, 1,
        "eq hardware band 2 center", "650Hz,850Hz,1.1kHz,1.4kHz", NULL, 4,
        TALK_ID(650, UNIT_HERTZ), TALK_ID(850, UNIT_HERTZ), 
        TALK_ID(1100, UNIT_HERTZ), TALK_ID(1400, UNIT_HERTZ)),
    CHOICE_SETTING(0, eq_hw_band2_bandwidth, LANG_EQUALIZER_BANDWIDTH, 0, 
        "eq hardware band 2 bandwidth", "narrow,wide", NULL, 2,
        ID2P(LANG_EQUALIZER_HARDWARE_BANDWIDTH_NARROW), 
        ID2P(LANG_EQUALIZER_HARDWARE_BANDWIDTH_WIDE)),
    INT_SETTING(0, eq_hw_band2_gain, LANG_GAIN, 0, 
        "eq hardware band 2 gain", UNIT_DB, EQ_HW_GAIN_MIN, 
        EQ_HW_GAIN_MAX, EQ_HW_GAIN_STEP, eq_hw_gain_format, NULL, NULL),

    STRINGCHOICE_SETTING(0, eq_hw_band3_center, LANG_EQUALIZER_BAND_CENTER, 1,
        "eq hardware band 3 center", "1.8kHz,2.4kHz,3.2kHz,4.1kHz", NULL, 4,
        TALK_ID(1800, UNIT_HERTZ), TALK_ID(2400, UNIT_HERTZ), 
        TALK_ID(3200, UNIT_HERTZ), TALK_ID(4100, UNIT_HERTZ)),
    CHOICE_SETTING(0, eq_hw_band3_bandwidth, LANG_EQUALIZER_BANDWIDTH, 0, 
        "eq hardware band 3 bandwidth", "narrow,wide", NULL, 2,
        ID2P(LANG_EQUALIZER_HARDWARE_BANDWIDTH_NARROW), 
        ID2P(LANG_EQUALIZER_HARDWARE_BANDWIDTH_WIDE)),
    INT_SETTING(0, eq_hw_band3_gain, LANG_GAIN, 0, 
        "eq hardware band 3 gain", UNIT_DB, EQ_HW_GAIN_MIN, 
        EQ_HW_GAIN_MAX, EQ_HW_GAIN_STEP, eq_hw_gain_format, NULL, NULL),

    STRINGCHOICE_SETTING(0, eq_hw_band4_cutoff, LANG_EQUALIZER_BAND_CUTOFF, 1,
        "eq hardware band 4 cutoff", "5.3kHz,6.9kHz,9kHz,11.7kHz", NULL, 4,
        TALK_ID(5300, UNIT_HERTZ), TALK_ID(6900, UNIT_HERTZ), 
        TALK_ID(9000, UNIT_HERTZ), TALK_ID(11700, UNIT_HERTZ)),
    INT_SETTING(0, eq_hw_band4_gain, LANG_GAIN, 0, 
        "eq hardware band 4 gain", UNIT_DB, EQ_HW_GAIN_MIN, 
        EQ_HW_GAIN_MAX, EQ_HW_GAIN_STEP, eq_hw_gain_format, NULL, NULL),
#endif

    OFFON_SETTING(0,hold_lr_for_scroll_in_list,-1,true,
        "hold_lr_for_scroll_in_list",NULL),
    CHOICE_SETTING(0, show_path_in_browser, LANG_SHOW_PATH, SHOW_PATH_OFF,
        "show path in browser", "off,current directory,full path", NULL, 3,
        ID2P(LANG_OFF), ID2P(LANG_SHOW_PATH_CURRENT), ID2P(LANG_DISPLAY_FULL_PATH)),

#ifdef HAVE_AGC
    {F_T_INT,&global_settings.rec_agc_preset_mic,LANG_RECORD_AGC_PRESET,INT(1),
        "agc mic preset",NULL,UNUSED},
    {F_T_INT,&global_settings.rec_agc_preset_line,LANG_RECORD_AGC_PRESET,INT(1),
        "agc line preset",NULL,UNUSED},
    {F_T_INT,&global_settings.rec_agc_maxgain_mic,-1,INT(104),
        "agc maximum mic gain",NULL,UNUSED},
    {F_T_INT,&global_settings.rec_agc_maxgain_line,-1,INT(96),
        "agc maximum line gain",NULL,UNUSED},
    {F_T_INT,&global_settings.rec_agc_cliptime,LANG_RECORD_AGC_CLIPTIME,INT(1),
        "agc cliptime","0.2s,0.4s,0.6s,0.8,1s",UNUSED},
#endif

#ifdef HAVE_REMOTE_LCD
#ifdef HAS_REMOTE_BUTTON_HOLD
    CHOICE_SETTING(0, remote_backlight_on_button_hold,
        LANG_BACKLIGHT_ON_BUTTON_HOLD, 0, "remote backlight on button hold",
        "normal,off,on", remote_backlight_set_on_button_hold, 3,
        ID2P(LANG_NORMAL), ID2P(LANG_OFF), ID2P(LANG_ON)),
#endif
#endif
#ifdef HAVE_HEADPHONE_DETECTION
    CHOICE_SETTING(0, unplug_mode, LANG_HEADPHONE_UNPLUG, 0,
        "pause on headphone unplug", "off,pause,pause and resume", NULL, 3,
        ID2P(LANG_OFF), ID2P(LANG_PAUSE), ID2P(LANG_HEADPHONE_UNPLUG_RESUME)),
    INT_SETTING(0, unplug_rw, LANG_HEADPHONE_UNPLUG_RW, 0, "rewind duration on pause",
                    UNIT_SEC, 0, 15, 1, NULL, NULL,NULL) ,
    OFFON_SETTING(0,unplug_autoresume,LANG_HEADPHONE_UNPLUG_DISABLE_AUTORESUME,false,
        "disable autoresume if phones not present",NULL),
#endif
#if CONFIG_TUNER
    CHOICE_SETTING(0, fm_region, LANG_FM_REGION, 0, 
        "fm_region", "eu,us,jp,kr", set_radio_region, 4, ID2P(LANG_FM_EUROPE),
        ID2P(LANG_FM_US), ID2P(LANG_FM_JAPAN), ID2P(LANG_FM_KOREA)),
#endif

    OFFON_SETTING(0,audioscrobbler,LANG_AUDIOSCROBBLER,
        false,"Last.fm Logging",NULL),

#ifdef HAVE_RECORDING
    {F_T_INT|F_RECSETTING,&global_settings.rec_trigger_type,
        LANG_RECORD_TRIGGER_TYPE,
        INT(0),"trigger type","stop,pause,nf stp",UNUSED},
#endif

    /** settings not in the old config blocks **/
#if CONFIG_TUNER
    FILENAME_SETTING(0, fmr_file, "fmr",
        "", FMPRESET_PATH "/", ".fmr", MAX_FILENAME+1),
#endif
    FILENAME_SETTING(F_THEMESETTING, font_file, "font",
        "", FONT_DIR "/", ".fnt", MAX_FILENAME+1),
    FILENAME_SETTING(F_THEMESETTING,wps_file, "wps",
        "", WPS_DIR "/", ".wps", MAX_FILENAME+1),
    FILENAME_SETTING(0,lang_file,"lang","",LANG_DIR "/",".lng",MAX_FILENAME+1),
#ifdef HAVE_REMOTE_LCD
    FILENAME_SETTING(F_THEMESETTING,rwps_file,"rwps",
        "", WPS_DIR "/", ".rwps", MAX_FILENAME+1),
#endif
#if LCD_DEPTH > 1
    FILENAME_SETTING(F_THEMESETTING,backdrop_file,"backdrop",
        "", BACKDROP_DIR "/", ".bmp", MAX_FILENAME+1),
#endif
#ifdef HAVE_LCD_BITMAP
    FILENAME_SETTING(0,kbd_file,"kbd","",ROCKBOX_DIR "/",".kbd",MAX_FILENAME+1),
#endif
#ifdef HAVE_USB_POWER
#if CONFIG_CHARGING
    OFFON_SETTING(0,usb_charging,LANG_USB_CHARGING,false,"usb charging",NULL),
#endif
#endif
    OFFON_SETTING(0,cuesheet,LANG_CUESHEET_ENABLE,false,"cuesheet support", NULL),
    CHOICE_SETTING(0, start_in_screen, LANG_START_SCREEN, 1, 
            "start in screen", "previous,root,files,db,wps,menu,"
#ifdef HAVE_RECORDING
            "recording,"
#endif
#if CONFIG_TUNER
            "radio,"
#endif
            "bookmarks"    ,NULL, 
#if defined(HAVE_RECORDING) && CONFIG_TUNER
            9,
#elif defined(HAVE_RECORDING) || CONFIG_TUNER /* only one of them */
            8,
#else 
            7,
#endif
            ID2P(LANG_PREVIOUS_SCREEN), ID2P(LANG_MAIN_MENU),
            ID2P(LANG_DIR_BROWSER), ID2P(LANG_TAGCACHE),
            ID2P(LANG_RESUME_PLAYBACK), ID2P(LANG_SETTINGS),
#ifdef HAVE_RECORDING
            ID2P(LANG_RECORDING),
#endif
#if CONFIG_TUNER
            ID2P(LANG_FM_RADIO),
#endif
            ID2P(LANG_BOOKMARK_MENU_RECENT_BOOKMARKS)
            ),
    SYSTEM_SETTING(NVRAM(1),last_screen,-1),
#if defined(HAVE_RTC_ALARM) && \
    (defined(HAVE_RECORDING) || CONFIG_TUNER)
    {F_T_INT, &global_settings.alarm_wake_up_screen, LANG_ALARM_WAKEUP_SCREEN,
        INT(ALARM_START_WPS), "alarm wakeup screen", ALARM_SETTING_TEXT, UNUSED},
#endif /* HAVE_RTC_ALARM */
            
    /* Customizable icons */
#ifdef HAVE_LCD_BITMAP
    FILENAME_SETTING(F_THEMESETTING, icon_file, "iconset", "",
                     ICON_DIR "/", ".bmp", MAX_FILENAME+1),
    FILENAME_SETTING(F_THEMESETTING, viewers_icon_file, "viewers iconset", "",
                     ICON_DIR "/", ".bmp", MAX_FILENAME+1),
#endif
#ifdef HAVE_REMOTE_LCD
    FILENAME_SETTING(F_THEMESETTING, remote_icon_file, "remote iconset", "",
                     ICON_DIR "/", ".bmp", MAX_FILENAME+1),
    FILENAME_SETTING(F_THEMESETTING, remote_viewers_icon_file,
                     "remote viewers iconset", "",
                     ICON_DIR "/", ".bmp", MAX_FILENAME+1),
#endif /* HAVE_REMOTE_LCD */
#ifdef HAVE_LCD_COLOR
    FILENAME_SETTING(F_THEMESETTING, colors_file, "filetype colours", "",
                     THEME_DIR "/", ".colours", MAX_FILENAME+1),
#endif
#ifdef HAVE_BUTTON_LIGHT
    INT_SETTING_W_CFGVALS(F_FLIPLIST, button_light_timeout,
                          LANG_BUTTONLIGHT_TIMEOUT, 6,
        "button light timeout", backlight_times_conf, UNIT_SEC,
        0, 18, 1, backlight_formatter, backlight_getlang, 
        button_backlight_set_timeout),
#endif
#ifdef HAVE_BUTTONLIGHT_BRIGHTNESS
    INT_SETTING(0, buttonlight_brightness, LANG_BUTTONLIGHT_BRIGHTNESS, DEFAULT_BRIGHTNESS_SETTING,
        "button light brightness",UNIT_INT, MIN_BRIGHTNESS_SETTING, MAX_BRIGHTNESS_SETTING, 1,
        NULL, NULL, buttonlight_set_brightness),
#endif
#ifndef HAVE_SCROLLWHEEL
    INT_SETTING(0, list_accel_start_delay, LANG_LISTACCEL_START_DELAY,
                2, "list_accel_start_delay", UNIT_MS, 0, 10, 1,
                listaccel_formatter, listaccel_getlang, NULL),
    INT_SETTING(0, list_accel_wait, LANG_LISTACCEL_ACCEL_SPEED,
                3, "list_accel_wait", UNIT_SEC, 1, 10, 1, 
                scanaccel_formatter, scanaccel_getlang, NULL),
#endif /* HAVE_SCROLLWHEEL */
};

const int nb_settings = sizeof(settings)/sizeof(*settings);

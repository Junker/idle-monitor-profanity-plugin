/*
 * idle_monitor - Profanity plugin that switches to the console window
 * when the X server has been idle for more than a configurable number
 * of minutes.
 */

#include <stdio.h>
#include <glib.h>
#include <profapi.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                         */
/* ------------------------------------------------------------------ */

#define SETTINGS_GROUP        "idle_monitor"
#define DEFAULT_IDLE_MINUTES   5
#define MIN_IDLE_MINUTES       1
#define MAX_IDLE_MINUTES       1440
#define POLL_INTERVAL_SECONDS  10

/* ------------------------------------------------------------------ */
/*  Data                                                              */
/* ------------------------------------------------------------------ */

static gboolean enabled        = TRUE;
static gint      idle_minutes  = DEFAULT_IDLE_MINUTES;

/* ------------------------------------------------------------------ */
/*  X11 idle time                                                     */
/* ------------------------------------------------------------------ */

/* Return milliseconds of X server idle time, or -1 on failure. */
static long get_xorg_idle_ms(void) {
    Display *display = XOpenDisplay(NULL);
    if (!display)
        return -1;

    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    if (!info) {
        XCloseDisplay(display);
        return -1;
    }

    long idle_ms = -1;
    if (XScreenSaverQueryInfo(display, DefaultRootWindow(display), info))
        idle_ms = (long)info->idle;

    XFree(info);
    XCloseDisplay(display);
    return idle_ms;
}

/* ------------------------------------------------------------------ */
/*  Settings                                                          */
/* ------------------------------------------------------------------ */

static void settings_load(void) {
    enabled = prof_settings_boolean_get(SETTINGS_GROUP, "enabled", TRUE);
    idle_minutes = CLAMP(prof_settings_int_get(SETTINGS_GROUP, "minutes", DEFAULT_IDLE_MINUTES),
                         MIN_IDLE_MINUTES, MAX_IDLE_MINUTES);
}

static void settings_save(void) {
    prof_settings_boolean_set(SETTINGS_GROUP, "enabled", enabled);
    prof_settings_int_set(SETTINGS_GROUP, "minutes", idle_minutes);
}

/* ------------------------------------------------------------------ */
/*  Timed callback                                                    */
/* ------------------------------------------------------------------ */

static void idle_check_cb(void) {
    if (!enabled)
        return;

    long idle_ms = get_xorg_idle_ms();
    if (idle_ms < 0)
        return;   /* X server not available — nothing to do */

    long threshold_ms = (long)idle_minutes * 60 * 1000;

    if (idle_ms >= threshold_ms && !prof_current_win_is_console())
        prof_send_line("/win console");
}

/* ------------------------------------------------------------------ */
/*  Subcommand handlers                                               */
/* ------------------------------------------------------------------ */

static void cmd_handle_enabled(char **args) {
    if (args && args[0]) {
        if (g_str_equal(args[0], "on")) {
            enabled = TRUE;
            settings_save();
            prof_cons_show("idle_monitor: enabled");
        } else if (g_str_equal(args[0], "off")) {
            enabled = FALSE;
            settings_save();
            prof_cons_show("idle_monitor: disabled");
        } else {
            prof_cons_bad_cmd_usage("/idle-monitor");
        }
    } else {
        g_autofree gchar *msg = g_strdup_printf(
            "idle_monitor: %s", enabled ? "enabled" : "disabled");
        prof_cons_show(msg);
    }
}

static void cmd_handle_minutes(char **args) {
    if (args && args[0]) {
        gchar *endptr = NULL;
        gint64 val = g_ascii_strtoll(args[0], &endptr, 10);
        if (endptr == args[0] || *endptr != '\0'
            || val < MIN_IDLE_MINUTES || val > MAX_IDLE_MINUTES) {
            g_autofree gchar *msg = g_strdup_printf(
                "idle_monitor: minutes must be between %d and %d",
                MIN_IDLE_MINUTES, MAX_IDLE_MINUTES);
            prof_cons_show(msg);
            return;
        }
        idle_minutes = (gint)val;
        settings_save();
        g_autofree gchar *msg = g_strdup_printf(
            "idle_monitor: idle threshold set to %d minute%s",
            idle_minutes, idle_minutes == 1 ? "" : "s");
        prof_cons_show(msg);
    } else if (idle_minutes == 1) {
        prof_cons_show("idle_monitor: idle threshold is 1 minute");
    } else {
        g_autofree gchar *msg = g_strdup_printf(
            "idle_monitor: idle threshold is %d minutes", idle_minutes);
        prof_cons_show(msg);
    }
}

static void cmd_handle_status(G_GNUC_UNUSED char **args) {
    prof_cons_show("idle_monitor settings:");
    g_autofree gchar *s_enabled = g_strdup_printf("  enabled:  %s", enabled ? "ON" : "OFF");
    prof_cons_show(s_enabled);
    g_autofree gchar *s_minutes = g_strdup_printf("  minutes:  %d", idle_minutes);
    prof_cons_show(s_minutes);
    long idle_ms = get_xorg_idle_ms();
    if (idle_ms >= 0) {
        g_autofree gchar *s_idle = g_strdup_printf("  current:  %ld ms idle", idle_ms);
        prof_cons_show(s_idle);
    } else {
        prof_cons_show("  current:  (X server not available)");
    }
}

/* ------------------------------------------------------------------ */
/*  /idle-monitor command dispatch                                   */
/* ------------------------------------------------------------------ */

typedef void (*cmd_handler_fn)(char **args);

typedef struct {
    const gchar *name;
    cmd_handler_fn handler;
} CmdDispatch;

static const CmdDispatch cmd_dispatch[] = {
    { "status",   cmd_handle_status   },
    { "on",       cmd_handle_enabled },
    { "off",      cmd_handle_enabled },
    { "minutes",  cmd_handle_minutes  },
    { NULL, NULL }
};

static void idle_command_cb(char **args) {
    if (args && args[0]) {
        for (const CmdDispatch *d = cmd_dispatch; d->name; d++) {
            if (g_str_equal(args[0], d->name)) {
                d->handler(args + 1);
                return;
            }
        }
        g_autofree gchar *msg = g_strdup_printf(
            "idle_monitor: unknown subcommand \"%s\"", args[0]);
        prof_cons_show(msg);
        prof_cons_bad_cmd_usage("/idle-monitor");
        return;
    }
    cmd_handle_status(NULL);
}

/* ------------------------------------------------------------------ */
/*  Plugin lifecycle                                                  */
/* ------------------------------------------------------------------ */

void prof_init(G_GNUC_UNUSED const char *const version,
               G_GNUC_UNUSED const char *const status,
               G_GNUC_UNUSED const char *const account_name,
               G_GNUC_UNUSED const char *const fulljid) {
    settings_load();

    static char *synopsis[] = {
        "/idle-monitor",
        "/idle-monitor status",
        "/idle-monitor on",
        "/idle-monitor off",
        "/idle-monitor minutes <N>",
        NULL
    };

    static char *description = "Switch to the console window when the X server "
        "has been idle for more than the configured number of minutes. "
        "The idle threshold is adjusted with /idle-monitor minutes (default: 5). "
        "Monitoring can be toggled with /idle-monitor on and /idle-monitor off. "
        "Current settings and live idle time are shown with /idle-monitor status.";

    static char *arguments[][2] = {
        { "status",        "Show current idle_monitor settings and live idle time" },
        { "on",            "Enable idle monitoring (default)" },
        { "off",           "Disable idle monitoring" },
        { "minutes <N>",   "Idle threshold in minutes (default: 5, range 1-1440)" },
        { NULL, NULL }
    };

    static char *examples[] = {
        "/idle-monitor",
        "/idle-monitor status",
        "/idle-monitor minutes 10",
        "/idle-monitor off",
        NULL
    };
    prof_register_command("/idle-monitor", 0, 2, synopsis, description, arguments, examples, idle_command_cb);

    static char *subcmds[] = { "status", "on", "off", "minutes", NULL };
    prof_completer_add("/idle-monitor", subcmds);

    prof_register_timed(idle_check_cb, POLL_INTERVAL_SECONDS);
}

void prof_on_start(void) {
    settings_load();
}

void prof_on_unload(void) {
    settings_save();

    static const gchar *completions[] = { "/idle-monitor", NULL };
    for (int i = 0; completions[i]; i++)
        prof_completer_clear(completions[i]);
}

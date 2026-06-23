/*
 * idle_monitor - Profanity plugin that switches to the console window
 * when the desktop has been idle for more than a configurable number
 * of minutes.
 */

#include <glib.h>
#include <profapi.h>
#include <strophe.h>

#ifdef HAVE_LIBXSS
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>
#endif

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
static gboolean heuristic     = FALSE;
static gint      idle_minutes  = DEFAULT_IDLE_MINUTES;

/* Timer counting up since the last observed Profanity activity. */
static GTimer *heuristic_timer = NULL;

/* libstrophe context used to parse outgoing message stanzas. */
static xmpp_ctx_t *strophe_ctx = NULL;

/* X window id of the terminal running Profanity (from $WINDOWID).
 * Used as an extra activity signal: when the terminal is the focused
 * X window we reset the heuristic idle timer. None if unavailable. */
#ifdef HAVE_LIBXSS
static Window term_window = None;
#endif

/* ------------------------------------------------------------------ */
/*  Forward declarations                                              */
/* ------------------------------------------------------------------ */

static void idle_check_cb(void);
static void reset_heuristic_idle(void);

/* ------------------------------------------------------------------ */
/*  Xorg idle time                                                    */
/* ------------------------------------------------------------------ */

/* Return milliseconds of X server idle time, or -1 if unavailable. */
static long get_xorg_idle_ms(void) {
#ifdef HAVE_LIBXSS
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
#else
    return -1;   /* built without libxss */
#endif
}

/* ------------------------------------------------------------------ */
/*  Heuristic (Profanity-only) idle time                              */
/* ------------------------------------------------------------------ */

static long get_heuristic_idle_ms(void) {
    if (!heuristic_timer)
        return -1;
    return (long)(g_timer_elapsed(heuristic_timer, NULL) * 1000.0);
}

static void reset_heuristic_idle(void) {
    if (heuristic_timer)
        g_timer_start(heuristic_timer);
}

/* ------------------------------------------------------------------ */
/*  Terminal focus check ($WINDOWID                                   */
/*                                                                    */
/*  Many terminal emulators export $WINDOWID — the X window id of the */
/*  terminal running Profanity. By polling XGetInputFocus we can tell */
/*  whether the terminal window itself has focus. If it does, the user*/
/*  is actively looking at Profanity even without having sent a       */
/*  message, so we reset the heuristic idle timer. This needs no event*/
/*  loop and runs harmlessly in the existing timed callback.          */
/* ------------------------------------------------------------------ */

#ifdef HAVE_LIBXSS
static void load_term_window(void) {
    const gchar *wid = g_getenv("WINDOWID");
    if (wid && *wid) {
        guint64 val = g_ascii_strtoull(wid, NULL, 0);
        if (val != 0)
            term_window = (Window)val;
    }
}

/* Return TRUE if the terminal window ($WINDOWID) currently has the X
 * input focus. */
static gboolean terminal_is_focused(void) {
    if (term_window == None)
        return FALSE;

    Display *display = XOpenDisplay(NULL);
    if (!display)
        return FALSE;

    Window focused_win = None;
    int revert = 0;
    XGetInputFocus(display, &focused_win, &revert);

    gboolean focused = (focused_win == term_window);

    XCloseDisplay(display);
    return focused;
}
#endif

/* Return TRUE if an outgoing <message> stanza reflects user activity:
 * a real body or a chat-state notification (composing/active/paused). */
static gboolean stanza_indicates_activity(const char *xml) {
    if (!xml || !*xml || !strophe_ctx)
        return FALSE;

    xmpp_stanza_t *stanza = xmpp_stanza_new_from_string(strophe_ctx, xml);
    if (!stanza)
        return FALSE;

    gboolean activity = FALSE;
    if (xmpp_stanza_get_child_by_name(stanza, "body"))
        activity = TRUE;

    if (!activity) {
        const char *states[] = { "composing", "active", "paused", NULL };
        for (gint i = 0; states[i] && !activity; i++) {
            if (xmpp_stanza_get_child_by_name(stanza, states[i]))
                activity = TRUE;
        }
    }

    xmpp_stanza_release(stanza);
    return activity;
}

/* ------------------------------------------------------------------ */
/*  Settings                                                          */
/* ------------------------------------------------------------------ */

static void settings_load(void) {
    enabled       = prof_settings_boolean_get(SETTINGS_GROUP, "enabled",   TRUE);
    heuristic     = prof_settings_boolean_get(SETTINGS_GROUP, "heuristic", FALSE);
    idle_minutes  = CLAMP(prof_settings_int_get(SETTINGS_GROUP, "minutes", DEFAULT_IDLE_MINUTES),
                          MIN_IDLE_MINUTES, MAX_IDLE_MINUTES);
}

static void settings_save(void) {
    prof_settings_boolean_set(SETTINGS_GROUP, "enabled",   enabled);
    prof_settings_boolean_set(SETTINGS_GROUP, "heuristic", heuristic);
    prof_settings_int_set(SETTINGS_GROUP, "minutes", idle_minutes);
}

/* ------------------------------------------------------------------ */
/*  Timed callback                                                    */
/* ------------------------------------------------------------------ */

static void idle_check_cb(void) {
    if (!enabled)
        return;

    /* If the heuristic source is on and the terminal is the focused X
     * window, the user is looking at Profanity — reset the heuristic idle
     * timer before sampling it. */
#ifdef HAVE_LIBXSS
    if (heuristic && terminal_is_focused())
        reset_heuristic_idle();
#endif

    long xorg_ms      = get_xorg_idle_ms();
    long heuristic_ms = heuristic ? get_heuristic_idle_ms() : -1;
    long threshold_ms = (long)idle_minutes * 60 * 1000;

    g_autofree gchar *dbg = g_strdup_printf(
        "idle_monitor: xorg %ld ms, app %ld ms (threshold %ld ms)",
        xorg_ms, heuristic_ms, threshold_ms);
    prof_log_debug(dbg);

    gboolean xorg_idle = (xorg_ms >= 0 && xorg_ms >= threshold_ms);
    gboolean heuristic_idle_flag  = (heuristic_ms  >= 0 && heuristic_ms  >= threshold_ms);

    if ((xorg_idle || heuristic_idle_flag) && !prof_current_win_is_console())
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

static void cmd_handle_heuristic(char **args) {
    if (args && args[0]) {
        if (g_str_equal(args[0], "on")) {
            heuristic = TRUE;
            reset_heuristic_idle();
            settings_save();
            prof_cons_show("idle_monitor: heuristic idle source enabled");
        } else if (g_str_equal(args[0], "off")) {
            heuristic = FALSE;
            settings_save();
            prof_cons_show("idle_monitor: heuristic idle source disabled");
        } else {
            prof_cons_bad_cmd_usage("/idle-monitor");
        }
    } else {
        g_autofree gchar *msg = g_strdup_printf(
            "idle_monitor: heuristic idle source is %s",
            heuristic ? "on" : "off");
        prof_cons_show(msg);
    }
}

static void cmd_handle_timeout(char **args) {
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
    g_autofree gchar *s_enabled   = g_strdup_printf("  enabled:    %s", enabled ? "ON" : "OFF");
    prof_cons_show(s_enabled);
    g_autofree gchar *s_heuristic = g_strdup_printf("  heuristic:  %s", heuristic ? "ON" : "OFF");
    prof_cons_show(s_heuristic);
    g_autofree gchar *s_minutes   = g_strdup_printf("  minutes:    %d", idle_minutes);
    prof_cons_show(s_minutes);

    long xorg_ms = get_xorg_idle_ms();
    if (xorg_ms >= 0) {
        g_autofree gchar *s = g_strdup_printf("  xorg idle:  %ld ms", xorg_ms);
        prof_cons_show(s);
    } else {
        prof_cons_show("  xorg idle:  (unavailable)");
    }

    long heuristic_ms = get_heuristic_idle_ms();
    if (heuristic_ms >= 0) {
        g_autofree gchar *s = g_strdup_printf("  heuristic idle:   %ld ms", heuristic_ms);
        prof_cons_show(s);
    } else {
        prof_cons_show("  heuristic idle:   (heuristic off)");
    }

#ifdef HAVE_LIBXSS
    if (term_window != None)
        prof_cons_show("  terminal:   focused window check active");
    else
        prof_cons_show("  terminal:   ($WINDOWID not set)");
#else
    prof_cons_show("  terminal:   (built without libxss)");
#endif
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
    { "status",    cmd_handle_status    },
    { "on",        cmd_handle_enabled   },
    { "off",       cmd_handle_enabled   },
    { "heuristic", cmd_handle_heuristic },
    { "timeout",   cmd_handle_timeout   },
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
/*  Activity hooks (discovered by Profanity via dlsym)                */
/*                                                                       */
/*  These fire on user-initiated Profanity actions and reset the app    */
/*  idle timer, which is the basis of the heuristic idle source.       */
/* ------------------------------------------------------------------ */

void prof_post_chat_message_send(G_GNUC_UNUSED const char *const barejid,
                                 G_GNUC_UNUSED const char *const message) {
    reset_heuristic_idle();
}

void prof_post_room_message_send(G_GNUC_UNUSED const char *const barejid,
                                 G_GNUC_UNUSED const char *const message) {
    reset_heuristic_idle();
}

void prof_post_priv_message_send(G_GNUC_UNUSED const char *const barejid,
                                 G_GNUC_UNUSED const char *const nick,
                                 G_GNUC_UNUSED const char *const message) {
    reset_heuristic_idle();
}

void prof_on_chat_win_focus(G_GNUC_UNUSED const char *const barejid) {
    reset_heuristic_idle();
}

void prof_on_room_win_focus(G_GNUC_UNUSED const char *const barejid) {
    reset_heuristic_idle();
}

/* Observe outgoing <message> stanzas. Reset heuristic idle when the stanza
 * carries a body (a message you sent) or a chat-state notification
 * (typing indicator). */
char *prof_on_message_stanza_send(const char *const text) {
    if (stanza_indicates_activity(text))
        reset_heuristic_idle();
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Plugin lifecycle                                                  */
/* ------------------------------------------------------------------ */

void prof_init(G_GNUC_UNUSED const char *const version,
               G_GNUC_UNUSED const char *const status,
               G_GNUC_UNUSED const char *const account_name,
               G_GNUC_UNUSED const char *const fulljid) {
    settings_load();

    heuristic_timer = g_timer_new();      /* created and started */
    strophe_ctx = xmpp_ctx_new(NULL, NULL);
#ifdef HAVE_LIBXSS
    load_term_window();
#endif

    static char *synopsis[] = {
        "/idle-monitor",
        "/idle-monitor status",
        "/idle-monitor on",
        "/idle-monitor off",
        "/idle-monitor heuristic on|off",
        "/idle-monitor timeout <N>",
        NULL
    };

    static char *description = "Switch to the console window when the desktop "
        "has been idle for more than the configured number of minutes. "
        "By default Xorg idle time (last keyboard/mouse input) is used. "
        "A heuristic Profanity-only idle source — reset when you send "
        "messages, focus windows or type (chat-state) — can be enabled too. "
        "The idle threshold is set with /idle-monitor timeout (default: 5). "
        "Monitoring is toggled with /idle-monitor on and /idle-monitor off. "
        "Use /idle-monitor status to see current settings and live idle.";

    static char *arguments[][2] = {
        { "status",          "Show current settings and live idle time" },
        { "on",              "Enable idle monitoring (default)" },
        { "off",             "Disable idle monitoring" },
        { "heuristic on|off", "Toggle Profanity-only idle source (default: off)" },
        { "timeout <N>",     "Idle threshold in minutes (default: 5, range 1-1440)" },
        { NULL, NULL }
    };

    static char *examples[] = {
        "/idle-monitor",
        "/idle-monitor status",
        "/idle-monitor timeout 10",
        "/idle-monitor heuristic on",
        "/idle-monitor off",
        NULL
    };
    prof_register_command("/idle-monitor", 0, 2, synopsis, description, arguments, examples, idle_command_cb);

    static char *subcmds[] = { "status", "on", "off", "heuristic", "timeout", NULL };
    prof_completer_add("/idle-monitor", subcmds);
    static char *on_off[] = { "on", "off", NULL };
    prof_completer_add("/idle-monitor heuristic", on_off);

    prof_register_timed(idle_check_cb, POLL_INTERVAL_SECONDS);
}

void prof_on_start(void) {
    settings_load();

    if (!heuristic_timer)
        heuristic_timer = g_timer_new();
    if (!strophe_ctx)
        strophe_ctx = xmpp_ctx_new(NULL, NULL);
#ifdef HAVE_LIBXSS
    if (term_window == None)
        load_term_window();
#endif
}

void prof_on_unload(void) {
    settings_save();

    static const gchar *completions[] = { "/idle-monitor", "/idle-monitor heuristic", NULL };
    for (int i = 0; completions[i]; i++)
        prof_completer_clear(completions[i]);

    g_clear_pointer(&strophe_ctx, xmpp_ctx_free);
    g_clear_pointer(&heuristic_timer, g_timer_destroy);
}

# idle_monitor Profanity plugin

A [Profanity](https://profanity.im/) C plugin that switches to the console
window when your **desktop** has been idle for longer than a configurable
number of minutes.

## Commands

| Command | Description |
|---------|-------------|
| `/idle-monitor` | Show current settings and live idle time |
| `/idle-monitor status` | Show current settings and live idle time |
| `/idle-monitor on` | Enable idle monitoring (default) |
| `/idle-monitor off` | Disable idle monitoring |
| `/idle-monitor heuristic on\|off` | Toggle the Profanity-only idle source (default: off) |
| `/idle-monitor timeout <N>` | Set idle threshold in minutes (default: 5, range 1-1440) |

Tab-completion:

```
/idle-monitor <Tab>            → status, on, off, heuristic, timeout
/idle-monitor heuristic <Tab> → on, off
```

## Building

Requires GLib and libstrophe development headers. The X11 Screen Saver
extension (`libxss`) is **optional**: if present, the Xorg idle source is built
(`HAVE_LIBXSS`); if absent, the plugin builds in heuristic-only mode.

```bash
make
```

This produces `build/idle_monitor.so`.

## Installation

Install the plugin to your local Profanity plugins directory:

```bash
make install
```

This copies the plugin to `~/.local/share/profanity/plugins/`.

## Usage

Load the plugin in Profanity:

```
/plugins load idle_monitor
```

The plugin starts monitoring immediately: every 10 seconds it checks the idle
sources and, if the threshold is exceeded, switches to the console window.

### Settings examples

```
/idle-monitor                → show current settings and live idle time
/idle-monitor timeout 10     → switch after 10 minutes of idle
/idle-monitor heuristic on   → also use Profanity-only idle
/idle-monitor off            → pause monitoring
/idle-monitor on             → resume monitoring
```

Settings are persisted automatically and survive restarts.

## Uninstallation

```bash
rm ~/.local/share/profanity/plugins/idle_monitor.so
```
## How it works

Two idle sources are available and can run alongside each other:

- **Xorg idle** (default) — time since the last keyboard or mouse input on the
  X server, read via the X11 Screen Saver extension (`libxss`). This works only
  on **Xorg**.
- **Heuristic idle** — a Profanity-only timer that resets whenever you send a
  message, focus a chat/room window, or emit an outgoing message stanza
  carrying a `<body>` or a typing (chat-state) notification. When built with
  `libxss` it also resets whenever the terminal running Profanity (detected via
  the `$WINDOWID` environment variable) is the focused X window, so merely
  looking at Profanity counts as activity. This detects "user is no longer
  using Profanity" even while the rest of the desktop is active. Enabled with
  `/idle-monitor heuristic on`.

The console window switch fires if **either** source exceeds the configured
threshold and the console is not already the active window.

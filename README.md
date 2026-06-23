# idle_monitor Profanity plugin

A [Profanity](https://profanity.im/) C plugin that switches to the console
window when your **desktop** has been idle for longer than N minutes. 
Idle time is the time since the last keyboard or mouse input — not
just Profanity activity.
It works only on **Xorg** (it uses the X11 Screen Saver extension to read the
idle time).

## Commands

| Command | Description |
|---------|-------------|
| `/idle-monitor` | Show current settings and live idle time |
| `/idle-monitor status` | Show current settings and live idle time |
| `/idle-monitor on` | Enable idle monitoring (default) |
| `/idle-monitor off` | Disable idle monitoring |
| `/idle-monitor minutes <N>` | Set idle threshold in minutes (default: 5, range 1-1440) |

Tab-completion:

```
/idle-monitor <Tab>   → status, on, off, minutes
```

## Building

Requires GLib, Xlib and the X11 Screen Saver extension (`libXss`) development
headers.

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

The plugin starts monitoring immediately: every 10 seconds it queries the X
server idle time and, if it exceeds the configured threshold, switches to the
console window.

### Settings examples

```
/idle-monitor              → show current settings and live idle time
/idle-monitor minutes 10   → switch after 10 minutes of X idle
/idle-monitor off          → pause monitoring
/idle-monitor on           → resume monitoring
```

Settings are persisted automatically and survive restarts.

## Uninstallation

```bash
rm ~/.local/share/profanity/plugins/idle_monitor.so
```

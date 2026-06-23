PLUGIN_NAME = idle_monitor
SRC = idle_monitor.c
BUILD_DIR = build
INSTALL_DIR = $(HOME)/.local/share/profanity/plugins

CC ?= gcc
CFLAGS ?= -Wall -Wextra
PROFANITY_LIBS = "-lprofanity"

GLIB_CFLAGS = $(shell pkg-config --cflags glib-2.0 2>/dev/null || echo "")
GLIB_LIBS = $(shell pkg-config --libs glib-2.0 2>/dev/null || echo "-lglib-2.0")

STROPHE_CFLAGS = $(shell pkg-config --cflags libstrophe 2>/dev/null || echo "")
STROPHE_LIBS = $(shell pkg-config --libs libstrophe 2>/dev/null || echo "-lstrophe")

# libxss (Xorg idle time) is optional — without it only the heuristic
# Profanity heuristic idle source is available.
XSS_OK := $(shell pkg-config --exists x11 xscrnsaver 2>/dev/null && echo yes)
ifeq ($(XSS_OK),yes)
X11_CFLAGS = $(shell pkg-config --cflags x11 xscrnsaver) -DHAVE_LIBXSS
X11_LIBS = $(shell pkg-config --libs x11 xscrnsaver)
else
X11_CFLAGS =
X11_LIBS =
endif

ALL_CFLAGS = $(CFLAGS) $(GLIB_CFLAGS) $(STROPHE_CFLAGS) $(X11_CFLAGS)
ALL_LIBS = $(PROFANITY_LIBS) $(GLIB_LIBS) $(STROPHE_LIBS) $(X11_LIBS)

all: $(BUILD_DIR)/$(PLUGIN_NAME).so

$(BUILD_DIR)/$(PLUGIN_NAME).so: $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) -shared -o $@ -fPIC $(ALL_CFLAGS) -Wl,-rpath=$(LIBRARY_PATH) $< $(ALL_LIBS)

install: all
	mkdir -p $(INSTALL_DIR)
	cp $(BUILD_DIR)/$(PLUGIN_NAME).so $(INSTALL_DIR)/

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all install clean
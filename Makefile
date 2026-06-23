PLUGIN_NAME = idle_monitor
SRC = idle_monitor.c
BUILD_DIR = build
INSTALL_DIR = $(HOME)/.local/share/profanity/plugins

CC ?= gcc
CFLAGS ?= -Wall -Wextra
PROFANITY_LIBS = "-lprofanity"
GLIB_CFLAGS = $(shell pkg-config --cflags glib-2.0 2>/dev/null || echo "")
GLIB_LIBS = $(shell pkg-config --libs glib-2.0 2>/dev/null || echo "-lglib-2.0")
X11_CFLAGS = $(shell pkg-config --cflags x11 xscrnsaver 2>/dev/null || echo "")
X11_LIBS = $(shell pkg-config --libs x11 xscrnsaver 2>/dev/null || echo "-lX11 -lXss")

all: $(BUILD_DIR)/$(PLUGIN_NAME).so

$(BUILD_DIR)/$(PLUGIN_NAME).so: $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) -shared -o $@ -fPIC $(CFLAGS) $(GLIB_CFLAGS) $(X11_CFLAGS) -Wl,-rpath=$(LIBRARY_PATH) $< $(PROFANITY_LIBS) $(GLIB_LIBS) $(X11_LIBS)

install: all
	mkdir -p $(INSTALL_DIR)
	cp $(BUILD_DIR)/$(PLUGIN_NAME).so $(INSTALL_DIR)/

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all install clean
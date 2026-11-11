# ClassicCars Studio preview server.
# The link includes Workstream 2 sources: src/json.c, src/gallery.c, src/api.c.

ifeq ($(OS),Windows_NT)
CC      ?= gcc
override CFLAGS += -std=c11 -Wall -Wextra -O2 -D_WIN32_WINNT=0x0600
LDFLAGS ?= -static -mwindows -Wl,--subsystem,windows -Wl,-e,mainCRTStartup
LDLIBS  ?= -lws2_32 -lgdi32 -luser32 -lole32 -loleaut32 -luuid -lshlwapi -ladvapi32
else
# Pin the system compiler. A Conda shell exports CC, CFLAGS, CPPFLAGS, and
# LDFLAGS for an older sysroot and adds -rpath into that prefix. System GTK
# and WebKitGTK then load Conda's libglib and libstdc++.
CC := $(shell if [ -x /usr/bin/cc ]; then echo /usr/bin/cc; elif [ -x /usr/bin/gcc ]; then echo /usr/bin/gcc; else echo cc; fi)
override CPPFLAGS :=
override CFLAGS := -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=700 -pthread
override LDFLAGS := -pthread -ldl
LDLIBS  := -pthread -ldl
endif

SRC_DIR := src
BUILD   := build
ifeq ($(OS),Windows_NT)
TARGET  = $(BUILD)/classiccars-studio.exe
else
TARGET  = $(BUILD)/classiccars-studio
endif

SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/http.c \
	$(SRC_DIR)/router.c \
	$(SRC_DIR)/ssi.c \
	$(SRC_DIR)/proxy.c \
	$(SRC_DIR)/settings.c \
	$(SRC_DIR)/config.c \
	$(SRC_DIR)/json.c \
	$(SRC_DIR)/gallery.c \
	$(SRC_DIR)/preview_draft.c \
	$(SRC_DIR)/project_search.c \
	$(SRC_DIR)/git_scm.c \
	$(SRC_DIR)/terminal.c \
	$(SRC_DIR)/api.c \
	$(SRC_DIR)/window.c

OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all test clean windows desktop desktop-install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

# src/version.h is generated from VERSION by build.py.
$(BUILD)/main.o $(BUILD)/api.o: $(SRC_DIR)/version.h

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

ROUNDTRIP_SRC := \
	tests/test_roundtrip.c \
	$(SRC_DIR)/gallery.c \
	$(SRC_DIR)/json.c \
	$(SRC_DIR)/settings.c \
	$(SRC_DIR)/config.c \
	$(SRC_DIR)/http.c

$(BUILD)/test_roundtrip: $(ROUNDTRIP_SRC) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ROUNDTRIP_SRC) $(LDFLAGS) $(LDLIBS)

test: $(TARGET) $(BUILD)/test_roundtrip
	$(TARGET) --check

clean:
	rm -rf $(BUILD) classiccars-studio.exe

# Cross-compile a GUI exe (no console window) that links libgcc statically.
# The Windows system DLLs below are already on every Windows box.
WIN_CC  ?= x86_64-w64-mingw32-gcc
WIN_CFLAGS := -std=c11 -Wall -Wextra -O2
WIN_LDFLAGS := -Wl,--subsystem,windows -Wl,-e,mainCRTStartup -static
WIN_LIBS := -lws2_32 -lgdi32 -luser32 -lole32 -loleaut32 -luuid -lshlwapi -ladvapi32
WIN_EXE := classiccars-studio.exe

windows: $(SRC_DIR)/version.h
	@if command -v $(WIN_CC) >/dev/null 2>&1; then \
	  echo "Compiling $(WIN_EXE) with $(WIN_CC)"; \
	  $(WIN_CC) $(WIN_CFLAGS) -mwindows -o $(WIN_EXE) $(SRCS) $(WIN_LDFLAGS) $(WIN_LIBS); \
	elif command -v zig >/dev/null 2>&1; then \
	  echo "Compiling $(WIN_EXE) with zig cc -target x86_64-windows-gnu"; \
	  zig cc -target x86_64-windows-gnu $(WIN_CFLAGS) -o $(WIN_EXE) $(SRCS) $(WIN_LDFLAGS) $(WIN_LIBS); \
	else \
	  echo "No Windows cross compiler found." >&2; \
	  echo "Install gcc-mingw-w64 (provides $(WIN_CC)) or zig, then run: make windows" >&2; \
	  echo "On Windows, run build_windows.bat in this directory." >&2; \
	  exit 1; \
	fi

# Rewrite ClassicCars-Studio.desktop with this checkout's absolute path.
desktop:
	@root="$$(cd "$(CURDIR)" && pwd)"; \
	out="$(CURDIR)/ClassicCars-Studio.desktop"; \
	{ \
	  echo "[Desktop Entry]"; \
	  echo "Version=1.0"; \
	  echo "Type=Application"; \
	  echo "Name=ClassicCars Studio"; \
	  echo "GenericName=Site preview"; \
	  echo "Comment=Open ClassicCars Studio in its own window"; \
	  echo "Exec=$$root/launcher.sh"; \
	  echo "TryExec=$$root/launcher.sh"; \
	  echo "Path=$$root"; \
	  icon="$$HOME/.local/share/icons/hicolor/256x256/apps/classiccars-studio.png"; \
	  if [ ! -f "$$icon" ]; then icon="$$root/assets/icon-256.png"; fi; \
	  if [ ! -f "$$icon" ]; then icon="$$root/assets/favicon-32x32.png"; fi; \
	  echo "Icon=$$icon"; \
	  echo "Terminal=false"; \
	  echo "Categories=Development;WebDevelopment;Utility;Graphics;"; \
	  echo "StartupWMClass=classiccars-studio"; \
	  echo "StartupNotify=true"; \
	} > "$$out"; \
	chmod 755 "$$out"; \
	chmod +x "$(CURDIR)/launcher.sh" "$(CURDIR)/launch-studio.sh"; \
	echo "Wrote $$out"

desktop-install: desktop
	mkdir -p "$(HOME)/.local/share/applications"
	cp ClassicCars-Studio.desktop "$(HOME)/.local/share/applications/classiccars-studio.desktop"
	chmod 755 ClassicCars-Studio.desktop "$(HOME)/.local/share/applications/classiccars-studio.desktop"
	@capital="$(HOME)/.local/share/applications/ClassicCars-Studio.desktop"; \
	installed="$(HOME)/.local/share/applications/classiccars-studio.desktop"; \
	if [ -f "$$capital" ] && [ ! "$$capital" -ef "$$installed" ]; then \
	  rm -f "$$capital"; \
	fi
	@if command -v update-desktop-database >/dev/null 2>&1; then \
	  update-desktop-database "$(HOME)/.local/share/applications" || true; \
	fi
	@echo "Installed $(HOME)/.local/share/applications/classiccars-studio.desktop"

-include $(DEPS)

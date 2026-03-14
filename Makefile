CC = cc
CFLAGS = -Wall -Wextra -Wno-misleading-indentation -O2
NCURSES_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo "-lncurses")
CURSES_LDFLAGS = $(NCURSES_LIBS) -lcurl
BACKEND_LDFLAGS = -lcurl

TUI_SRCS = nvt.c api.c cJSON.c
BACKEND_SRCS = backend.c api.c cJSON.c

all: tui backend

tui: nvt

backend: nvt-backend

backend-run: nvt-backend
	@printf "NVT API URL for the Home Assistant config flow: http://127.0.0.1:%s\n" "$${NVT_BACKEND_PORT:-8080}"
	./nvt-backend "$${NVT_BACKEND_PORT:-8080}"

nvt: $(TUI_SRCS) api.h config.h cJSON.h line_colors.h
	$(CC) $(CFLAGS) -o $@ $(TUI_SRCS) $(CURSES_LDFLAGS)

nvt-backend: $(BACKEND_SRCS) api.h config.h cJSON.h line_colors.h
	$(CC) $(CFLAGS) -o $@ $(BACKEND_SRCS) $(BACKEND_LDFLAGS)

clean:
	rm -f nvt nvt-backend

.PHONY: all tui backend backend-run clean

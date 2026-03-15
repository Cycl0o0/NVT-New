CC = cc
CFLAGS = -Wall -Wextra -Wno-misleading-indentation -O2
NCURSES_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo "-lncurses")
CURSES_LDFLAGS = $(NCURSES_LIBS) -lcurl
BACKEND_LDFLAGS = -lcurl

COMMON_SRCS = api.c cJSON.c
TUI_SRCS = nvt.c app_state.c data.c filter.c map_math.c network.c ui.c $(COMMON_SRCS)
BACKEND_SRCS = backend.c api.c cJSON.c
TEST_FILTER_SRCS = tests/test_filter.c filter.c
TEST_MAP_SRCS = tests/test_map_math.c map_math.c

all: tui backend

tui: nvt

backend: nvt-backend

test: tests/test_filter tests/test_map_math
	./tests/test_filter
	./tests/test_map_math

backend-run: nvt-backend
	@printf "NVT API URL for the Home Assistant config flow: http://127.0.0.1:%s\n" "$${NVT_BACKEND_PORT:-8080}"
	./nvt-backend "$${NVT_BACKEND_PORT:-8080}"

nvt: $(TUI_SRCS) api.h config.h cJSON.h line_colors.h
	$(CC) $(CFLAGS) -o $@ $(TUI_SRCS) $(CURSES_LDFLAGS)

nvt-backend: $(BACKEND_SRCS) api.h config.h cJSON.h line_colors.h
	$(CC) $(CFLAGS) -o $@ $(BACKEND_SRCS) $(BACKEND_LDFLAGS)

tests/test_filter: $(TEST_FILTER_SRCS) api.h config.h
	$(CC) $(CFLAGS) -I. -o $@ $(TEST_FILTER_SRCS)

tests/test_map_math: $(TEST_MAP_SRCS) map_math.h
	$(CC) $(CFLAGS) -I. -o $@ $(TEST_MAP_SRCS)

clean:
	rm -f nvt nvt-backend tests/test_filter tests/test_map_math

.PHONY: all tui backend test backend-run clean

CC = cc
CFLAGS = -Wall -Wextra -Wno-misleading-indentation -O2
UNAME_S := $(shell uname -s)
NCURSES_CFLAGS := $(shell pkg-config --cflags ncursesw 2>/dev/null)
NCURSES_LIBS := $(shell pkg-config --libs ncursesw 2>/dev/null || echo "-lncurses")

ifeq ($(UNAME_S),Darwin)
MACOS_SDK := $(shell xcrun --show-sdk-path 2>/dev/null)
ifneq ($(MACOS_SDK),)
CURL_LDFLAGS = $(MACOS_SDK)/usr/lib/libcurl.tbd
else
CURL_LDFLAGS = -L/usr/lib -lcurl
endif
else
CURL_LDFLAGS = -lcurl
endif

CURSES_LDFLAGS = $(NCURSES_LIBS) $(CURL_LDFLAGS)
BACKEND_LDFLAGS = $(CURL_LDFLAGS)

COMMON_SRCS = api.c cJSON.c
TUI_SRCS = nvt.c app_state.c config_file.c data.c filter.c itinerary.c map_math.c network.c ui.c $(COMMON_SRCS)
BACKEND_SRCS = backend.c api.c cJSON.c
TEST_FILTER_SRCS = tests/test_filter.c filter.c
TEST_ITINERARY_SRCS = tests/test_itinerary.c itinerary.c filter.c
TEST_MAP_SRCS = tests/test_map_math.c map_math.c

all: tui backend

tui: nvt

backend: nvt-backend

test: tests/test_filter tests/test_itinerary tests/test_map_math
	./tests/test_filter
	./tests/test_itinerary
	./tests/test_map_math
	python3 -m unittest discover -s tests -p 'test_mcp_server.py'

backend-run: nvt-backend
	@set -a; \
	[ ! -f .nvt-backend.env ] || . ./.nvt-backend.env; \
	set +a; \
	printf "NVT API URL for the Home Assistant config flow: http://127.0.0.1:%s\n" "$${NVT_BACKEND_PORT:-8080}"; \
	./nvt-backend "$${NVT_BACKEND_PORT:-8080}"

mcp-run: nvt-backend
	python3 ./mcp/nvt_mcp_server.py

nvt: $(TUI_SRCS) api.h config.h cJSON.h line_colors.h
	$(CC) $(CFLAGS) $(NCURSES_CFLAGS) -o $@ $(TUI_SRCS) $(CURSES_LDFLAGS)

nvt-backend: $(BACKEND_SRCS) api.h config.h cJSON.h line_colors.h
	$(CC) $(CFLAGS) -o $@ $(BACKEND_SRCS) $(BACKEND_LDFLAGS)

tests/test_filter: $(TEST_FILTER_SRCS) api.h config.h
	$(CC) $(CFLAGS) -I. -o $@ $(TEST_FILTER_SRCS)

tests/test_itinerary: $(TEST_ITINERARY_SRCS) api.h config.h itinerary.h
	$(CC) $(CFLAGS) -I. -o $@ $(TEST_ITINERARY_SRCS)

tests/test_map_math: $(TEST_MAP_SRCS) map_math.h
	$(CC) $(CFLAGS) -I. -o $@ $(TEST_MAP_SRCS)

clean:
	rm -f nvt nvt-backend tests/test_filter tests/test_itinerary tests/test_map_math

.PHONY: all tui backend test backend-run mcp-run clean

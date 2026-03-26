#include "config_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define CONFIG_DIR ".nvt"
#define CONFIG_FILE "config"

static void get_config_path(char *path, size_t size) {
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(path, size, "%s/%s/%s", home, CONFIG_DIR, CONFIG_FILE);
}

static void ensure_config_dir() {
    char path[1024];
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(path, sizeof(path), "%s/%s", home, CONFIG_DIR);
    mkdir(path, 0755);
}

void nvt_load_config(AppState *app) {
    char path[1024];
    FILE *fp;
    char line[256];
    char key[64], val[64];

    /* Set defaults */
    app->ui.theme = 0;
    app->ui.lang = 0;
    app->ui.network = NET_BDX;

    get_config_path(path, sizeof(path));
    fp = fopen(path, "r");
    if (!fp) return;

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if (strcmp(key, "theme") == 0) app->ui.theme = atoi(val);
            else if (strcmp(key, "lang") == 0) app->ui.lang = atoi(val);
            else if (strcmp(key, "network") == 0) app->ui.network = atoi(val);
        }
    }
    fclose(fp);

    /* Validate loaded values */
    if (app->ui.network < 0 || app->ui.network >= NET_COUNT) app->ui.network = NET_BDX;
    if (app->ui.theme < 0 || app->ui.theme >= 24) app->ui.theme = 0; /* Updated theme count check */
}

void nvt_save_config(const AppState *app) {
    char path[1024];
    FILE *fp;

    ensure_config_dir();
    get_config_path(path, sizeof(path));
    fp = fopen(path, "w");
    if (!fp) return;

    fprintf(fp, "theme=%d\n", app->ui.theme);
    fprintf(fp, "lang=%d\n", app->ui.lang);
    fprintf(fp, "network=%d\n", app->ui.network);
    
    fclose(fp);
}

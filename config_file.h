#ifndef CONFIG_FILE_H
#define CONFIG_FILE_H

#include "app_state.h"

void nvt_load_config(AppState *app);
void nvt_save_config(const AppState *app);

#endif

#include "ui_main.h"

// Dummy-Implementierungen für Linker

extern "C"
{
#include <lvgl.h>
}
void ui_main_init(lv_disp_t *) {}
void ui_main_update_status(bool, bool, bool, bool) {}
void ui_main_loop() {}

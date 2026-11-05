#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool control_bar_is_open(void);
bool control_bar_hit(float x, float y);
void control_bar_toggle(void);
void control_bar_draw(float scale);
void menu_control_open_full(void);
void menu_absorb_pad_buttons(void);
#ifdef __cplusplus
}
#endif

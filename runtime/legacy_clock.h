#pragma once
#include <stdint.h>
float sdk7_frame_ratio(void);
unsigned sdk7_pad_tick(void);
void sdk7_frame_presented(uint64_t now_ns);

#include "stdafx.h"
#include "config.h"
#include "guids.h"

namespace wsz::config {
cfg_int window_x(guids::cfg_window_x, CW_USEDEFAULT);
cfg_int window_y(guids::cfg_window_y, CW_USEDEFAULT);
cfg_string skin_path(guids::cfg_skin_path, "");
}

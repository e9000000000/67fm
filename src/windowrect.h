#pragma once

#include <windows.h>

namespace windowrect {

// Shrinks a window rectangle to fit a monitor work area and moves it inside.
RECT ClampToWork(const RECT& window, const RECT& work, int margin = 40);

}  // namespace windowrect

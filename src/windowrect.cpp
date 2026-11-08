#include "windowrect.h"

#include <algorithm>

namespace windowrect {

RECT ClampToWork(const RECT& window, const RECT& work, int margin) {
    const int workWidth = std::max(1, static_cast<int>(work.right - work.left));
    const int workHeight = std::max(1, static_cast<int>(work.bottom - work.top));
    int width = std::max(1, static_cast<int>(window.right - window.left));
    int height = std::max(1, static_cast<int>(window.bottom - window.top));
    width = std::min(width, std::max(1, workWidth - margin));
    height = std::min(height, std::max(1, workHeight - margin));

    int left = static_cast<int>(window.left);
    int top = static_cast<int>(window.top);
    left = std::clamp(left, static_cast<int>(work.left),
                      std::max(static_cast<int>(work.left), static_cast<int>(work.right) - width));
    top = std::clamp(top, static_cast<int>(work.top),
                     std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));

    RECT result{left, top, left + width, top + height};
    return result;
}

}  // namespace windowrect

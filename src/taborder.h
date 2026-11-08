#pragma once

#include <algorithm>
#include <vector>

namespace taborder {

// Moves items[from] to final index `to`, shifting the rest.
template <typename T>
void Move(std::vector<T>& items, int from, int to) {
    const int count = static_cast<int>(items.size());
    if (from < 0 || to < 0 || from >= count || to >= count || from == to) {
        return;
    }
    if (from < to) {
        std::rotate(items.begin() + from, items.begin() + from + 1, items.begin() + to + 1);
    } else {
        std::rotate(items.begin() + to, items.begin() + from, items.begin() + from + 1);
    }
}

// Converts an insertion slot (0..count) into the final index after removal.
inline int TargetFromDrop(int dropIndex, int from) {
    return dropIndex > from ? dropIndex - 1 : dropIndex;
}

}  // namespace taborder

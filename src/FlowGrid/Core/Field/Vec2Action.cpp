#include "Vec2Action.h"

namespace Action {
std::variant<Vec2::Set, bool> Vec2::Set::Merge(const Vec2::Set &other) const {
    if (path == other.path) return other;
    return false;
}
} // namespace Action

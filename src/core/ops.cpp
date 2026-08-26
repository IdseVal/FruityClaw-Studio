#include "core/ops.h"

#include <algorithm>

namespace core {

Delta apply_delta(MusicalContent& content, const Delta& delta) {
    Delta inverse;
    inverse.label = delta.label;
    inverse.origin = delta.origin;
    inverse.ops.reserve(delta.ops.size());

    for (std::size_t i = 0; i < delta.ops.size(); ++i) {
        try {
            if (auto inv = delta.ops[i].run(content)) {
                inverse.ops.push_back(std::move(*inv));
            }
        } catch (const OpError&) {
            // Atomicity: roll back what already applied, most recent first,
            // so the caller observes either the whole Delta or none of it.
            for (auto it = inverse.ops.rbegin(); it != inverse.ops.rend(); ++it) {
                it->run(content);
            }
            throw;
        }
    }

    std::reverse(inverse.ops.begin(), inverse.ops.end());
    return inverse;
}

}  // namespace core

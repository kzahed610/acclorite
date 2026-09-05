#pragma once

#include <vector>

#include "acclorite/core/location.hpp"
#include "acclorite/core/query.hpp"

namespace acclorite {

class LocationSource {
public:
    virtual ~LocationSource() = default;
    [[nodiscard]] virtual bool available() const = 0;
    [[nodiscard]] virtual std::vector<LocationHit> search(const Query& query) const = 0;
};

} // namespace acclorite

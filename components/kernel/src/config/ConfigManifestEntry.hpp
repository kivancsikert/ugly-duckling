#pragma once

#include <string>

namespace cornucopia::ugly_duckling::kernel::config {

// One SYNC manifest entry: the fingerprint and requestedAt stamp of a configuration (device,
// network, or function) echoed verbatim from the envelope that was last successfully applied.
struct ConfigManifestEntry {
    std::string fingerprint;
    std::string requestedAt;
};

}    // namespace cornucopia::ugly_duckling::kernel::config

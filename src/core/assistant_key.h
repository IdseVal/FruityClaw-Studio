// The seam for the user's private Assistant key (core document 1.1a, 2, 7).
// The Studio is a complete DAW without it; the key only switches the
// Assistant on. The UI offers it once on first open and reads nothing but
// "is there a key" — the key itself is only ever read by the Assistant's
// transport. Like the ports in playback.h, this is implemented behind the
// seam (src/assistant/) and handed to the UI by the composition root, so
// the UI never learns where or how the key is kept.
#pragma once

#include <optional>
#include <string>

namespace core {

class AssistantKeyPort {
public:
    virtual ~AssistantKeyPort() = default;

    virtual bool has_key() const = 0;

    // The key as pasted, or nothing. Never to be shown or logged.
    virtual std::optional<std::string> key() const = 0;

    // Persists the key; false when it could not be kept (the caller says so
    // to the user rather than pretending it was).
    virtual bool store_key(const std::string& key) = 0;

    virtual void clear_key() = 0;

    // Whether the first-open offer has been shown and answered. Declining is
    // an answer: a user who said no is never asked again (core document 1.1a).
    virtual bool offer_made() const = 0;
    virtual void record_offer() = 0;
};

}  // namespace core

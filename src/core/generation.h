// Music generation enablement (core document section 6.4).
//
// Generation is off by default and no model ships or downloads on its own
// (NON-scope 7). The user turns it on by choosing one entry from the list the
// project handpicks (ADR-003); that list lives here so every surface that
// gates on generation reads one source. Each entry states its rights
// position, not merely its name — the curation commitment 6.4 records.
//
// Settings are outside the Assistant's reach (core document 3.8), so nothing
// here is a Function and nothing here touches the Project. Persistence is
// behind GenerationSettingsPort, implemented by the composition root; the
// UI never learns where the choice or the key is kept.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core {

// The two enablement paths of section 6.4.
enum class GenerationPath {
    Local,   // weights imported by the user; nothing leaves the machine
    Remote,  // the user's own API key; the prompt leaves the machine
};

// One handpicked entry. The four rights fields are deliberately the same
// shape for every entry so the settings page can lay them side by side and
// the user compares trade-offs rather than reading a ranking (ADR-003,
// Consequences).
struct GenerationModel {
    std::string id;    // permanent; written into settings files
    std::string name;
    GenerationPath path;
    std::string runs;            // where inference happens
    std::string leaves_machine;  // what is sent off the machine, if anything
    std::string output_rights;   // what the user may do with the output
    std::string conditions;      // the catch: caps, gates, carve-outs, gaps
    // False for an entry recorded as provisional: shown so the user knows it
    // is coming, never selectable (ADR-003 entry 4 "does not ship until the
    // endpoint is verified").
    bool available = true;
};

// The ADR-003 list, in the ADR's order.
const std::vector<GenerationModel>& handpicked_models();

// The entry with `id`, or null when no such entry is listed (also the case
// for a model removed from the list after the user configured it — ADR-003
// makes removal a first-class action and the user is told why).
const GenerationModel* find_model(std::string_view id);

// What is kept between sessions. A default-constructed value is the fresh
// install: off, nothing chosen, nothing imported. The API key of a Remote
// model is never held here; it lives only behind the port.
struct GenerationSettings {
    bool enabled = false;
    std::string model;         // GenerationModel::id, empty when none chosen
    std::string weights_path;  // Local path: the folder the user imported

    bool operator==(const GenerationSettings&) const = default;
};

// Why a request to turn generation on cannot be honoured, or nothing when
// it can. `has_key` is whether a key is stored for a Remote model;
// `acknowledged` is whether the user confirmed the section 6.2 warning.
// Every rule of 6.4 that the page enforces is checked here, so the page
// cannot enable by accident and the rules are testable without Qt.
std::optional<std::string> enable_problem(const GenerationSettings& requested,
                                          bool has_key, bool acknowledged);

// The seam to wherever the settings and the key are kept.
class GenerationSettingsPort {
public:
    virtual ~GenerationSettingsPort() = default;

    virtual GenerationSettings read() const = 0;

    // Persists the settings; false when they could not be kept, and the
    // caller says so to the user rather than pretending they were.
    virtual bool write(const GenerationSettings& settings) = 0;

    // The Remote model's key. Only ever read by a generator's transport;
    // the UI asks nothing but whether one exists.
    virtual bool has_key() const = 0;
    virtual bool store_key(const std::string& key) = 0;
    virtual void clear_key() = 0;
};

}  // namespace core

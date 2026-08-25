#include "assistant/selector.h"

#include <algorithm>
#include <cctype>

namespace assistant {
namespace {

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Every entity of `kind`, in the Project's authored order.
std::vector<Candidate> all_of(EntityKind kind, const core::Project& project) {
    std::vector<Candidate> out;
    switch (kind) {
        case EntityKind::Pattern:
            for (const auto& p : project.patterns.items) out.push_back({p.id, p.name});
            break;
        case EntityKind::Track:
            for (const auto& a : project.arrangements.items)
                for (const auto& t : a.tracks) out.push_back({t.id, t.name});
            break;
        case EntityKind::Instrument:
            for (const auto& i : project.instruments.items) out.push_back({i.id, i.name});
            break;
        case EntityKind::Sample:
            for (const auto& s : project.samples.items) out.push_back({s.id, s.name});
            break;
    }
    return out;
}

Resolution by_name(const std::string& text, EntityKind kind, const core::Project& project) {
    std::vector<Candidate> all = all_of(kind, project);
    std::string wanted = lowered(text);

    // An exact (case-insensitive) match wins outright, so "Drums A" is not
    // ambiguous with "Drums AB". Otherwise any name containing the words, or
    // contained in them, is a candidate.
    std::vector<Candidate> exact;
    std::vector<Candidate> partial;
    for (const Candidate& c : all) {
        std::string have = lowered(c.name);
        if (have == wanted) {
            exact.push_back(c);
        } else if (!wanted.empty() && (have.find(wanted) != std::string::npos ||
                                       wanted.find(have) != std::string::npos)) {
            partial.push_back(c);
        }
    }
    const std::vector<Candidate>& hits = exact.empty() ? partial : exact;
    if (hits.size() == 1) return hits.front().id;
    if (hits.empty())
        return NotFound{"No " + std::string(kind_name(kind)) + " called '" + text + "'"};
    return Ambiguous{hits};
}

}  // namespace

std::optional<core::Id>& Focus::slot(EntityKind kind) {
    switch (kind) {
        case EntityKind::Pattern: return pattern;
        case EntityKind::Track: return track;
        case EntityKind::Instrument: return instrument;
        case EntityKind::Sample: return sample;
    }
    return pattern;
}

const std::optional<core::Id>& Focus::slot(EntityKind kind) const {
    return const_cast<Focus*>(this)->slot(kind);
}

const char* kind_name(EntityKind kind) {
    switch (kind) {
        case EntityKind::Pattern: return "Pattern";
        case EntityKind::Track: return "Track";
        case EntityKind::Instrument: return "Instrument";
        case EntityKind::Sample: return "Sample";
    }
    return "";
}

Resolution resolve(const Selector& selector, EntityKind kind, const core::Project& project,
                   const Focus& focused, const Focus& last_created) {
    switch (selector.kind) {
        case Selector::Kind::Focused:
            if (const auto& id = focused.slot(kind)) return *id;
            return NotFound{std::string("No ") + kind_name(kind) + " is selected"};
        case Selector::Kind::LastCreated:
            if (const auto& id = last_created.slot(kind)) return *id;
            return NotFound{std::string("No ") + kind_name(kind) + " was just created"};
        case Selector::Kind::Ordinal: {
            std::vector<Candidate> all = all_of(kind, project);
            if (selector.ordinal < 1 || selector.ordinal > static_cast<int>(all.size()))
                return NotFound{"There is no " + std::string(kind_name(kind)) + " number " +
                                std::to_string(selector.ordinal)};
            return all[static_cast<std::size_t>(selector.ordinal - 1)].id;
        }
        case Selector::Kind::Named:
            return by_name(selector.name, kind, project);
    }
    return NotFound{"Nothing selected"};
}

}  // namespace assistant

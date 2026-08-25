// One Assistant turn, end to end: bind the Function file, send the prompt,
// check the reply against the file that was sent, resolve Selectors
// locally, run each selected Function against a working copy of the Project
// and hand back the Deltas for the History to apply.
//
// The session never writes the Project. It returns Deltas; the caller
// applies them through ProjectHistory on the edit thread. Ops address by id,
// so a Delta computed here applies to the live Project exactly as it did to
// the copy — or fails cleanly if the user changed things meanwhile.
//
// Contract: docs/specs/function-surface.md sections 2.3, 3.2 (turn 1 carries
// no Project content), 4.2 (integrity check), 4.4 (turn binding, O-16.1)
// and O-16.2 (local disambiguation).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "assistant/function_file.h"
#include "assistant/transport.h"

namespace assistant {

// A Selector that matched several entities. The user picks; no request is
// made. Everything needed to carry on the turn afterwards rides along.
struct PendingChoice {
    std::string question;
    std::vector<Candidate> candidates;

    // Resume state. The stalled Function is `remaining.front()`; `argument`
    // is the Selector being asked about and `answered` holds the choices
    // already made for that same Function, so a second ambiguous Selector
    // does not re-ask the first question.
    std::string argument;
    std::map<std::string, core::Id> answered;
    std::vector<ToolUse> remaining;
    Focus focused;
    Focus last_created;
    std::string text;
};

struct TurnOutcome {
    std::string text;                      // shown to the user; never names a Function
    std::vector<core::Delta> deltas;       // apply in order through ProjectHistory
    std::optional<PendingChoice> pending;  // set when the user must choose
    std::uint64_t manifest_hash = 0;       // the file bound to this turn (O-16.1)
};

class AssistantSession {
public:
    AssistantSession(const Registry& registry, AssistantTransport& transport);

    // Runs one turn against a snapshot of the Project. Blocks on the
    // transport; call it off the UI thread.
    TurnOutcome run_turn(const std::string& prompt, core::Project project,
                         const Toggles& toggles, const Focus& focused);

    // Continues a stalled turn with the user's answer. Local only.
    TurnOutcome resume(PendingChoice pending, core::Id chosen, core::Project project);

private:
    // Selector answers for `tool_uses.front()`, keyed by argument name.
    using Answers = std::map<std::string, core::Id>;
    TurnOutcome execute(std::vector<ToolUse> tool_uses, core::Project working, Focus focused,
                        Focus last_created, std::string text, Answers first_answers);

    const Registry& registry_;
    AssistantTransport& transport_;
};

// Removes any Function name from prose meant for the user. The transport's
// instructions ask the model not to name them; this is the backstop that
// makes NON-scope item 6 hold whatever the model says.
std::string without_function_names(std::string text, const Registry& registry);

}  // namespace assistant

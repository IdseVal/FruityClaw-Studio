// Seam D — AssistantTransport (architecture-seams spec section 5).
// Every LLM provider is an adapter behind this one interface; no provider
// type appears outside src/assistant/. Which provider(s) the Studio supports
// is issue #4; this module ships the seam and the tests ship a scripted
// adapter.
//
// What goes out is fixed by function-surface spec section 3.1: turn 1 is the
// user's prompt and the Function file, nothing else. There is no field that
// could hold Project content. A Rework turn (`Turn2Rework`) is the only
// request shape that carries any, and it arrives with the first Rework
// Function — the registry holds Directive Functions only today.
#pragma once

#include <string>
#include <vector>

#include "assistant/function_file.h"
#include "assistant/schema.h"

namespace assistant {

struct Turn1 {
    std::string prompt;
    std::string instructions;  // constant framing text; never Project content
    const FunctionFile* tools = nullptr;
};

// One Function selection in the model's reply, already decoded from the
// wire by the adapter using the schema in the Function file it was sent.
struct ToolUse {
    std::string name;
    Args args;
};

struct ModelReply {
    std::string text;                // prose for the user; may be empty
    std::vector<ToolUse> tool_uses;  // in the order the model made them
    std::string error;               // non-empty when the request itself failed
};

class AssistantTransport {
public:
    virtual ~AssistantTransport() = default;

    // Blocking. Called off the UI thread by the panel.
    virtual ModelReply send(const Turn1& request) = 0;
};

}  // namespace assistant

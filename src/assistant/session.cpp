#include "assistant/session.h"

#include <type_traits>
#include <utility>

namespace assistant {
namespace {

// What the model is told the Studio is. Constant: it carries no Project
// content. It restates NON-scope 1 and 6 in the model's own instructions so
// the common case needs no scrubbing.
const char* const kInstructions =
    "You are the Assistant inside a music studio. The user is the author: you act on the "
    "individual Patterns, Tracks, Instruments and Samples they name, one component at a time, "
    "using the tools offered. Never try to compose a whole piece or restructure the whole "
    "Arrangement. If a request needs something no tool offers, say so briefly in musical "
    "terms. Never mention tool or function names; describe what changed in the studio. "
    "Refer to Patterns, Tracks, Instruments and Samples exactly as the user named them.";

std::string to_std(const Resolution& resolution, std::string& error,
                   std::vector<Candidate>& candidates, core::Id& out) {
    if (const core::Id* id = std::get_if<core::Id>(&resolution)) {
        out = *id;
        return "ok";
    }
    if (const NotFound* missing = std::get_if<NotFound>(&resolution)) {
        error = missing->message;
        return "missing";
    }
    candidates = std::get<Ambiguous>(resolution).candidates;
    return "ambiguous";
}

}  // namespace

AssistantSession::AssistantSession(const Registry& registry, AssistantTransport& transport)
    : registry_(registry), transport_(transport) {}

TurnOutcome AssistantSession::run_turn(const std::string& prompt, core::Project project,
                                       const Toggles& toggles, const Focus& focused) {
    // Bound now and immutable for the turn (spec 4.4). A toggle flipped while
    // the request is in flight takes effect on the next turn.
    FunctionFile file = build(registry_, toggles);

    Turn1 request;
    request.prompt = prompt;
    request.instructions = kInstructions;
    request.tools = &file;
    ModelReply reply = transport_.send(request);

    TurnOutcome outcome;
    outcome.manifest_hash = file.manifest_hash;
    if (!reply.error.empty()) {
        outcome.text = "The Assistant could not be reached: " + reply.error;
        return outcome;
    }

    // Integrity, not permission (spec 4.2): a name outside the file that was
    // sent means a malformed reply, and the turn ends. Nothing disabled was
    // ever offered, so this never fires from a toggle.
    for (const ToolUse& use : reply.tool_uses) {
        bool offered = false;
        for (const ToolDefinition& entry : file.entries) offered |= entry.name == use.name;
        if (!offered) {
            outcome.text =
                "The Assistant replied with something the Studio could not use. Nothing was "
                "changed.";
            return outcome;
        }
    }

    TurnOutcome executed = execute(std::move(reply.tool_uses), std::move(project), focused,
                                   Focus{}, std::move(reply.text), {});
    executed.manifest_hash = file.manifest_hash;
    return executed;
}

TurnOutcome AssistantSession::resume(PendingChoice pending, core::Id chosen,
                                     core::Project project) {
    Answers answers = std::move(pending.answered);
    answers[pending.argument] = chosen;
    return execute(std::move(pending.remaining), std::move(project), pending.focused,
                   pending.last_created, std::move(pending.text), std::move(answers));
}

TurnOutcome AssistantSession::execute(std::vector<ToolUse> tool_uses, core::Project working,
                                      Focus focused, Focus last_created, std::string text,
                                      Answers first_answers) {
    TurnOutcome outcome;

    for (std::size_t i = 0; i < tool_uses.size(); ++i) {
        const ToolUse& use = tool_uses[i];
        const FunctionDescriptor* descriptor = find(registry_, use.name);
        if (!descriptor) {
            outcome.text = "The Assistant replied with something the Studio could not use.";
            return outcome;
        }
        if (auto problem = validate(descriptor->schema, use.args)) {
            outcome.text = "The Assistant asked for something the Studio cannot do (" +
                           *problem + ").";
            return outcome;
        }

        // Selectors resolve here, against the real Project, locally.
        ResolvedArgs resolved;
        for (const Property& property : descriptor->schema.properties) {
            const ArgValue& value = use.args.at(property.name);
            const Selector* selector = std::get_if<Selector>(&value);
            if (!selector) {
                std::visit(
                    [&](const auto& v) {
                        if constexpr (!std::is_same_v<std::decay_t<decltype(v)>, Selector>)
                            resolved[property.name] = v;
                    },
                    value);
                continue;
            }
            if (i == 0) {
                auto answered = first_answers.find(property.name);
                if (answered != first_answers.end()) {
                    resolved[property.name] = answered->second;
                    continue;
                }
            }
            std::string error;
            std::vector<Candidate> candidates;
            core::Id id;
            std::string state = to_std(resolve(*selector, property.entity, working, focused,
                                               last_created),
                                       error, candidates, id);
            if (state == "ok") {
                resolved[property.name] = id;
            } else if (state == "missing") {
                outcome.text = error + ".";
                return outcome;
            } else {
                PendingChoice choice;
                choice.question = "Which " + std::string(kind_name(property.entity)) +
                                  " did you mean by '" + selector->name + "'?";
                choice.candidates = std::move(candidates);
                choice.argument = property.name;
                if (i == 0) choice.answered = first_answers;
                choice.remaining.assign(tool_uses.begin() + static_cast<std::ptrdiff_t>(i),
                                        tool_uses.end());
                choice.focused = focused;
                choice.last_created = last_created;
                choice.text = std::move(text);
                outcome.pending = std::move(choice);
                return outcome;
            }
        }

        core::functions::Expected<FunctionResult> result = descriptor->impl(resolved, working);
        if (!result.ok()) {
            outcome.text = result.error + ".";
            return outcome;
        }
        try {
            core::apply_delta(working, result->delta);
        } catch (const core::OpError&) {
            outcome.text = "That change could not be made.";
            return outcome;
        }
        if (result->created) last_created.slot(*result->created_kind) = *result->created;
        outcome.deltas.push_back(std::move(result->delta));
    }

    outcome.text = without_function_names(std::move(text), registry_);
    if (outcome.text.empty() && !outcome.deltas.empty()) outcome.text = "Done.";
    return outcome;
}

std::string without_function_names(std::string text, const Registry& registry) {
    for (const FunctionDescriptor& descriptor : registry) {
        std::size_t at = 0;
        while ((at = text.find(descriptor.name, at)) != std::string::npos) {
            text.replace(at, descriptor.name.size(), "that");
            at += 4;
        }
    }
    return text;
}

}  // namespace assistant

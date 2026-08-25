#include "core/effect_functions.h"

#include <algorithm>
#include <utility>

#include "core/effect_schema.h"

namespace core::functions {
namespace {

// The chain holding `effect`, searched across the two holders the data model
// defines. Null when no chain holds it.
std::vector<Effect>* chain_holding(Project& p, Id effect) {
    for (const Effect& e : p.master_chain)
        if (e.id == effect) return &p.master_chain;
    for (Instrument& instrument : p.instruments.items)
        for (const Effect& e : instrument.chain)
            if (e.id == effect) return &instrument.chain;
    return nullptr;
}

const std::vector<Effect>* chain_holding(const Project& p, Id effect) {
    return chain_holding(const_cast<Project&>(p), effect);
}

std::vector<Effect>& chain_or_throw(Project& p, Id effect) {
    if (std::vector<Effect>* chain = chain_holding(p, effect)) return *chain;
    throw OpError("Effect not found: " + to_string(effect));
}

Effect& effect_or_throw(Project& p, Id effect) {
    std::vector<Effect>& chain = chain_or_throw(p, effect);
    return *std::find_if(chain.begin(), chain.end(),
                         [effect](const Effect& e) { return e.id == effect; });
}

std::vector<Effect>* chain_of(Project& p, ChainRef ref) {
    if (!ref.instrument) return &p.master_chain;
    Instrument* instrument = p.instruments.find(*ref.instrument);
    return instrument ? &instrument->chain : nullptr;
}

std::vector<Effect>& chain_or_throw(Project& p, ChainRef ref) {
    if (std::vector<Effect>* chain = chain_of(p, ref)) return *chain;
    throw OpError("Instrument not found: " + to_string(*ref.instrument));
}

std::size_t entity_bytes(const Effect& effect) {
    return sizeof(Effect) + effect.params.size() * 32;
}

// Insert/Remove pairs as in the other function files: a Remove bakes the
// observed chain and index into the Insert it returns so undo restores
// order exactly.

Op insert_effect_op(ChainRef chain, Effect effect, std::size_t index);

Op remove_effect_op(Id effect, std::size_t bytes) {
    Op op;
    op.bytes = bytes;
    op.run = [effect](Project& p) -> std::optional<Op> {
        ChainRef ref = ChainRef::master();
        for (Instrument& instrument : p.instruments.items)
            for (const Effect& e : instrument.chain)
                if (e.id == effect) ref = ChainRef::of(instrument.id);
        std::vector<Effect>& chain = chain_or_throw(p, effect);
        auto it = std::find_if(chain.begin(), chain.end(),
                               [effect](const Effect& e) { return e.id == effect; });
        std::size_t index = static_cast<std::size_t>(it - chain.begin());
        Effect removed = std::move(*it);
        chain.erase(it);
        return insert_effect_op(ref, std::move(removed), index);
    };
    return op;
}

Op insert_effect_op(ChainRef ref, Effect effect, std::size_t index) {
    Op op;
    op.bytes = entity_bytes(effect);
    op.run = [ref, effect = std::move(effect), index](Project& p) -> std::optional<Op> {
        std::vector<Effect>& chain = chain_or_throw(p, ref);
        std::size_t at = std::min(index, chain.size());
        chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(at), effect);
        return remove_effect_op(effect.id, entity_bytes(effect));
    };
    return op;
}

Op move_effect_op(Id effect, std::size_t new_index) {
    Op op;
    op.bytes = sizeof(std::size_t);
    op.run = [effect, new_index](Project& p) -> std::optional<Op> {
        std::vector<Effect>& chain = chain_or_throw(p, effect);
        auto it = std::find_if(chain.begin(), chain.end(),
                               [effect](const Effect& e) { return e.id == effect; });
        std::size_t from = static_cast<std::size_t>(it - chain.begin());
        std::size_t to = std::min(new_index, chain.size() - 1);
        if (from == to) return std::nullopt;
        Effect moving = std::move(*it);
        chain.erase(it);
        chain.insert(chain.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
        return move_effect_op(effect, from);
    };
    return op;
}

std::string holder_name(const Project& project, ChainRef ref) {
    if (!ref.instrument) return "master";
    const Instrument* instrument = project.instruments.find(*ref.instrument);
    return "'" + (instrument ? instrument->name : std::string()) + "'";
}

const Effect* find_effect(const Project& project, Id effect) {
    const std::vector<Effect>* chain = chain_holding(project, effect);
    if (!chain) return nullptr;
    for (const Effect& e : *chain)
        if (e.id == effect) return &e;
    return nullptr;
}

}  // namespace

Expected<CreatedDelta> add_effect(const Project& project, ChainRef chain, EffectType type,
                                  Origin origin) {
    if (chain.instrument && !project.instruments.find(*chain.instrument))
        return Expected<CreatedDelta>::failure("No such Instrument");

    Effect effect;
    effect.id = new_id();
    effect.type = type;
    for (const EffectParameter& parameter : effect_parameters(type))
        effect.params.emplace(std::string(parameter.name), parameter.default_value);

    Delta delta;
    delta.label = "Add " + std::string(effect_type_name(type)) + " to " +
                  holder_name(project, chain);
    delta.origin = origin;
    Id id = effect.id;
    std::size_t at = chain_of(const_cast<Project&>(project), chain)->size();
    delta.ops.push_back(insert_effect_op(chain, std::move(effect), at));
    return Expected<CreatedDelta>::success({std::move(delta), id});
}

Expected<Delta> remove_effect(const Project& project, Id effect, Origin origin) {
    const Effect* e = find_effect(project, effect);
    if (!e) return Expected<Delta>::failure("No such Effect");

    Delta delta;
    delta.label = "Remove " + std::string(effect_type_name(e->type));
    delta.origin = origin;
    delta.ops.push_back(remove_effect_op(effect, entity_bytes(*e)));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> move_effect(const Project& project, Id effect, std::size_t new_index,
                            Origin origin) {
    const Effect* e = find_effect(project, effect);
    if (!e) return Expected<Delta>::failure("No such Effect");
    if (new_index >= chain_holding(project, effect)->size())
        return Expected<Delta>::failure("Position is past the end of the chain");

    Delta delta;
    delta.label = "Move " + std::string(effect_type_name(e->type));
    delta.origin = origin;
    delta.ops.push_back(move_effect_op(effect, new_index));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> set_effect_parameter(const Project& project, Id effect, std::string name,
                                     float value, Origin origin) {
    const Effect* e = find_effect(project, effect);
    if (!e) return Expected<Delta>::failure("No such Effect");
    const EffectParameter* parameter = find_effect_parameter(e->type, name);
    if (!parameter)
        return Expected<Delta>::failure(std::string(effect_type_name(e->type)) +
                                        " has no parameter '" + name + "'");
    value = std::clamp(value, parameter->min_value, parameter->max_value);

    Delta delta;
    delta.label = "Set " + std::string(effect_type_name(e->type)) + " " + name;
    delta.origin = origin;
    delta.ops.push_back(make_set<float>(
        [effect, name](Project& p) {
            Effect& target = effect_or_throw(p, effect);
            auto it = target.params.find(name);
            return it == target.params.end() ? 0.0f : it->second;
        },
        [effect, name](Project& p, float v) { effect_or_throw(p, effect).params[name] = v; },
        value));
    return Expected<Delta>::success(std::move(delta));
}

Expected<Delta> set_effect_bypassed(const Project& project, Id effect, bool bypassed,
                                    Origin origin) {
    const Effect* e = find_effect(project, effect);
    if (!e) return Expected<Delta>::failure("No such Effect");

    Delta delta;
    delta.label = std::string(bypassed ? "Bypass " : "Enable ") +
                  std::string(effect_type_name(e->type));
    delta.origin = origin;
    delta.ops.push_back(make_set<bool>(
        [effect](Project& p) { return !effect_or_throw(p, effect).enabled; },
        [effect](Project& p, bool v) { effect_or_throw(p, effect).enabled = !v; }, bypassed));
    return Expected<Delta>::success(std::move(delta));
}

}  // namespace core::functions

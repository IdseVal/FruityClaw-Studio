#include "core/generation.h"

namespace core {

// Every claim below is ADR-003's, measured on 2026-08-23. When the quarterly
// review changes an entry, this table changes with it; nothing else does.
const std::vector<GenerationModel>& handpicked_models() {
    static const std::vector<GenerationModel> models = {
        {"stable-audio-3",
         "Stable Audio 3",
         GenerationPath::Local,
         "On this computer, from weights you import.",
         "Nothing. The weights are downloaded once, by you.",
         "You own the output. Free for commercial use under US $1M annual revenue; "
         "above that the licence ends and Stability must be asked.",
         "Needs a Hugging Face account and two licence acceptances: the Stability AI "
         "Community License and Google's Gemma Terms, which cover the bundled text "
         "encoder. Training data is licensed and documented."},
        {"ace-step-1.5",
         "ACE-Step 1.5",
         GenerationPath::Local,
         "On this computer, from weights you import.",
         "Nothing. The weights download without an account.",
         "Unrestricted: MIT. No revenue cap, no attribution, no registration.",
         "Training data is not documented. A permissive licence on the weights is not a "
         "warranty about what they learned from; this is the open risk you take for a "
         "model with no gate and no cap."},
        {"elevenlabs-music",
         "ElevenLabs Music",
         GenerationPath::Remote,
         "On ElevenLabs' servers, with your API key.",
         "Your text prompt. For audio-to-audio, your audio.",
         "Commercial use permitted on every paid tier, except film, TV, radio and "
         "multi-platform games unless you hold an Enterprise Music plan.",
         "Plans are capped by headcount (individual, under 10, under 50). The free tier "
         "cannot download at all, so a free key is useless here. Building a library of "
         "generated Samples for others is prohibited. Training data is licensed."},
        {"stable-audio-api",
         "Stable Audio (Stability AI API)",
         GenerationPath::Remote,
         "On Stability AI's servers, with your API key.",
         "Your text prompt. For audio-to-audio, your audio.",
         "As Stable Audio 3: you own the output, same licensed training data.",
         "Not available yet. The Stable Audio 3 endpoint could not be verified; this "
         "entry ships when it is.",
         false},
    };
    return models;
}

const GenerationModel* find_model(std::string_view id) {
    for (const GenerationModel& model : handpicked_models()) {
        if (model.id == id) return &model;
    }
    return nullptr;
}

std::optional<std::string> enable_problem(const GenerationSettings& requested,
                                          bool has_key, bool acknowledged) {
    const GenerationModel* model = find_model(requested.model);
    if (!model) return "Choose a model first.";
    if (!model->available) return model->name + " is not available yet.";
    if (model->path == GenerationPath::Local && requested.weights_path.empty()) {
        return "Choose the folder holding the " + model->name + " weights.";
    }
    if (model->path == GenerationPath::Remote && !has_key) {
        return "Paste your " + model->name + " API key.";
    }
    if (!acknowledged) {
        return "Confirm that you understand generated output may not be licenseable.";
    }
    return std::nullopt;
}

}  // namespace core

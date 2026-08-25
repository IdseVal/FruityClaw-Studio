// Where the Function toggle store lives between sessions: one text file in
// the Studio's config directory. The directory derives from FCS_APP_ID
// (ADR-008: never a product name), so a rename costs nothing here.
#pragma once

#include "assistant/toggles.h"

namespace app {

// The store as last saved; an empty store when there is no file yet.
assistant::FunctionToggles load_toggles();

// Writes the store atomically. Returns false, having logged, when the
// config directory cannot be written; the session keeps its in-memory store.
bool save_toggles(const assistant::FunctionToggles& toggles);

}  // namespace app

# src/assistant — behind seam D (architecture-seams section 5). Links core
# only: no Qt, no engine. Holds the Assistant key store, the Function
# registry, the toggle store and the Function file builder; the LLM
# transport (#16) lands here too.
add_library(assistant STATIC
  key_store.cpp
  registry.cpp
  toggles.cpp
  function_file.cpp
)
target_include_directories(assistant PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The config directory derives from the app id, never a product name
# (ADR-008, core document 8.1).
target_compile_definitions(assistant PRIVATE FCS_APP_ID_STR="${FCS_APP_ID}")
target_link_libraries(assistant PUBLIC core)

if(WIN32)
  # DPAPI, for keeping the key encrypted at rest under the user's login.
  target_link_libraries(assistant PRIVATE crypt32)
  target_compile_definitions(assistant PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

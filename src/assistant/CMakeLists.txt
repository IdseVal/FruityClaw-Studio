# src/assistant — the Function registry, the toggle store and the Function
# file builder (architecture-seams seam D). No Qt, no engine link, no I/O.
add_library(assistant STATIC
  registry.cpp
  toggles.cpp
  function_file.cpp
)
target_include_directories(assistant PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

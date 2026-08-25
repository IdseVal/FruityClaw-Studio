# src/assistant — the Function registry, the Function file and the Assistant
# transport seam (architecture-seams section 5). Links core only: no Qt, no
# engine. It computes Deltas; it never writes.
add_library(assistant STATIC
  schema.cpp
  selector.cpp
  registry.cpp
  function_file.cpp
  session.cpp
)
target_include_directories(assistant PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(assistant PUBLIC core)

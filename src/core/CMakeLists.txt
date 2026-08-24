# src/core — the section 5 domain as data plus the operations on it.
# Depends on nothing else in the project. No Qt, no audio library, no I/O.
add_library(core STATIC
  ids.cpp
  ops.cpp
  history.cpp
  arrangement_functions.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

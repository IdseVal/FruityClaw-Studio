# src/persistence — Project save and load (issue #13). Links core only; no
# Qt, no audio library. The file magic derives from FCS_APP_ID and nothing
# else (architecture-seams section 6, core document 8.1).
add_library(persistence STATIC
  project_file.cpp
)
target_include_directories(persistence PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(persistence PRIVATE FCS_APP_ID_STR="${FCS_APP_ID}")
target_link_libraries(persistence PUBLIC core)

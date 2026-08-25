# src/ui — Qt. The ONLY directory that links Qt (architecture-seams rule 2).
# Links core and assistant; the engine is reached through the TransportPort seam.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(CMAKE_AUTOMOC ON)

add_library(ui STATIC
  arrangement_view.cpp
  pattern_palette.cpp
  sample_browser.cpp
  assistant_panel.cpp
  main_window.cpp
)
target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ui PUBLIC core assistant Qt6::Widgets)

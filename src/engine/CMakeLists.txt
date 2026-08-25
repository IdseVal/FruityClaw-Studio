# src/engine — realtime playback: the baked render model, transport and voices,
# and the recorder that captures the callback's input side.
# Links core only. No Qt, no audio device library; the audio adapter calls
# Engine::render from its callback.
add_library(engine STATIC
  render_model.cpp
  engine.cpp
  recorder.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(engine PUBLIC core)

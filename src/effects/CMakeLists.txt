# src/effects — the six stock Effects of ADR-001 as Processor adapters.
# Links engine (for Processor) and is the only target that may include the
# vendored Airwindows or peaklim sources (architecture-seams rule 3).
set(THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)

set(AIRWINDOWS_PLUGINS Parametric Pressure6 Verbity2 TapeDelay2 Distortion)
set(AIRWINDOWS_SOURCES)
foreach(plugin ${AIRWINDOWS_PLUGINS})
  set(main ${THIRD_PARTY}/airwindows/${plugin}/${plugin}.cpp)
  set(proc ${THIRD_PARTY}/airwindows/${plugin}/${plugin}Proc.cpp)
  # Every plugin defines a global createEffectInstance(); rename each so the
  # five link into one library without touching the vendored files.
  set_source_files_properties(${main} PROPERTIES
    COMPILE_DEFINITIONS createEffectInstance=airwindows_create_${plugin})
  list(APPEND AIRWINDOWS_SOURCES ${main} ${proc})
endforeach()

add_library(effects STATIC
  stock_effects.cpp
  airwindows_eq.cpp
  airwindows_compressor.cpp
  airwindows_reverb.cpp
  airwindows_delay.cpp
  airwindows_distortion.cpp
  peaklim_limiter.cpp
  ${AIRWINDOWS_SOURCES}
  ${THIRD_PARTY}/peaklim/peaklim.cc
)

target_include_directories(effects
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
  PRIVATE ${THIRD_PARTY} ${THIRD_PARTY}/airwindows/shim)
target_link_libraries(effects PUBLIC engine)

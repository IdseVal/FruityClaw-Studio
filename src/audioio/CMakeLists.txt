# src/audioio — the AudioDevice seam and its PortAudio adapter. The only
# directory that may include a PortAudio header (architecture-seams rule 3).
#
# ADR-008 pins PortAudio to an explicit commit (the v19.7.0 release commit)
# via FetchContent; vendoring the pinned source under third_party/ is a
# follow-up recorded in the PR.
include(FetchContent)

set(PA_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  portaudio
  # The v19.7.0 release commit, pinned by content hash.
  URL https://github.com/PortAudio/portaudio/archive/147dd722548358763a8b649b3e4b41dfffbcfbb6.tar.gz
  URL_HASH SHA256=95457b809ce60d4d4790f84bb692e271f644e59d8adf96feb988c89ab52a506a
)
FetchContent_MakeAvailable(portaudio)

add_library(audioio STATIC
  portaudio_device.cpp
)
target_include_directories(audioio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(audioio PUBLIC core PRIVATE portaudio_static)

cmake_minimum_required(VERSION 3.24)

# ADR-008: the display name is disposable and the app id is permanent. Nothing
# below main() may use FCS_PRODUCT_NAME; identifiers and paths derive from
# FCS_APP_ID only. The app id value is an owner decision (ADR-008 open item 1);
# code depends on the symbol, not the string.
set(FCS_PRODUCT_NAME "Studio" CACHE STRING "Display name only. Disposable.")
set(FCS_APP_ID "studio" CACHE STRING "Stable neutral identifier. Never shown to a user.")

# C is enabled for vendored C dependencies (PortAudio).
project(${FCS_APP_ID} VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FCS_BUILD_UI    "Build the Qt user interface"          ON)
option(FCS_BUILD_AUDIO "Build the audio device layer"        ON)
option(FCS_BUILD_TESTS "Build the test suite"                ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src/core)
add_subdirectory(src/engine)
add_subdirectory(src/assistant)

if(FCS_BUILD_AUDIO)
  add_subdirectory(src/audioio)
endif()

if(FCS_BUILD_UI)
  add_subdirectory(src/ui)
  add_subdirectory(src/app)
endif()

if(FCS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(FetchContent)
FetchContent_Declare(
  catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.2.tar.gz
  URL_HASH SHA256=269543a49eb76f40b3f93ff231d4c24c27a7e16c90e47d2e45bcc564de470c6e
)
FetchContent_MakeAvailable(catch2)

add_executable(core_tests
  test_history.cpp
  test_arrangement_functions.cpp
  test_engine.cpp
  test_engine_stress.cpp
  test_persistence.cpp
)
target_link_libraries(core_tests PRIVATE core engine persistence Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(core_tests)

# The Qt interaction suite from the confirmed FMEA table
# (FMEA-arrangement-view.md). Only built with the UI; supplies its own main
# so a QApplication exists, and forces the offscreen platform itself.
if(TARGET ui)
  find_package(Qt6 COMPONENTS Test QUIET)
  if(Qt6Test_FOUND)
    add_executable(ui_tests test_ui.cpp)
    target_link_libraries(ui_tests PRIVATE ui Catch2::Catch2 Qt6::Test)
    # Qt's DLLs are not on PATH on Windows; point discovery and the tests at
    # the installed Qt bin directory.
    catch_discover_tests(ui_tests DL_PATHS "$<TARGET_FILE_DIR:Qt6::Core>")
  else()
    message(STATUS "Qt6Test not found; ui_tests skipped")
  endif()
endif()

# Invariant I10: no product name and no retired vocabulary in identifiers.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME identifier_lint
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/identifier_lint.py
                   ${CMAKE_SOURCE_DIR}/src)
endif()

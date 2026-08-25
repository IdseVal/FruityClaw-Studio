# src/app — main() and the composition root: the only place adapters are
# constructed, and the only translation unit that sees FCS_PRODUCT_NAME.
add_executable(app
  main.cpp
  audio_session.cpp
  demo_project.cpp
  generation_file.cpp
)
target_compile_definitions(app PRIVATE FCS_PRODUCT_NAME_STR="${FCS_PRODUCT_NAME}"
                                       FCS_APP_ID_STR="${FCS_APP_ID}")
target_link_libraries(app PRIVATE core engine audioio ui)
if(WIN32)
  target_link_libraries(app PRIVATE crypt32)  # DPAPI seals the generation key
endif()
set_target_properties(app PROPERTIES OUTPUT_NAME ${FCS_APP_ID})

# src/app — main() and the composition root: the only place adapters are
# constructed, and the only translation unit that sees FCS_PRODUCT_NAME.
add_executable(app
  main.cpp
  demo_project.cpp
)
target_compile_definitions(app PRIVATE FCS_PRODUCT_NAME_STR="${FCS_PRODUCT_NAME}")
target_link_libraries(app PRIVATE core engine audioio assistant ui)
set_target_properties(app PROPERTIES OUTPUT_NAME ${FCS_APP_ID})

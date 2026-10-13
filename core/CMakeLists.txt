# ESP-IDF component registration; the host build continues to use ../CMakeLists.txt.
idf_component_register(SRCS "music_core.c" INCLUDE_DIRS ".")
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
target_compile_options(${COMPONENT_LIB} PUBLIC -fno-fast-math)
target_link_libraries(${COMPONENT_LIB} PUBLIC m)

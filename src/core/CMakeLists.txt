add_library(gmax_core STATIC
  version.cpp)

target_include_directories(gmax_core PUBLIC "${CMAKE_SOURCE_DIR}/src")

gmax_set_warnings(gmax_core)
gmax_set_sanitizers(gmax_core)

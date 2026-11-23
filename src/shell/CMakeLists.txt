add_executable(gmax
  main.cpp)

target_link_libraries(gmax PRIVATE gmax_core)

gmax_set_warnings(gmax)
gmax_set_sanitizers(gmax)

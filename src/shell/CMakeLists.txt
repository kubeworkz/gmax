add_library(gmax_shell STATIC
  cli.cpp)

target_link_libraries(gmax_shell PUBLIC gmax_core)

gmax_set_warnings(gmax_shell)
gmax_set_sanitizers(gmax_shell)

add_executable(gmax
  main.cpp)

target_link_libraries(gmax PRIVATE gmax_shell)

gmax_set_warnings(gmax)
gmax_set_sanitizers(gmax)

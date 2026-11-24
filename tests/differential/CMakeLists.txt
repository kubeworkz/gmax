add_executable(fault_diff
  fault_diff.cpp)

target_link_libraries(fault_diff PRIVATE gmax_core)

gmax_set_warnings(fault_diff)
gmax_set_sanitizers(fault_diff)

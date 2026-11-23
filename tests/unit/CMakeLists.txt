add_executable(version_test
  version_test.cpp)

target_link_libraries(version_test PRIVATE gmax_core)

gmax_set_warnings(version_test)
gmax_set_sanitizers(version_test)

add_test(NAME version COMMAND version_test)

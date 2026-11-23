add_executable(version_test
  version_test.cpp)

target_link_libraries(version_test PRIVATE gmax_core)

gmax_set_warnings(version_test)
gmax_set_sanitizers(version_test)

add_test(NAME version COMMAND version_test)

add_executable(db_test
  db_test.cpp)

target_link_libraries(db_test PRIVATE gmax_core)

gmax_set_warnings(db_test)
gmax_set_sanitizers(db_test)

add_test(NAME db COMMAND db_test)

add_executable(lib_test
  lib_test.cpp)

target_link_libraries(lib_test PRIVATE gmax_core)

gmax_set_warnings(lib_test)
gmax_set_sanitizers(lib_test)

add_test(NAME lib COMMAND lib_test)

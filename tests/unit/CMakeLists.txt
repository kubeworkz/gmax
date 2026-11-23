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

add_executable(sim_test
  sim_test.cpp)

target_link_libraries(sim_test PRIVATE gmax_core)

gmax_set_warnings(sim_test)
gmax_set_sanitizers(sim_test)

add_test(NAME sim COMMAND sim_test)

add_executable(fault_test
  fault_test.cpp)

target_link_libraries(fault_test PRIVATE gmax_core)

gmax_set_warnings(fault_test)
gmax_set_sanitizers(fault_test)

add_test(NAME fault COMMAND fault_test)

add_executable(cli_test
  cli_test.cpp)

target_link_libraries(cli_test PRIVATE gmax_shell)

gmax_set_warnings(cli_test)
gmax_set_sanitizers(cli_test)

add_test(NAME cli COMMAND cli_test)

add_executable(fault_list_test
  fault_list_test.cpp)

target_link_libraries(fault_list_test PRIVATE gmax_core)

gmax_set_warnings(fault_list_test)
gmax_set_sanitizers(fault_list_test)

add_test(NAME fault_list COMMAND fault_list_test)

add_executable(yosys_test
  yosys_test.cpp)

target_link_libraries(yosys_test PRIVATE gmax_core)

target_compile_definitions(yosys_test PRIVATE GMAX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

gmax_set_warnings(yosys_test)
gmax_set_sanitizers(yosys_test)

add_test(NAME yosys COMMAND yosys_test)

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

add_executable(atpg_cli_fixture
  atpg_cli_fixture.cpp)

target_link_libraries(atpg_cli_fixture PRIVATE gmax_core)

gmax_set_warnings(atpg_cli_fixture)
gmax_set_sanitizers(atpg_cli_fixture)

add_executable(fault_cli_test
  fault_cli_test.cpp)

target_link_libraries(fault_cli_test PRIVATE gmax_shell)

gmax_set_warnings(fault_cli_test)
gmax_set_sanitizers(fault_cli_test)

add_test(NAME fault_cli COMMAND fault_cli_test)

add_executable(fault_cli_fixture
  fault_cli_fixture.cpp)

target_link_libraries(fault_cli_fixture PRIVATE gmax_core)

gmax_set_warnings(fault_cli_fixture)
gmax_set_sanitizers(fault_cli_fixture)

add_executable(fault_list_test
  fault_list_test.cpp)

target_link_libraries(fault_list_test PRIVATE gmax_core)

gmax_set_warnings(fault_list_test)
gmax_set_sanitizers(fault_list_test)

add_test(NAME fault_list COMMAND fault_list_test)

add_executable(fault_file_test
  fault_file_test.cpp)

target_link_libraries(fault_file_test PRIVATE gmax_core)

gmax_set_warnings(fault_file_test)
gmax_set_sanitizers(fault_file_test)

add_test(NAME fault_file COMMAND fault_file_test)

add_executable(implication_test
  implication_test.cpp)

target_link_libraries(implication_test PRIVATE gmax_core)

gmax_set_warnings(implication_test)
gmax_set_sanitizers(implication_test)

add_test(NAME implication COMMAND implication_test)

add_executable(podem_test
  podem_test.cpp)

target_link_libraries(podem_test PRIVATE gmax_core)

gmax_set_warnings(podem_test)
gmax_set_sanitizers(podem_test)

add_test(NAME podem COMMAND podem_test)

add_executable(sat_test
  sat_test.cpp)

target_link_libraries(sat_test PRIVATE gmax_core)

gmax_set_warnings(sat_test)
gmax_set_sanitizers(sat_test)

add_test(NAME sat COMMAND sat_test)

add_executable(yosys_test
  yosys_test.cpp)

target_link_libraries(yosys_test PRIVATE gmax_core)

target_compile_definitions(yosys_test PRIVATE GMAX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

gmax_set_warnings(yosys_test)
gmax_set_sanitizers(yosys_test)

add_test(NAME yosys COMMAND yosys_test)

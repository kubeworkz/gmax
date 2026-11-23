add_subdirectory(unit)

# WP-010's executable contract: the schema only matters if the installed gmax
# wires it up. cli_test owns the exhaustive parser and exit-code cases; these
# ctest entries check the process-level flags.
add_test(NAME cli.version COMMAND gmax --version)
set_tests_properties(cli.version PROPERTIES
  PASS_REGULAR_EXPRESSION "gmax [0-9]+\\.[0-9]+\\.[0-9]+")

add_test(NAME cli.help COMMAND gmax -help)
set_tests_properties(cli.help PROPERTIES
  PASS_REGULAR_EXPRESSION "usage: gmax <command>")

add_test(NAME cli.help-command COMMAND gmax help)
set_tests_properties(cli.help-command PROPERTIES
  PASS_REGULAR_EXPRESSION "Commands:")

add_test(NAME cli.no-args COMMAND gmax)
set_tests_properties(cli.no-args PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli.unknown-command COMMAND gmax no-such-command)
set_tests_properties(cli.unknown-command PROPERTIES WILL_FAIL TRUE)

# The command registry only matters if the real binary wires it up. The script
# builds a .dfdb fixture, runs write_faults through the executable, and checks
# the generated artifact plus the usage and design exit codes.
add_test(NAME cli.write-faults
  COMMAND ${CMAKE_COMMAND}
    -DGMAX_BINARY=$<TARGET_FILE:gmax>
    -DFIXTURE_BINARY=$<TARGET_FILE:fault_cli_fixture>
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-write-faults
    -P ${CMAKE_CURRENT_SOURCE_DIR}/process/write_faults.cmake)

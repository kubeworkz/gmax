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

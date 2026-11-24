add_subdirectory(unit)
add_subdirectory(differential)

find_program(GMAX_PYTHON_EXECUTABLE NAMES python3 python)

# G0 evidence retention: process tests copy their reports here before deleting
# their work directories. The default lives inside the build tree so a test run
# never writes generated files into the checkout; the gate job overrides it with
# the checkout's reports/g0 path so the retained artifact location stays stable.
# The tree is generated, not tracked; the gate pack
# records the artifact hashes.
set(GMAX_REPORTS_DIR "${CMAKE_BINARY_DIR}/reports/g0" CACHE PATH
  "Directory where process tests preserve coverage and run reports")

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

# The ATPG command must load both artifacts, report each fault's status and
# budget counters, and return ATPG-incomplete when a fault exhausts its budget.
add_test(NAME cli.run-atpg
  COMMAND ${CMAKE_COMMAND}
    -DGMAX_BINARY=$<TARGET_FILE:gmax>
    -DFIXTURE_BINARY=$<TARGET_FILE:atpg_cli_fixture>
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-run-atpg
    -P ${CMAKE_CURRENT_SOURCE_DIR}/process/run_atpg.cmake)

# WP-011's benchmark exit check: the pinned Yosys frontend, fault collapse, and
# PODEM are exercised on the real ISCAS'85 netlists, including the coverage
# denominators, hashes, manifest, and repeated-run report determinism.
add_test(NAME cli.iscas85
  COMMAND ${CMAKE_COMMAND}
    -DGMAX_BINARY=$<TARGET_FILE:gmax>
    -DGMAX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-iscas85
    -DGMAX_REPORTS_DIR=${GMAX_REPORTS_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/process/iscas85.cmake)

# G0.3 / ADR-011: c432 run with one worker thread and with four must write
# identical coverage and manifests that differ only in the recorded count. The
# test shares the pinned-Yosys prerequisite with the ISCAS'85 test and is made
# mandatory by GMAX_G0_3_GATE=1 in the gate job.
add_test(NAME cli.threads
  COMMAND ${CMAKE_COMMAND}
    -DGMAX_BINARY=$<TARGET_FILE:gmax>
    -DGMAX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-threads
    -DGMAX_REPORTS_DIR=${GMAX_REPORTS_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/process/threads.cmake)

# G0.7 / architecture 10.4: the 64-lane fault simulator must match the scalar
# reference on every net. Raw faults on all c432 nets are simulated over a fixed
# pattern set and any disagreement fails the test. It shares the pinned-Yosys
# prerequisite with the ISCAS'85 test and is made mandatory by GMAX_G0_7_GATE=1
# in the gate job.
add_test(NAME sim.fault-diff
  COMMAND ${CMAKE_COMMAND}
    -DGMAX_BINARY=$<TARGET_FILE:gmax>
    -DDIFF_BINARY=$<TARGET_FILE:fault_diff>
    -DGMAX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/fault-diff
    -P ${CMAKE_CURRENT_SOURCE_DIR}/process/fault_diff.cmake)

# G0.2 requires an independent SAT cross-check, not just a second call into
# our own solver. This runs the two c432 smoke cases through Yosys `sat` on a
# two-copy miter and, when GMAX_MINISAT is set, through the pinned MiniSat
# 2.2.1 binary on the same CNFs. Both engines must return the same verdicts.
# The default developer run may omit GMAX_MINISAT and then uses Yosys only;
# GMAX_G0_2_GATE=1 makes MiniSat mandatory so the gate cannot pass on Yosys
# alone.
add_test(NAME cli.sat-smoke
  COMMAND ${CMAKE_COMMAND}
    -DGMAX_BINARY=$<TARGET_FILE:gmax>
    -DGMAX_SOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DPYTHON_EXECUTABLE=${GMAX_PYTHON_EXECUTABLE}
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-sat-smoke
    -DGMAX_REPORTS_DIR=${GMAX_REPORTS_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/process/sat_smoke.cmake)

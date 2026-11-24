# WP-007/WP-010 process-level check: the installed gmax binary must register
# run_atpg, load a .dfdb and .faults file, report every fault outcome, and
# return ATPG-incomplete when a per-fault budget aborts. The fixture has three
# faults: one direct detection, one backtracking detection, and one tied site
# that cannot be activated.

foreach(required GMAX_BINARY FIXTURE_BINARY WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(database "${WORK_DIR}/design.dfdb")
set(faults "${WORK_DIR}/design.faults")

execute_process(
    COMMAND "${FIXTURE_BINARY}" "${database}" "${faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${result}): ${fixture_err}${fixture_out}")
endif()

# Default budgets: the direct fault and the backtracking fault are detected,
# and the tied fault is untestable. The backtracking fault contributes one
# failed branch and two decisions.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "run_atpg default run returned ${result}: ${err}")
endif()
if(NOT err STREQUAL "")
    message(FATAL_ERROR "run_atpg wrote to stderr: ${err}")
endif()
if(NOT out MATCHES "raw faults: 3")
    message(FATAL_ERROR "missing raw fault count in stdout: ${out}")
endif()
if(NOT out MATCHES "collapsed faults: 3")
    message(FATAL_ERROR "missing collapsed fault count in stdout: ${out}")
endif()
if(NOT out MATCHES "fault [0-9]+ 0: detected decisions=0 backtracks=0")
    message(FATAL_ERROR "missing direct detection line: ${out}")
endif()
if(NOT out MATCHES "fault [0-9]+ 0: detected decisions=2 backtracks=1")
    message(FATAL_ERROR "missing backtracking detection line: ${out}")
endif()
if(NOT out MATCHES "fault [0-9]+ 0: untestable decisions=0 backtracks=0")
    message(FATAL_ERROR "missing untestable line: ${out}")
endif()
if(NOT out MATCHES "detected: 2")
    message(FATAL_ERROR "wrong detected count: ${out}")
endif()
if(NOT out MATCHES "untestable: 1")
    message(FATAL_ERROR "wrong untestable count: ${out}")
endif()
if(NOT out MATCHES "aborted: 0")
    message(FATAL_ERROR "wrong aborted count: ${out}")
endif()
if(NOT out MATCHES "backtracks: 1")
    message(FATAL_ERROR "wrong backtrack counter: ${out}")
endif()
if(NOT out MATCHES "decisions: 2")
    message(FATAL_ERROR "wrong decision counter: ${out}")
endif()

# A zero backtrack budget aborts the backtracking fault; the command must
# report it, count it, and return exit code 4 rather than silently dropping it.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}" 0
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "4")
    message(FATAL_ERROR "budget-exhausted run returned ${result}, expected 4: ${err}")
endif()
if(NOT err STREQUAL "")
    message(FATAL_ERROR "budget-exhausted run wrote to stderr: ${err}")
endif()
if(NOT out MATCHES "fault [0-9]+ 0: aborted decisions=1 backtracks=1")
    message(FATAL_ERROR "missing aborted line: ${out}")
endif()
if(out MATCHES "\\[sat\\]")
    message(FATAL_ERROR "zero-backtrack run invoked the SAT closer: ${out}")
endif()
if(NOT out MATCHES "detected: 1")
    message(FATAL_ERROR "wrong detected count after abort: ${out}")
endif()
if(NOT out MATCHES "untestable: 1")
    message(FATAL_ERROR "wrong untestable count after abort: ${out}")
endif()
if(NOT out MATCHES "aborted: 1")
    message(FATAL_ERROR "wrong aborted count after abort: ${out}")
endif()
if(NOT out MATCHES "backtracks: 1")
    message(FATAL_ERROR "wrong backtrack counter after abort: ${out}")
endif()
if(NOT out MATCHES "decisions: 1")
    message(FATAL_ERROR "wrong decision counter after abort: ${out}")
endif()

# The executable must register the command and advertise both optional budgets.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg -help
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "run_atpg -help returned ${result}: ${err}")
endif()
if(NOT out MATCHES "usage: gmax run_atpg <design> <faults>")
    message(FATAL_ERROR "run_atpg usage was not generated: ${out}")
endif()
if(NOT out MATCHES "max-backtracks" OR NOT out MATCHES "max-time-ms")
    message(FATAL_ERROR "budget arguments are missing from help: ${out}")
endif()

# Missing required arguments are usage errors, not unknown commands.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "missing argument returned ${result}, expected 1: ${err}")
endif()
if(NOT err MATCHES "missing argument '<faults>'")
    message(FATAL_ERROR "missing-argument diagnostic was not specific: ${err}")
endif()

# A missing design is a design/import error.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/missing.dfdb" "${faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "missing design returned ${result}, expected 2: ${err}")
endif()
if(NOT err MATCHES "cannot load design")
    message(FATAL_ERROR "missing-design diagnostic was not specific: ${err}")
endif()

# A missing or mismatched fault list is also a design error.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${WORK_DIR}/missing.faults"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "missing faults returned ${result}, expected 2: ${err}")
endif()
if(NOT err MATCHES "cannot read faults")
    message(FATAL_ERROR "missing-faults diagnostic was not specific: ${err}")
endif()

# Budget values are parsed before running ATPG.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}" nope
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "invalid backtrack budget returned ${result}, expected 1: ${err}")
endif()
if(NOT err MATCHES "invalid max-backtracks 'nope'")
    message(FATAL_ERROR "invalid-backtrack diagnostic was not specific: ${err}")
endif()

execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}" 0 nope
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "invalid time budget returned ${result}, expected 1: ${err}")
endif()
if(NOT err MATCHES "invalid max-time-ms 'nope'")
    message(FATAL_ERROR "invalid-time diagnostic was not specific: ${err}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

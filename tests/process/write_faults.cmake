# WP-006/WP-010 process-level check: the installed gmax binary must register
# write_faults, produce a .faults file from a saved .dfdb, print both counts,
# and return the architecture exit codes for usage and design failures.

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
    COMMAND "${FIXTURE_BINARY}" "${database}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${result}): ${fixture_err}${fixture_out}")
endif()

execute_process(
    COMMAND "${GMAX_BINARY}" write_faults "${database}" "${faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "write_faults failed (${result}): ${err}")
endif()
if(NOT err STREQUAL "")
    message(FATAL_ERROR "write_faults wrote to stderr: ${err}")
endif()
if(NOT out MATCHES "raw faults: 6")
    message(FATAL_ERROR "missing raw count in stdout: ${out}")
endif()
if(NOT out MATCHES "collapsed faults: 2")
    message(FATAL_ERROR "missing collapsed count in stdout: ${out}")
endif()
if(NOT out MATCHES "wrote '")
    message(FATAL_ERROR "missing output confirmation in stdout: ${out}")
endif()

if(NOT EXISTS "${faults}")
    message(FATAL_ERROR "write_faults did not create ${faults}")
endif()
file(READ "${faults}" contents)
if(NOT contents MATCHES "gmax-faults 1")
    message(FATAL_ERROR "missing .faults version header")
endif()
if(NOT contents MATCHES "netlist [0-9a-f]+")
    message(FATAL_ERROR "missing netlist hash")
endif()
if(NOT contents MATCHES "fault-list [0-9a-f]+")
    message(FATAL_ERROR "missing fault-list hash")
endif()
if(NOT contents MATCHES "raw-count 6")
    message(FATAL_ERROR "wrong raw count in .faults header")
endif()
if(NOT contents MATCHES "collapsed-count 2")
    message(FATAL_ERROR "wrong collapsed count in .faults header")
endif()
file(STRINGS "${faults}" raw_entries REGEX "^R ")
list(LENGTH raw_entries raw_count)
if(NOT raw_count EQUAL 6)
    message(FATAL_ERROR "expected 6 raw entries, found ${raw_count}")
endif()
file(STRINGS "${faults}" collapsed_entries REGEX "^C ")
list(LENGTH collapsed_entries collapsed_count)
if(NOT collapsed_count EQUAL 2)
    message(FATAL_ERROR "expected 2 collapsed entries, found ${collapsed_count}")
endif()

# A missing output argument is a usage error, not an unknown command: this also
# proves main registered the command rather than falling through the registry.
execute_process(
    COMMAND "${GMAX_BINARY}" write_faults "${database}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "1")
    message(FATAL_ERROR "missing argument returned ${result}, expected 1: ${err}")
endif()
if(NOT err MATCHES "missing argument '<faults>'")
    message(FATAL_ERROR "missing-argument diagnostic was not specific: ${err}")
endif()

# A nonexistent input is a design/import error and must not create an output.
set(missing_database "${WORK_DIR}/missing.dfdb")
set(missing_faults "${WORK_DIR}/missing.faults")
execute_process(
    COMMAND "${GMAX_BINARY}" write_faults "${missing_database}" "${missing_faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "invalid input returned ${result}, expected 2: ${err}")
endif()
if(NOT err MATCHES "cannot load design")
    message(FATAL_ERROR "invalid-input diagnostic was not specific: ${err}")
endif()
if(EXISTS "${missing_faults}")
    message(FATAL_ERROR "missing input created an output file")
endif()

# A corrupt database is also a design error and must not create an output.
set(invalid_database "${WORK_DIR}/invalid.dfdb")
set(invalid_faults "${WORK_DIR}/invalid.faults")
file(WRITE "${invalid_database}" "not a dfdb")
execute_process(
    COMMAND "${GMAX_BINARY}" write_faults "${invalid_database}" "${invalid_faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "corrupt input returned ${result}, expected 2: ${err}")
endif()
if(NOT err MATCHES "cannot load design")
    message(FATAL_ERROR "corrupt-input diagnostic was not specific: ${err}")
endif()
if(EXISTS "${invalid_faults}")
    message(FATAL_ERROR "corrupt input created an output file")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

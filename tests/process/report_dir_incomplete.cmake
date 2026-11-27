# ADR-013 s4: an incomplete-constraints run refuses before any report is
# written, and the deferred creation of the report directory means no directory
# is left behind.

foreach(required GMAX_BINARY FIXTURE_BINARY WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(database "${WORK_DIR}/design.dfdb")
set(faults "${WORK_DIR}/design.faults")
set(report_dir "${WORK_DIR}/report-dir")

execute_process(
    COMMAND "${FIXTURE_BINARY}" "${database}" "${faults}" "incomplete"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${result}): ${fixture_err}${fixture_out}")
endif()

execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}" 100000 0 "${report_dir}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "incomplete run returned ${result}, expected 2: ${err}${out}")
endif()
if(NOT err MATCHES "design constraints are incomplete")
    message(FATAL_ERROR "missing incomplete diagnostic: ${err}")
endif()
if(EXISTS "${report_dir}")
    message(FATAL_ERROR "report directory was created by a refused run: ${report_dir}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
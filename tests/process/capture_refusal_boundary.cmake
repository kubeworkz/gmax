# ADR-013 section 4's granularity boundary for condition 5: the clock-level
# exclusion refuses only when it empties the clock list. Here clk_a clocks the
# design's only flop and clk_b clocks nothing, so the procedure is still
# appliable and the run grades normally.
#
# This is the boundary the ADR calls out: a fix that counted any dropped clock
# instead of an emptied list would turn this run into a refusal.

foreach(required GMAX_BINARY FIXTURE_BINARY WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(database "${WORK_DIR}/design.dfdb")
set(faults "${WORK_DIR}/design.faults")
set(reports "${WORK_DIR}/reports")

execute_process(
    COMMAND "${FIXTURE_BINARY}" boundary "${database}" "${faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${result}): ${fixture_err}${fixture_out}")
endif()

execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}" 100000 0 "${reports}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
        "one non-clocking clock must not refuse the run: returned ${result}: ${err}${out}")
endif()
if(NOT "${err}" STREQUAL "")
    message(FATAL_ERROR "the boundary run wrote to stderr: ${err}")
endif()
if(NOT out MATCHES "raw faults:")
    message(FATAL_ERROR "the boundary run printed no fault count: ${out}")
endif()
if(NOT EXISTS "${reports}/coverage.json")
    message(FATAL_ERROR "the boundary run wrote no coverage report")
endif()

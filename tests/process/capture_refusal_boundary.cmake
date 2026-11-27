# ADR-013 section 3's granularity boundary: the clock-level exclusion refuses only
# when it empties the clock list. Here clk_a clocks the design's only flop and
# clk_b clocks nothing, so clk_b is dropped, the drop is reported as a warning,
# and the run grades normally.
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
# The drop is reported rather than refused: exactly one warning, naming the idle
# clock and not the one that clocks state.
string(REGEX MATCHALL "warning:" warnings "${err}")
list(LENGTH warnings warning_count)
if(NOT warning_count EQUAL 1)
    message(FATAL_ERROR "expected one warning, found ${warning_count}: ${err}")
endif()
string(FIND "${err}"
    "gmax: warning: clock 'clk_b' clocks no state-holding gate and was left out of the capture procedure"
    warning_position)
if(warning_position EQUAL -1)
    message(FATAL_ERROR "the warning does not name the idle clock: ${err}")
endif()
string(FIND "${err}" "clock 'clk_a'" wrong_position)
if(NOT wrong_position EQUAL -1)
    message(FATAL_ERROR "the warning names a clock that clocks state: ${err}")
endif()
if(NOT out MATCHES "raw faults:")
    message(FATAL_ERROR "the boundary run printed no fault count: ${out}")
endif()
if(NOT EXISTS "${reports}/coverage.json")
    message(FATAL_ERROR "the boundary run wrote no coverage report")
endif()

# ADR-013 section 6: the run applied the one surviving clock, so the reports claim
# it as enforced, simultaneous (one cycle with one group) and derived, with the
# procedure body listing the clock that is actually pulsed.
file(READ "${reports}/coverage.json" coverage_text)
string(FIND "${coverage_text}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"simultaneous\",\"derived\":true,\"hash\":\""
    capture_position)
if(capture_position EQUAL -1)
    message(FATAL_ERROR "the coverage report claims no enforced procedure: ${coverage_text}")
endif()
# ADR-013 section 6: the run applied the procedure, so the enforced list names the
# clock-group model beside whatever else ran.
string(FIND "${coverage_text}" "\"enforced\":[\"clock_groups\"]" enforced_position)
if(enforced_position EQUAL -1)
    message(FATAL_ERROR "the coverage report claims no applied clock groups: ${coverage_text}")
endif()
# ADR-013 section 5: this constraint set retains no false path, so the run examined
# none. The enforced list does not name them, and the warning list is present and
# empty rather than omitted, which is how a reader tells "no finding" from "not
# evaluated".
string(FIND "${coverage_text}" "\"enforced\":[\"clock_groups\",\"false_paths\"]"
    enforced_false_position)
if(NOT enforced_false_position EQUAL -1)
    message(FATAL_ERROR
        "the report claims false paths the run never examined: ${coverage_text}")
endif()
string(FIND "${coverage_text}" "\"false_paths\":[]" empty_false_position)
if(empty_false_position EQUAL -1)
    message(FATAL_ERROR "the report carries no empty warning list: ${coverage_text}")
endif()

if(NOT EXISTS "${reports}/manifest.json")
    message(FATAL_ERROR "the boundary run wrote no manifest")
endif()
file(READ "${reports}/manifest.json" manifest_text)
string(FIND "${manifest_text}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"simultaneous\",\"derived\":true,\"hash\":\""
    manifest_position)
if(manifest_position EQUAL -1)
    message(FATAL_ERROR "the manifest claims no enforced procedure: ${manifest_text}")
endif()
string(FIND "${manifest_text}" "\"cycles\":[{\"groups\":[{\"clocks\":[\"clk_a\"],\"edge\":0}]}]"
    cycles_position)
if(cycles_position EQUAL -1)
    message(FATAL_ERROR "the manifest does not carry the applied procedure: ${manifest_text}")
endif()

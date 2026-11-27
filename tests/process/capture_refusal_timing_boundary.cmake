# ADR-013 section 4's condition 1/3 boundary: one group holding two clocks that the
# SDC declares together and that agree on period and waveform is a legal simultaneous
# capture, so the run grades. This is the companion the refusal tests need, because it
# is what proves condition 1 keys off the SDC's grouping, and condition 3 off a timing
# disagreement, rather than both off "more than one clock in a group".

foreach(required GMAX_BINARY FIXTURE_BINARY WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

function(check_contains text needle label)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: missing '${needle}' in:\n${text}")
    endif()
endfunction()

function(check_absent text needle label)
    string(FIND "${text}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${label}: unexpected '${needle}' in:\n${text}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(database "${WORK_DIR}/design.dfdb")
set(faults "${WORK_DIR}/design.faults")
set(reports "${WORK_DIR}/reports")

execute_process(
    COMMAND "${FIXTURE_BINARY}" independent "${database}" "${faults}"
    RESULT_VARIABLE fixture_result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${fixture_result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${fixture_result}): ${fixture_err}${fixture_out}")
endif()

# clk_a and clk_b are one declared group; clk_c gives set_clock_groups its second
# -group option. All three keep the same period and state no waveform.
set(sdc "${WORK_DIR}/paired.sdc")
file(WRITE "${sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 10 [get_ports clk_b]\n"
    "create_clock -name clk_c -period 10 [get_ports clk_c]\n"
    "set_clock_groups -asynchronous -name pair -group {clk_a clk_b} -group {clk_c}\n")

set(attached "${WORK_DIR}/attached.dfdb")
execute_process(
    COMMAND "${GMAX_BINARY}" read_sdc "${sdc}" --design "${database}" --output "${attached}"
    RESULT_VARIABLE attach_result
    OUTPUT_VARIABLE attach_out
    ERROR_VARIABLE attach_err)
if(NOT "${attach_result}" STREQUAL "0")
    message(FATAL_ERROR "read_sdc failed (${attach_result}): ${attach_err}${attach_out}")
endif()

file(WRITE "${WORK_DIR}/simultaneous.json"
    "{\"capture\": {\"kind\": \"simultaneous\", \"cycles\": [{\"groups\": [\n"
    "  {\"name\": \"pair\", \"clocks\": [\"clk_a\", \"clk_b\"]}]}]}}\n")

execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${attached}" "${faults}" 100000 0 "${reports}"
            --profile "${WORK_DIR}/simultaneous.json"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
        "two agreeing clocks in one declared group must grade, returned ${result}: ${err}${out}")
endif()
check_absent("${err}" "cannot apply the capture procedure" "the procedure applies")
check_absent("${err}" "simultaneous-across-groups" "condition 1 stays quiet")
check_absent("${err}" "group-timing-disagrees" "condition 3 stays quiet")
if(NOT out MATCHES "raw faults:")
    message(FATAL_ERROR "the boundary run printed no fault count: ${out}")
endif()

# Section 5: the applied procedure is the declared one, one cycle with one group that
# pulses both clocks, and no cycle was derived.
file(READ "${reports}/coverage.json" coverage_text)
check_contains("${coverage_text}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"simultaneous\",\"derived\":false,\"hash\":\""
    "the coverage report names the declared simultaneous procedure")
check_contains("${coverage_text}" "\"enforced\":[\"clock_groups\"]"
    "the run applied the clock groups")
file(READ "${reports}/manifest.json" manifest_text)
check_contains("${manifest_text}"
    "\"cycles\":[{\"groups\":[{\"clocks\":[\"clk_a\",\"clk_b\"],\"edge\":0}]}]"
    "the manifest carries both clocks in one group")

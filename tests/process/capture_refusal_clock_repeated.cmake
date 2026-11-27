# ADR-013 section 4 condition 4: a clock appears in more than one cycle, or twice in
# one cycle. Both positions are named, because the fix depends on which one is the
# typo. The last case is section 5's determinism companion: two conditions fire in one
# run, and repeated runs print the block byte for byte (ADR-011).

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

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(database "${WORK_DIR}/design.dfdb")
set(faults "${WORK_DIR}/design.faults")

execute_process(
    COMMAND "${FIXTURE_BINARY}" independent "${database}" "${faults}"
    RESULT_VARIABLE fixture_result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${fixture_result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${fixture_result}): ${fixture_err}${fixture_out}")
endif()

function(attach name sdc)
    execute_process(
        COMMAND "${GMAX_BINARY}" read_sdc "${WORK_DIR}/${sdc}"
                --design "${database}" --output "${WORK_DIR}/${name}_attached.dfdb"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE attach_out
        ERROR_VARIABLE attach_err)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: read_sdc failed (${result}): ${attach_err}${attach_out}")
    endif()
endfunction()

function(refuse name sdc profile finding count)
    attach("${name}" "${sdc}")
    file(REMOVE_RECURSE "${WORK_DIR}/${name}-report")
    execute_process(
        COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/${name}_attached.dfdb" "${faults}"
                100000 0 "${WORK_DIR}/${name}-report" --profile "${WORK_DIR}/${profile}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE run_out
        ERROR_VARIABLE run_err)
    if(NOT "${result}" STREQUAL "2")
        message(FATAL_ERROR "${name}: expected exit 2, got ${result}: ${run_err}${run_out}")
    endif()
    if(EXISTS "${WORK_DIR}/${name}-report")
        message(FATAL_ERROR "${name}: a refused run left a report directory")
    endif()
    string(REGEX MATCHALL "refusing to grade" headers "${run_err}")
    list(LENGTH headers header_count)
    if(NOT header_count EQUAL 1)
        message(FATAL_ERROR "${name}: expected one header, found ${header_count}: ${run_err}")
    endif()
    string(REGEX MATCHALL "${finding}" findings "${run_err}")
    list(LENGTH findings finding_count)
    if(NOT finding_count EQUAL count)
        message(FATAL_ERROR
            "${name}: expected ${count} of '${finding}', found ${finding_count}: ${run_err}")
    endif()
    if(run_out MATCHES "raw faults:")
        message(FATAL_ERROR "${name}: a refused run printed fault counts: ${run_out}")
    endif()
    set(RUN_ERR "${run_err}" PARENT_SCOPE)
endfunction()

# Every clock keeps the same timing, so condition 3 cannot fire and the repetition is
# the only finding.
file(WRITE "${WORK_DIR}/clocks.sdc"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 10 [get_ports clk_b]\n"
    "create_clock -name clk_c -period 10 [get_ports clk_c]\n")

# clk_a and clk_b are one declared group and their periods disagree on purpose, so the
# two-refusal case reports condition 3 beside condition 4.
file(WRITE "${WORK_DIR}/paired.sdc"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "create_clock -name clk_c -period 10 [get_ports clk_c]\n"
    "set_clock_groups -asynchronous -name pair -group {clk_a clk_b} -group {clk_c}\n")

file(WRITE "${WORK_DIR}/cross-cycle.json"
    "{\"capture\": {\"kind\": \"sequential\", \"cycles\": [\n"
    "  {\"groups\": [{\"clocks\": [\"clk_a\"], \"edge\": 0}]},\n"
    "  {\"groups\": [{\"clocks\": [\"clk_a\"], \"edge\": 0}]}]}}\n")

file(WRITE "${WORK_DIR}/same-cycle.json"
    "{\"capture\": {\"kind\": \"sequential\", \"cycles\": [\n"
    "  {\"groups\": [{\"clocks\": [\"clk_a\", \"clk_a\"], \"edge\": 0}]},\n"
    "  {\"groups\": [{\"clocks\": [\"clk_b\"], \"edge\": 0}]}]}}\n")

file(WRITE "${WORK_DIR}/two-refusals.json"
    "{\"capture\": {\"kind\": \"sequential\", \"cycles\": [\n"
    "  {\"groups\": [{\"clocks\": [\"clk_a\", \"clk_b\"], \"edge\": 0}]},\n"
    "  {\"groups\": [{\"clocks\": [\"clk_a\"], \"edge\": 0}]}]}}\n")

file(SHA256 "${database}" hash_before)

# 1. One clock in two cycles: both cycle numbers are named.
refuse(cross-cycle "clocks.sdc" "cross-cycle.json" "clock-repeated:" 1)
check_contains("${RUN_ERR}" "clock-repeated: 'clk_a' appears in cycle 0 and cycle 1"
    "cross-cycle repetition")

# 2. One clock twice in one cycle: the finding says so instead of naming that cycle
#    twice as a pair.
refuse(same-cycle "clocks.sdc" "same-cycle.json" "clock-repeated:" 1)
check_contains("${RUN_ERR}" "clock-repeated: 'clk_a' appears twice in cycle 0"
    "same-cycle repetition")

# 3. Two conditions in one run: both findings under one header, in contract order.
refuse(two-refusals "paired.sdc" "two-refusals.json" "clock-repeated:" 1)
check_contains("${RUN_ERR}" "group [clk_a, clk_b] mixes periods 10 ns (clk_a) and 20 ns (clk_b)"
    "the timing finding names the group")
string(FIND "${RUN_ERR}" "group-timing-disagrees:" timing_position)
string(FIND "${RUN_ERR}" "clock-repeated:" repeated_position)
if(timing_position EQUAL -1 OR repeated_position EQUAL -1)
    message(FATAL_ERROR "the two-refusal block is missing a condition: ${RUN_ERR}")
endif()
if(NOT timing_position LESS repeated_position)
    message(FATAL_ERROR "condition 3 must print before condition 4: ${RUN_ERR}")
endif()

# Determinism: the same inputs print the same block, byte for byte.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/two-refusals_attached.dfdb" "${faults}"
            100000 0 "${WORK_DIR}/two-refusals-report" --profile
            "${WORK_DIR}/two-refusals.json"
    RESULT_VARIABLE repeat_result
    OUTPUT_VARIABLE repeat_out
    ERROR_VARIABLE repeat_err)
if(NOT "${repeat_result}" STREQUAL "2")
    message(FATAL_ERROR "the repeat run returned ${repeat_result}, expected 2: ${repeat_err}")
endif()
if(NOT "${repeat_err}" STREQUAL "${RUN_ERR}")
    message(FATAL_ERROR
        "the refusal block is not deterministic:\nfirst:\n${RUN_ERR}\nsecond:\n${repeat_err}")
endif()
if(repeat_out MATCHES "raw faults:")
    message(FATAL_ERROR "the repeat run printed fault counts: ${repeat_out}")
endif()

file(SHA256 "${database}" hash_after)
if(NOT hash_before STREQUAL hash_after)
    message(FATAL_ERROR "a refused run rewrote the database")
endif()

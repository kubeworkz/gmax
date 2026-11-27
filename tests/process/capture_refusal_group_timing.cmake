# ADR-013 section 4 condition 3: a group's clocks disagree on period or waveform. The
# captured group is legal -- the SDC puts both clocks in one declared group, so
# condition 1 stays quiet -- and the disagreement is the whole finding. The detail
# names the group and both clocks with their values, because that is what the operator
# has to reconcile.

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

function(run name profile)
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
    string(REGEX MATCHALL "group-timing-disagrees:" findings "${run_err}")
    list(LENGTH findings finding_count)
    if(NOT finding_count EQUAL 1)
        message(FATAL_ERROR "${name}: expected one finding, found ${finding_count}: ${run_err}")
    endif()
    if(run_out MATCHES "raw faults:")
        message(FATAL_ERROR "${name}: a refused run printed fault counts: ${run_out}")
    endif()
    set(RUN_ERR "${run_err}" PARENT_SCOPE)
endfunction()

# One declared group holds clk_a and clk_b, and a third clock gives the command its
# second -group option. The periods are what differs here.
set(periods_sdc "${WORK_DIR}/periods.sdc")
file(WRITE "${periods_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "create_clock -name clk_c -period 10 [get_ports clk_c]\n"
    "set_clock_groups -asynchronous -name pair -group {clk_a clk_b} -group {clk_c}\n")

# Same period, different waveform: clk_a states one and clk_b inherits the default.
set(waveform_sdc "${WORK_DIR}/waveform.sdc")
file(WRITE "${waveform_sdc}"
    "create_clock -name clk_a -period 10 -waveform {0 5} [get_ports clk_a]\n"
    "create_clock -name clk_b -period 10 [get_ports clk_b]\n"
    "create_clock -name clk_c -period 10 [get_ports clk_c]\n"
    "set_clock_groups -asynchronous -name pair -group {clk_a clk_b} -group {clk_c}\n")

# A named group reports its label; an unnamed one reports its clock list.
file(WRITE "${WORK_DIR}/named.json"
    "{\"capture\": {\"kind\": \"simultaneous\", \"cycles\": [{\"groups\": [\n"
    "  {\"name\": \"pair\", \"clocks\": [\"clk_a\", \"clk_b\"]}]}]}}\n")

file(WRITE "${WORK_DIR}/unnamed.json"
    "{\"capture\": {\"kind\": \"simultaneous\", \"cycles\": [{\"groups\": [\n"
    "  {\"clocks\": [\"clk_a\", \"clk_b\"]}]}]}}\n")

file(SHA256 "${database}" hash_before)

# 1. Periods disagree: the detail names the group label, both clocks and both periods.
attach(periods "periods.sdc")
run(periods "named.json")
check_contains("${RUN_ERR}"
    "group-timing-disagrees: group 'pair' mixes periods 10 ns (clk_a) and 20 ns (clk_b)"
    "period disagreement")
check_absent("${RUN_ERR}" "simultaneous-across-groups"
    "a declared group satisfies condition 1")

# 2. Periods agree and the waveforms do not: an unnamed group is reported by its clock
#    list, and a clock without a waveform is named as such rather than as zeros.
attach(waveform "waveform.sdc")
run(waveform "unnamed.json")
check_contains("${RUN_ERR}"
    "group-timing-disagrees: group [clk_a, clk_b] mixes waveforms (clk_a rise 0 ns fall 5 ns, clk_b has no waveform)"
    "waveform disagreement")
check_absent("${RUN_ERR}" "mixes periods" "the waveform case reports no period")

file(SHA256 "${database}" hash_after)
if(NOT hash_before STREQUAL hash_after)
    message(FATAL_ERROR "a refused run rewrote the database")
endif()

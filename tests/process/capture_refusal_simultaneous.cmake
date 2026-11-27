# ADR-013 section 4 condition 1: clocks captured together that the SDC keeps apart.
# Both triggers are pinned here: one group naming clocks the SDC separates, and two
# groups of one cycle sharing an edge time. Every refusal owes the same things -- exit
# 2, one header, no fault count, no report directory and an untouched database -- so
# each case checks those too.
#
# The fixture is three independent clock domains, so section 5's crossing half has
# nothing to find and each block holds exactly the condition-1 findings.

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
    # Section 4: a refusal writes nothing, so its report directory is never created.
    if(EXISTS "${WORK_DIR}/${name}-report")
        message(FATAL_ERROR "${name}: a refused run left a report directory")
    endif()
    string(REGEX MATCHALL "refusing to grade" headers "${run_err}")
    list(LENGTH headers header_count)
    if(NOT header_count EQUAL 1)
        message(FATAL_ERROR "${name}: expected one header, found ${header_count}: ${run_err}")
    endif()
    string(REGEX MATCHALL "simultaneous-across-groups:" findings "${run_err}")
    list(LENGTH findings finding_count)
    if(NOT finding_count EQUAL 1)
        message(FATAL_ERROR "${name}: expected one finding, found ${finding_count}: ${run_err}")
    endif()
    # A refused run stops before the fault list is read, so stdout stays empty.
    if(run_out MATCHES "raw faults:")
        message(FATAL_ERROR "${name}: a refused run printed fault counts: ${run_out}")
    endif()
    set(RUN_ERR "${run_err}" PARENT_SCOPE)
endfunction()

# The clocks share a period and a waveform, so condition 3 has nothing to say and the
# only question left is whether the SDC lets them sit in one group.
set(plain_sdc "${WORK_DIR}/plain.sdc")
file(WRITE "${plain_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 10 [get_ports clk_b]\n"
    "create_clock -name clk_c -period 10 [get_ports clk_c]\n")

# clk_a and clk_b land in different -group options of one command, which is exactly
# the relationship the condition refuses to break.
set(apart_sdc "${WORK_DIR}/apart.sdc")
file(WRITE "${apart_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 10 [get_ports clk_b]\n"
    "create_clock -name clk_c -period 10 [get_ports clk_c]\n"
    "set_clock_groups -asynchronous -name apart -group {clk_a} -group {clk_b clk_c}\n")

file(WRITE "${WORK_DIR}/simultaneous.json"
    "{\"capture\": {\"kind\": \"simultaneous\", \"cycles\": [{\"groups\": [\n"
    "  {\"name\": \"pair\", \"clocks\": [\"clk_a\", \"clk_b\"]}]}]}}\n")

file(WRITE "${WORK_DIR}/shared-edge.json"
    "{\"capture\": {\"kind\": \"grouped\", \"cycles\": [{\"groups\": [\n"
    "  {\"clocks\": [\"clk_a\"], \"edge\": 0},\n"
    "  {\"clocks\": [\"clk_b\"], \"edge\": 0}]}]}}\n")

file(SHA256 "${database}" hash_before)

# 1. One group naming two clocks the SDC never puts together: each clock is its own
#    group, so simultaneous capture of both is the condition's first trigger.
attach(plain "plain.sdc")
run(plain "simultaneous.json")
check_contains("${RUN_ERR}"
    "simultaneous-across-groups: cycle 0 pulses 'clk_a' (no declared group) and 'clk_b' (no declared group), which set_clock_groups keeps apart"
    "undeclared pairing")

# 2. The same group with the clocks declared in different -group options. Both clocks
#    carry the row's -name, so the phrase has to name their actual memberships too;
#    that is what makes the two sides of the finding distinguishable.
attach(apart "apart.sdc")
run(apart "simultaneous.json")
check_contains("${RUN_ERR}"
    "simultaneous-across-groups: cycle 0 pulses 'clk_a' (group 'apart' [clk_a]) and 'clk_b' (group 'apart' [clk_b, clk_c]), which set_clock_groups keeps apart"
    "two inner groups of one declared row")

# 3. Two groups of one cycle given the same edge time: simultaneous capture by timing
#    rather than by declaration, so each group stays a single clock.
attach(shared-edge "plain.sdc")
run(shared-edge "shared-edge.json")
check_contains("${RUN_ERR}"
    "simultaneous-across-groups: cycle 0 gives 'clk_a' and 'clk_b' one edge time"
    "shared edge time")

# Section 4: a refusal never rewrites the database.
file(SHA256 "${database}" hash_after)
if(NOT hash_before STREQUAL hash_after)
    message(FATAL_ERROR "a refused run rewrote the database")
endif()

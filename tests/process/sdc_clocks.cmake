# ADR-012: a clock is enforceable only on a top-level input port. read_sdc
# resolves every clock source, warns when one lands anywhere else, and reports
# how many declared clocks actually clock a state-holding gate. Clocks change no
# fault outcome, so the graded run's coverage block keeps an empty enforced list
# and capture not-enforced.
#
# The sequential fixture is saved by a C++ fixture rather than ingested through
# Yosys: the pinned recipe is structural, this test is about the constraint view
# of a design that already has flops, and keeping it Yosys-free lets the test run
# everywhere.

foreach(required GMAX_BINARY CLOCK_FIXTURE_BINARY FAULT_FIXTURE_BINARY WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

function(run_gmax expected_result)
    execute_process(
        COMMAND "${GMAX_BINARY}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(NOT "${result}" STREQUAL "${expected_result}")
        message(FATAL_ERROR
            "gmax ${ARGN} returned ${result}, expected ${expected_result}: ${err}${out}")
    endif()
    set(GMAX_OUT "${out}" PARENT_SCOPE)
    set(GMAX_ERR "${err}" PARENT_SCOPE)
endfunction()

function(check_text text needle label)
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

function(capture_hash text)
    string(REGEX MATCH "constraints hash: ([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])"
        hash_line "${text}")
    if(hash_line STREQUAL "")
        message(FATAL_ERROR "no constraints hash was printed:\n${text}")
    endif()
    set(GMAX_HASH "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(design "${WORK_DIR}/two-clock.dfdb")
execute_process(
    COMMAND "${CLOCK_FIXTURE_BINARY}" "${design}"
    RESULT_VARIABLE fixture_result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${fixture_result}" STREQUAL "0")
    message(FATAL_ERROR
        "the two-clock fixture failed (${fixture_result}): ${fixture_err}${fixture_out}")
endif()

# Both clocks are defined on top-level inputs, and clk_a and clk_b each clock one
# flop, so both declared clocks are live domains.
set(good_sdc "${WORK_DIR}/two-clock.sdc")
set(attached "${WORK_DIR}/two-clock-attached.dfdb")
file(WRITE "${good_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n")

run_gmax(0 read_sdc "${good_sdc}" --design "${design}" --output "${attached}")
check_text("${GMAX_OUT}" "attached: ${attached}" "two-clock attachment")
check_text("${GMAX_OUT}" "clocks: 2" "two-clock attachment")
check_text("${GMAX_OUT}" "constraints status: complete" "two-clock attachment")
check_text("${GMAX_OUT}" "clock domains: 2 (2 flops)" "two-clock attachment")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "a clock on a top-level input warned: ${GMAX_ERR}")
endif()

# ra is the output of ff_a, so it is neither a port nor a top-level input. The
# clock stays in the set and the hash, and the attachment reports it instead of
# refusing the file; only the declared clocks that clock a flop are counted.
set(internal_sdc "${WORK_DIR}/two-clock-internal.sdc")
set(internal_db "${WORK_DIR}/two-clock-internal.dfdb")
file(WRITE "${internal_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_ra -period 10 [get_nets ra]\n")

run_gmax(0 read_sdc "${internal_sdc}" --design "${design}" --output "${internal_db}")
check_text("${GMAX_OUT}" "clocks: 2" "internal clock attachment")
check_text("${GMAX_OUT}" "constraints status: complete" "internal clock attachment")
check_text("${GMAX_OUT}" "clock domains: 1 (1 flops)" "internal clock attachment")
check_text("${GMAX_ERR}" "clock source 'ra' is not a top-level input"
    "internal clock attachment")

# The report-only form has no netlist to count against, so it must not print a
# domain line it did not measure.
run_gmax(0 read_sdc "${good_sdc}")
check_text("${GMAX_OUT}" "clocks: 2" "report-only form")
check_absent("${GMAX_OUT}" "clock domains:" "report-only form")

# A clock changes no fault outcome. The combinational fixture has one input and
# no flops, so its domain count is zero, and grading it with the clock attached
# must still produce the unconstrained coverage block.
set(flat_db "${WORK_DIR}/flat.dfdb")
execute_process(
    COMMAND "${FAULT_FIXTURE_BINARY}" "${flat_db}"
    RESULT_VARIABLE flat_result
    OUTPUT_VARIABLE flat_out
    ERROR_VARIABLE flat_err)
if(NOT "${flat_result}" STREQUAL "0")
    message(FATAL_ERROR
        "the combinational fixture failed (${flat_result}): ${flat_err}${flat_out}")
endif()

set(flat_sdc "${WORK_DIR}/flat.sdc")
set(flat_attached "${WORK_DIR}/flat-attached.dfdb")
set(faults "${WORK_DIR}/flat.faults")
set(report "${WORK_DIR}/flat-report")
file(WRITE "${flat_sdc}" "create_clock -name a -period 10 [get_ports a]\n")

run_gmax(0 read_sdc "${flat_sdc}" --design "${flat_db}" --output "${flat_attached}")
check_text("${GMAX_OUT}" "constraints status: complete" "flat attachment")
check_text("${GMAX_OUT}" "clock domains: 0 (0 flops)" "flat attachment")
capture_hash("${GMAX_OUT}")
set(flat_hash "${GMAX_HASH}")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "a clock on the only top-level input warned: ${GMAX_ERR}")
endif()

run_gmax(0 write_faults "${flat_attached}" "${faults}")
run_gmax(0 run_atpg "${flat_attached}" "${faults}" 100000 0 "${report}")
check_text("${GMAX_OUT}" "constraints status: complete" "flat ATPG")
check_text("${GMAX_OUT}" "constraints hash: ${flat_hash}" "flat ATPG")

file(READ "${report}/coverage.json" coverage_text)
check_text("${coverage_text}"
    "\"constraints\":{\"status\":\"complete\",\"hash\":\"${flat_hash}\",\"algorithm\":\"crc32\",\"enforced\":[],\"capture\":\"not-enforced\"}"
    "flat coverage constraints")

file(REMOVE_RECURSE "${WORK_DIR}")

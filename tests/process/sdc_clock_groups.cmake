# ADR-012: set_clock_groups members name clocks, so read_sdc validates every
# member against the declared clock names and reports a member that names no
# clock and a clock placed in more than one -group of one command. Both findings
# are warnings and summary lines; the member stays in the group and the set is
# retained whole, so the status stays complete.
#
# Membership needs no netlist, so the report-only form covers the findings; the
# attachment case proves the warnings change neither status nor domain count.

foreach(required GMAX_BINARY CLOCK_FIXTURE_BINARY WORK_DIR)
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

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# Both members name declared clocks: no warning and no summary annotation.
set(good_sdc "${WORK_DIR}/groups-good.sdc")
file(WRITE "${good_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_clock_groups -asynchronous -group {clk_a} -group {clk_b}\n")

run_gmax(0 read_sdc "${good_sdc}")
check_text("${GMAX_OUT}" "clock groups: 1" "declared members")
check_text("${GMAX_OUT}" "constraints status: complete" "declared members")
check_text("${GMAX_OUT}" "warnings: 0" "declared members")
check_absent("${GMAX_OUT}" "unknown member" "declared members")
check_absent("${GMAX_OUT}" "conflicting member" "declared members")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "declared clock-group members warned: ${GMAX_ERR}")
endif()

# clk_typo is not declared. The file is still accepted, the member is kept and
# the summary names it next to the group it belongs to.
set(unknown_sdc "${WORK_DIR}/groups-unknown.sdc")
file(WRITE "${unknown_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_clock_groups -asynchronous -group {clk_a clk_typo} -group {clk_b}\n")

run_gmax(0 read_sdc "${unknown_sdc}")
check_text("${GMAX_OUT}" "constraints status: complete" "unknown member")
check_text("${GMAX_OUT}" "warnings: 1" "unknown member")
check_text("${GMAX_OUT}" "group asynchronous [clk_a clk_typo] [clk_b]" "unknown member")
check_text("${GMAX_OUT}" "unknown member clk_typo" "unknown member")
check_text("${GMAX_ERR}" "clock group member 'clk_typo' is not a declared clock"
    "unknown member")

# clk_a sits in both -group options, so it would be on both sides of one
# relationship. The command is reported, not rewritten.
set(conflict_sdc "${WORK_DIR}/groups-conflict.sdc")
file(WRITE "${conflict_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_clock_groups -asynchronous -group {clk_a} -group {clk_a clk_b}\n")

run_gmax(0 read_sdc "${conflict_sdc}")
check_text("${GMAX_OUT}" "constraints status: complete" "conflicting member")
check_text("${GMAX_OUT}" "warnings: 1" "conflicting member")
check_text("${GMAX_OUT}" "conflicting member clk_a" "conflicting member")
check_absent("${GMAX_OUT}" "unknown member" "conflicting member")
check_text("${GMAX_ERR}" "clock 'clk_a' is a member of more than one -group"
    "conflicting member")

# A member names a clock, not the port or pin it is defined on. core_clk is
# declared on clk_a, so naming clk_a puts a port on one side of the relationship;
# the warning says that rather than only calling the member unknown.
set(source_sdc "${WORK_DIR}/groups-source.sdc")
file(WRITE "${source_sdc}"
    "create_clock -name core_clk -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_clock_groups -asynchronous -group {clk_a} -group {core_clk clk_b}\n")

run_gmax(0 read_sdc "${source_sdc}")
check_text("${GMAX_OUT}" "constraints status: complete" "source member")
check_text("${GMAX_OUT}" "warnings: 1" "source member")
check_text("${GMAX_OUT}" "unknown member clk_a" "source member")
check_text("${GMAX_ERR}" "clock group member 'clk_a' is not a declared clock (it is a clock source; set_clock_groups takes clock names)" "source member")

# Both findings in one command are independent, so they are reported together.
set(mixed_sdc "${WORK_DIR}/groups-mixed.sdc")
file(WRITE "${mixed_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_clock_groups -asynchronous -group {clk_a clk_zz} -group {clk_a}\n")

run_gmax(0 read_sdc "${mixed_sdc}")
check_text("${GMAX_OUT}" "warnings: 2" "mixed findings")
check_text("${GMAX_OUT}" "unknown member clk_zz" "mixed findings")
check_text("${GMAX_OUT}" "conflicting member clk_a" "mixed findings")

# Attachment keeps the member, reports the same warning, and leaves the status
# and the clock-domain count to the clocks themselves.
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

set(attached "${WORK_DIR}/groups-attached.dfdb")
run_gmax(0 read_sdc "${unknown_sdc}" --design "${design}" --output "${attached}")
check_text("${GMAX_OUT}" "attached: ${attached}" "attached group")
check_text("${GMAX_OUT}" "constraints status: complete" "attached group")
check_text("${GMAX_OUT}" "clock domains: 2 (2 flops)" "attached group")
check_text("${GMAX_OUT}" "unknown member clk_typo" "attached group")
check_text("${GMAX_ERR}" "clock group member 'clk_typo' is not a declared clock"
    "attached group")

# The attached database carries the group, warning and all.
run_gmax(0 read_sdc "${unknown_sdc}" --design "${attached}" --output "${WORK_DIR}/again.dfdb")
check_text("${GMAX_OUT}" "clocks: 2" "reattached group")
check_text("${GMAX_OUT}" "clock groups: 1" "reattached group")

file(REMOVE_RECURSE "${WORK_DIR}")

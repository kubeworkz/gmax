# ADR-012 section 4: a false-path endpoint may be a clock, a port, a pin or a
# net. An endpoint the file asked for with [get_clocks ...] must name a declared
# clock, which needs no netlist; every endpoint is resolved against the design
# at attachment. Both findings are warnings: the path is kept, the set stays
# complete, and neither the hash nor the clock-domain count changes.

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

# Both endpoints name declared clocks, so the report-only pass has nothing to say.
set(declared_sdc "${WORK_DIR}/false-declared.sdc")
file(WRITE "${declared_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_false_path -from [get_clocks clk_a] -to [get_clocks clk_b]\n")

run_gmax(0 read_sdc "${declared_sdc}")
check_text("${GMAX_OUT}" "clocks: 2" "declared clock endpoints")
check_text("${GMAX_OUT}" "constraints status: complete" "declared clock endpoints")
check_text("${GMAX_OUT}" "false paths: 1" "declared clock endpoints")
check_text("${GMAX_OUT}" "false_path from=clk_a to=clk_b" "declared clock endpoints")
check_text("${GMAX_OUT}" "warnings: 0" "declared clock endpoints")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "a false path between declared clocks warned: ${GMAX_ERR}")
endif()

# clk_typo is not declared. The path is kept and the warning names the option.
set(unknown_sdc "${WORK_DIR}/false-unknown.sdc")
file(WRITE "${unknown_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "set_false_path -from [get_clocks clk_a] -to [get_clocks clk_typo]\n")

run_gmax(0 read_sdc "${unknown_sdc}")
check_text("${GMAX_OUT}" "constraints status: complete" "unknown clock endpoint")
check_text("${GMAX_OUT}" "false paths: 1" "unknown clock endpoint")
check_text("${GMAX_OUT}" "false_path from=clk_a to=clk_typo" "unknown clock endpoint")
check_text("${GMAX_OUT}" "warnings: 1" "unknown clock endpoint")
check_text("${GMAX_ERR}" "false path -to names clock 'clk_typo', which is not a declared clock" "unknown clock endpoint")

# A port or net endpoint says nothing about clocks, so report-only leaves it to
# attachment even when the name is not declared anywhere.
set(objects_sdc "${WORK_DIR}/false-objects.sdc")
file(WRITE "${objects_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "set_false_path -from [get_ports clk_a] -through [get_nets nope] -to [get_ports clk_b]\n")

run_gmax(0 read_sdc "${objects_sdc}")
check_text("${GMAX_OUT}" "warnings: 0" "object endpoints")
check_text("${GMAX_OUT}" "false paths: 1" "object endpoints")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "object endpoints warned in report-only mode: ${GMAX_ERR}")
endif()

# A clock query naming the port gets the hint the master checks give.
set(hint_sdc "${WORK_DIR}/false-hint.sdc")
file(WRITE "${hint_sdc}"
    "create_clock -name core_clk -period 10 [get_ports clk]\n"
    "set_false_path -from [get_clocks clk] -to [get_clocks core_clk]\n")

run_gmax(0 read_sdc "${hint_sdc}")
check_text("${GMAX_OUT}" "warnings: 1" "clock source hint")
check_text("${GMAX_ERR}" "false path -from names clock 'clk', which is not a declared clock (it is a clock source; the clock defined on it has another name)" "clock source hint")

# Attachment resolves the endpoints that report-only could not: a top-level
# input, a net and a top-level output all exist in the fixture.
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

set(resolved_sdc "${WORK_DIR}/false-resolved.sdc")
file(WRITE "${resolved_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "set_false_path -from [get_ports clk_a] -through [get_nets ra] -to [get_ports q_a]\n")

set(attached "${WORK_DIR}/false-attached.dfdb")
run_gmax(0 read_sdc "${resolved_sdc}" --design "${design}" --output "${attached}")
check_text("${GMAX_OUT}" "attached: ${attached}" "resolved endpoints")
check_text("${GMAX_OUT}" "constraints status: complete" "resolved endpoints")
check_text("${GMAX_OUT}" "clock domains: 1 (1 flops)" "resolved endpoints")
check_text("${GMAX_OUT}" "false paths: 1" "resolved endpoints")
check_absent("${GMAX_ERR}" "does not resolve" "resolved endpoints")

# Endpoints that resolve nowhere are reported and kept: a false path is not
# enforced, so refusing the set would claim more than the engine can back up.
set(unresolved_sdc "${WORK_DIR}/false-unresolved.sdc")
file(WRITE "${unresolved_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "set_false_path -from [get_nets ghost] -to [get_pins phantom]\n")

set(unresolved_db "${WORK_DIR}/false-unresolved.dfdb")
run_gmax(0 read_sdc "${unresolved_sdc}" --design "${design}" --output "${unresolved_db}")
check_text("${GMAX_OUT}" "attached: ${unresolved_db}" "unresolved endpoints")
check_text("${GMAX_OUT}" "constraints status: complete" "unresolved endpoints")
check_text("${GMAX_OUT}" "clock domains: 1 (1 flops)" "unresolved endpoints")
check_text("${GMAX_OUT}" "warnings: 0" "unresolved endpoints")
check_text("${GMAX_ERR}" "false path -from 'ghost' does not resolve to a port, pin or net" "unresolved endpoints")
check_text("${GMAX_ERR}" "false path -to 'phantom' does not resolve to a port, pin or net" "unresolved endpoints")

# An unknown clock endpoint is reported by both layers: read_sdc knows the name
# is not a clock, and the design has no such port, pin or net either.
set(attached_unknown "${WORK_DIR}/false-attached-unknown.dfdb")
run_gmax(0 read_sdc "${unknown_sdc}" --design "${design}" --output "${attached_unknown}")
check_text("${GMAX_OUT}" "warnings: 1" "unknown clock at attachment")
check_text("${GMAX_OUT}" "constraints status: complete" "unknown clock at attachment")
check_text("${GMAX_ERR}" "false path -to names clock 'clk_typo', which is not a declared clock" "unknown clock at attachment")
check_text("${GMAX_ERR}" "false path -to 'clk_typo' does not resolve to a port, pin or net" "unknown clock at attachment")

# The attached database keeps the false paths and stays attachable.
run_gmax(0 read_sdc "${unresolved_sdc}" --design "${unresolved_db}" --output "${WORK_DIR}/again.dfdb")
check_text("${GMAX_OUT}" "clocks: 1" "reattached false path")
check_text("${GMAX_OUT}" "false paths: 1" "reattached false path")
check_text("${GMAX_OUT}" "constraints status: complete" "reattached false path")

file(REMOVE_RECURSE "${WORK_DIR}")

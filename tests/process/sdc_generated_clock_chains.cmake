# ADR-012 section 4: a generated clock with no -master_clock links through its
# -source alone, so the source must carry exactly one declared clock; and a
# -master_clock chain must reach a primary clock rather than close on itself.
# Both findings are warnings with summary lines: the clocks keep their recorded
# masters, so the status stays complete and the hash is unchanged.
#
# Linkage needs no netlist, so the report-only form covers the findings; the
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

# Two clocks are defined on clk_a, so an implicit -source does not say which
# one is the master of gen_amb.
set(ambiguous_sdc "${WORK_DIR}/gen-ambiguous.sdc")
file(WRITE "${ambiguous_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_a]\n"
    "create_generated_clock -name gen_amb -source [get_ports clk_a] [get_nets ra]\n")

run_gmax(0 read_sdc "${ambiguous_sdc}")
check_text("${GMAX_OUT}" "clocks: 3" "ambiguous source")
check_text("${GMAX_OUT}" "constraints status: complete" "ambiguous source")
check_text("${GMAX_OUT}" "warnings: 1" "ambiguous source")
check_text("${GMAX_OUT}" "generated master= source_pin=clk_a" "ambiguous source")
check_text("${GMAX_OUT}" "ambiguous source clk_a (clk_a clk_b)" "ambiguous source")
check_absent("${GMAX_OUT}" "unknown master" "ambiguous source")
check_absent("${GMAX_OUT}" "conflicting master" "ambiguous source")
check_text("${GMAX_ERR}" "generated clock 'gen_amb' takes -source 'clk_a' but no -master_clock, and 'clk_a' carries more than one clock (clk_a, clk_b)" "ambiguous source")

# One clock on the source is the implicit link the ADR already allows.
set(implicit_sdc "${WORK_DIR}/gen-implicit.sdc")
file(WRITE "${implicit_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_generated_clock -name gen_a -source [get_ports clk_a] [get_nets ra]\n")

run_gmax(0 read_sdc "${implicit_sdc}")
check_text("${GMAX_OUT}" "warnings: 0" "implicit single clock")
check_absent("${GMAX_OUT}" "ambiguous source" "implicit single clock")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "an implicit single master warned: ${GMAX_ERR}")
endif()

# Naming the master resolves the same shared source.
set(named_sdc "${WORK_DIR}/gen-named.sdc")
file(WRITE "${named_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_a]\n"
    "create_generated_clock -name gen_b -source [get_ports clk_a] -master_clock clk_b [get_nets ra]\n")

run_gmax(0 read_sdc "${named_sdc}")
check_text("${GMAX_OUT}" "warnings: 0" "named master on a shared source")
check_absent("${GMAX_OUT}" "ambiguous source" "named master on a shared source")

# Two generated clocks that name each other have no primary root.
set(cycle_sdc "${WORK_DIR}/gen-cycle.sdc")
file(WRITE "${cycle_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock gen_b [get_nets ra]\n"
    "create_generated_clock -name gen_b -source [get_nets ra] -master_clock gen_a [get_nets rb]\n")

run_gmax(0 read_sdc "${cycle_sdc}")
check_text("${GMAX_OUT}" "clocks: 3" "master cycle")
check_text("${GMAX_OUT}" "constraints status: complete" "master cycle")
check_text("${GMAX_OUT}" "warnings: 2" "master cycle")
check_text("${GMAX_OUT}" "master cycle gen_a -> gen_b -> gen_a" "master cycle")
check_text("${GMAX_OUT}" "master cycle gen_b -> gen_a -> gen_b" "master cycle")
check_absent("${GMAX_OUT}" "conflicting master" "master cycle")
check_text("${GMAX_ERR}" "generated clock 'gen_a' has no root: -master_clock cycle gen_a -> gen_b -> gen_a" "master cycle")

# A three-hop loop renders every hop, and a clock that only leads into the loop
# is reported with the cycle it cannot leave.
set(leaf_sdc "${WORK_DIR}/gen-leaf.sdc")
file(WRITE "${leaf_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock gen_c [get_nets ra]\n"
    "create_generated_clock -name gen_b -source [get_nets ra] -master_clock gen_a [get_nets rb]\n"
    "create_generated_clock -name gen_c -source [get_nets rb] -master_clock gen_b [get_nets rc]\n"
    "create_generated_clock -name gen_leaf -source [get_ports clk_a] -master_clock gen_a [get_nets rd]\n")

run_gmax(0 read_sdc "${leaf_sdc}")
check_text("${GMAX_OUT}" "warnings: 4" "cycle with a leaf")
check_text("${GMAX_OUT}" "master cycle gen_a -> gen_c -> gen_b -> gen_a" "cycle with a leaf")
check_text("${GMAX_ERR}" "generated clock 'gen_leaf' has no root: -master_clock cycle gen_a -> gen_c -> gen_b -> gen_a" "cycle with a leaf")

# Attachment keeps the clocks, reports the same warning, and leaves the status
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

set(attached "${WORK_DIR}/gen-attached.dfdb")
run_gmax(0 read_sdc "${ambiguous_sdc}" --design "${design}" --output "${attached}")
check_text("${GMAX_OUT}" "attached: ${attached}" "attached ambiguity")
check_text("${GMAX_OUT}" "constraints status: complete" "attached ambiguity")
check_text("${GMAX_OUT}" "clock domains: 1 (1 flops)" "attached ambiguity")
check_text("${GMAX_OUT}" "ambiguous source clk_a (clk_a clk_b)" "attached ambiguity")
check_text("${GMAX_ERR}" "generated clock 'gen_amb' takes -source 'clk_a' but no -master_clock" "attached ambiguity")

# The attached database carries the generated clock without a master.
run_gmax(0 read_sdc "${implicit_sdc}" --design "${attached}" --output "${WORK_DIR}/again.dfdb")
check_text("${GMAX_OUT}" "clocks: 2" "reattached ambiguity")
check_text("${GMAX_OUT}" "generated master= source_pin=clk_a" "reattached ambiguity")

file(REMOVE_RECURSE "${WORK_DIR}")

# ADR-012 section 4: a generated clock's -master_clock names a declared clock,
# and the two ends of the link must agree: the master must be the clock defined
# on the command's -source (or that clock object itself). An unknown master, a
# master defined on another object, and a clock that names itself are warnings
# with summary lines; the clock keeps its recorded master, so the status stays
# complete and the hash is unchanged.
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

# The master is the clock defined on the -source, so the link is complete.
set(linked_sdc "${WORK_DIR}/gen-linked.sdc")
file(WRITE "${linked_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_a [get_nets ra]\n")

run_gmax(0 read_sdc "${linked_sdc}")
check_text("${GMAX_OUT}" "clocks: 2" "linked master")
check_text("${GMAX_OUT}" "generated master=clk_a source_pin=clk_a" "linked master")
check_text("${GMAX_OUT}" "constraints status: complete" "linked master")
check_text("${GMAX_OUT}" "warnings: 0" "linked master")
check_absent("${GMAX_OUT}" "unknown master" "linked master")
check_absent("${GMAX_OUT}" "conflicting master" "linked master")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "a linked master warned: ${GMAX_ERR}")
endif()

# clk_typo is not declared. The file is still accepted, the clock keeps the
# master it named, and the summary annotates the clock it belongs to.
set(unknown_sdc "${WORK_DIR}/gen-unknown.sdc")
file(WRITE "${unknown_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_typo [get_nets ra]\n")

run_gmax(0 read_sdc "${unknown_sdc}")
check_text("${GMAX_OUT}" "constraints status: complete" "unknown master")
check_text("${GMAX_OUT}" "warnings: 1" "unknown master")
check_text("${GMAX_OUT}" "generated master=clk_typo source_pin=clk_a" "unknown master")
check_text("${GMAX_OUT}" "unknown master clk_typo" "unknown master")
check_absent("${GMAX_OUT}" "conflicting master" "unknown master")
check_text("${GMAX_ERR}" "generated clock 'gen_a' takes -master_clock 'clk_typo', which is not a declared clock" "unknown master")

# A master that names the port the clock is defined on is the same mistake the
# group check explains, so the warning gives the same reason.
set(source_sdc "${WORK_DIR}/gen-source.sdc")
file(WRITE "${source_sdc}"
    "create_clock -name core_clk -period 10 [get_ports clk]\n"
    "create_generated_clock -name gen_core -source [get_ports clk] -master_clock clk [get_nets ra]\n")

run_gmax(0 read_sdc "${source_sdc}")
check_text("${GMAX_OUT}" "warnings: 1" "source-only master")
check_text("${GMAX_OUT}" "unknown master clk" "source-only master")
check_text("${GMAX_ERR}" "generated clock 'gen_core' takes -master_clock 'clk', which is not a declared clock (it is a clock source; create_generated_clock -master_clock takes clock names)" "source-only master")

# Both clocks are declared, but the master is defined on another object than the
# command's -source, so the command names two links at once.
set(conflict_sdc "${WORK_DIR}/gen-conflict.sdc")
file(WRITE "${conflict_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_b [get_nets ra]\n")

run_gmax(0 read_sdc "${conflict_sdc}")
check_text("${GMAX_OUT}" "constraints status: complete" "conflicting master")
check_text("${GMAX_OUT}" "warnings: 1" "conflicting master")
check_text("${GMAX_OUT}" "conflicting master clk_b" "conflicting master")
check_absent("${GMAX_OUT}" "unknown master" "conflicting master")
check_text("${GMAX_ERR}" "generated clock 'gen_a' takes -source 'clk_a' but its master 'clk_b' is defined on 'clk_b'" "conflicting master")

# A clock that derives from itself has no root to derive from.
set(self_sdc "${WORK_DIR}/gen-self.sdc")
file(WRITE "${self_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock gen_a [get_nets ra]\n")

run_gmax(0 read_sdc "${self_sdc}")
check_text("${GMAX_OUT}" "warnings: 1" "self-linked master")
check_text("${GMAX_OUT}" "conflicting master gen_a" "self-linked master")
check_text("${GMAX_ERR}" "generated clock 'gen_a' names itself as its master" "self-linked master")

# Attachment keeps the clock, reports the same warning, and leaves the status,
# the hash and the clock-domain count to the clocks themselves.
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
run_gmax(0 read_sdc "${unknown_sdc}" --design "${design}" --output "${attached}")
check_text("${GMAX_OUT}" "attached: ${attached}" "attached master")
check_text("${GMAX_OUT}" "constraints status: complete" "attached master")
check_text("${GMAX_OUT}" "clock domains: 1 (1 flops)" "attached master")
check_text("${GMAX_OUT}" "unknown master clk_typo" "attached master")
check_text("${GMAX_ERR}" "generated clock 'gen_a' takes -master_clock 'clk_typo', which is not a declared clock" "attached master")
check_text("${GMAX_ERR}" "clock source 'ra' is not a top-level input" "attached master")

# The attached database carries the generated clock and its master.
run_gmax(0 read_sdc "${unknown_sdc}" --design "${attached}" --output "${WORK_DIR}/again.dfdb")
check_text("${GMAX_OUT}" "clocks: 2" "reattached master")
check_text("${GMAX_OUT}" "generated master=clk_typo" "reattached master")

file(REMOVE_RECURSE "${WORK_DIR}")

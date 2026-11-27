# ADR-012: the attachment path against a real Yosys netlist rather than a
# hand-built .dfdb. c432 is ingested by read_verilog, an SDC is attached with
# read_sdc --design/--output, and the attached database is graded. The coverage
# report must carry the constraints identity, with case_analysis listed as
# enforced because PODEM and the SAT miter apply the values. The same set
# written in a different order and spacing must hash the same, so the canonical
# form is checked against the pinned frontend.
#
# The test is skipped when the pinned Yosys executable is not available;
# GMAX_G0_1_GATE=1 makes that absence a hard failure so CI cannot pass without
# the evidence.

foreach(required GMAX_BINARY GMAX_SOURCE_DIR WORK_DIR GMAX_REPORTS_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT DEFINED ENV{GMAX_YOSYS} OR NOT EXISTS "$ENV{GMAX_YOSYS}")
    if(DEFINED ENV{GMAX_G0_1_GATE})
        message(FATAL_ERROR "G0.1 gate requires GMAX_YOSYS to point at the pinned Yosys binary")
    endif()
    message(STATUS "Skipping SDC attachment test: GMAX_YOSYS is not set to an existing binary")
    return()
endif()

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

# The hash is captured rather than hardcoded: the point is that the report and
# the CLI agree with the attached database, and that two spellings of the same
# set produce the same value.
function(capture_hash text)
    string(REGEX MATCH "constraints hash: ([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])"
        hash_line "${text}")
    if(hash_line STREQUAL "")
        message(FATAL_ERROR "no constraints hash was printed:\n${text}")
    endif()
    set(GMAX_HASH "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

set(netlist "${GMAX_SOURCE_DIR}/tests/benchmarks/iscas85/c432.v")
set(design "${WORK_DIR}/c432.dfdb")
set(attached "${WORK_DIR}/c432-attached.dfdb")
set(attached_b "${WORK_DIR}/c432-attached-b.dfdb")
set(faults "${WORK_DIR}/c432.faults")
set(report "${WORK_DIR}/c432-attached-report")
set(sdc_a "${WORK_DIR}/c432-a.sdc")
set(sdc_b "${WORK_DIR}/c432-b.sdc")

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

run_gmax(0 read_verilog "${netlist}" "${design}" c432)
check_text("${GMAX_OUT}" "netlist hash: 413dd789" "c432 ingest")

# N1 and N4 are top-level inputs of c432, so both case values are enforceable
# targets and the attached set is complete.
file(WRITE "${sdc_a}"
    "# one spelling of the same constraint set\n"
    "set_case_analysis 1 [get_ports N1]\n"
    "set_case_analysis 0 [get_ports N4]\n")

run_gmax(0 read_sdc "${sdc_a}" --design "${design}" --output "${attached}")
check_text("${GMAX_OUT}" "attached: ${attached}" "c432 attachment")
check_text("${GMAX_OUT}" "case analysis: 2" "c432 attachment")
check_text("${GMAX_OUT}" "constraints status: complete" "c432 attachment")
capture_hash("${GMAX_OUT}")
set(constraints_hash "${GMAX_HASH}")

# Same set, reversed order with a comment and a blank line between statements.
# The canonical payload sorts records and ignores layout, so the hash must not
# move.
file(WRITE "${sdc_b}"
    "set_case_analysis 0 [get_ports N4]\n"
    "\n"
    "# reordered\n"
    "set_case_analysis 1 [get_ports N1]\n")
run_gmax(0 read_sdc "${sdc_b}" --design "${design}" --output "${attached_b}")
capture_hash("${GMAX_OUT}")
if(NOT GMAX_HASH STREQUAL constraints_hash)
    message(FATAL_ERROR
        "the same set formatted differently hashed differently: "
        "${constraints_hash} vs ${GMAX_HASH}")
endif()

# The fault list is built from the unconstrained netlist: attaching constraints
# must not change the netlist identity it was derived from.
run_gmax(0 write_faults "${design}" "${faults}")
check_text("${GMAX_OUT}" "collapsed faults: 484" "c432 fault list")

run_gmax(0 run_atpg "${attached}" "${faults}" 100000 0 "${report}")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "c432 run_atpg wrote to stderr: ${GMAX_ERR}")
endif()
check_text("${GMAX_OUT}" "netlist hash: 413dd789" "c432 ATPG")
check_text("${GMAX_OUT}" "constraints status: complete" "c432 ATPG")
check_text("${GMAX_OUT}" "constraints hash: ${constraints_hash}" "c432 ATPG")
# The attached set pins N1=1 and N4=0, and the engines apply both, so the 30
# faults whose activation or propagation those values block are decided
# untestable instead of detected. The unconstrained c432 numbers stay 479/5.
check_text("${GMAX_OUT}" "detected: 449" "c432 ATPG")
check_text("${GMAX_OUT}" "untestable: 35" "c432 ATPG")
check_text("${GMAX_OUT}" "aborted: 0" "c432 ATPG")

set(coverage "${report}/coverage.json")
set(manifest "${report}/manifest.json")
if(NOT EXISTS "${coverage}" OR NOT EXISTS "${manifest}")
    message(FATAL_ERROR "the attached run wrote no reports")
endif()

file(READ "${coverage}" coverage_text)
check_text("${coverage_text}" "\"netlist_hash\":\"413dd789\"" "c432 coverage netlist hash")
check_text("${coverage_text}"
    "\"constraints\":{\"status\":\"complete\",\"hash\":\"${constraints_hash}\",\"algorithm\":\"crc32\",\"enforced\":[\"case_analysis\"],\"false_paths\":[],\"capture\":{\"status\":\"absent\",\"procedure\":null,\"derived\":false,\"hash\":null}}"
    "c432 coverage constraints")
# No declared clock means no capture claim, which ADR-013 section 6 reports as
# `absent` inside the constraints object rather than as a constraint status.
check_absent("${coverage_text}" "\"constraints\":{\"status\":\"absent\""
    "c432 coverage constraints status")

file(READ "${manifest}" manifest_text)
check_text("${manifest_text}"
    "\"constraints\":{\"path\":\"\",\"status\":\"complete\",\"hash\":\"${constraints_hash}\",\"algorithm\":\"crc32\",\"false_paths\":[],\"capture\":{\"status\":\"absent\",\"procedure\":null,\"derived\":false,\"hash\":null,\"cycles\":[]}}"
    "c432 manifest constraints")

# A target that cannot be applied must never be graded. N118 is an internal net
# of c432 (the output of NOT1_1, feeding NAND2_19), addressed by the name the
# Verilog declares rather than by a Yosys alias, so it is driven by a gate
# instead of being a top-level input: the attachment keeps the record but marks
# the set incomplete, and run_atpg must refuse the design before it reads faults
# or writes a report.
set(incomplete_sdc "${WORK_DIR}/c432-incomplete.sdc")
set(incomplete_db "${WORK_DIR}/c432-incomplete.dfdb")
set(incomplete_report "${WORK_DIR}/c432-incomplete-report")
file(WRITE "${incomplete_sdc}" "set_case_analysis 0 [get_nets N118]\n")

run_gmax(0 read_sdc "${incomplete_sdc}" --design "${design}" --output "${incomplete_db}")
check_text("${GMAX_OUT}" "attached: ${incomplete_db}" "c432 incomplete attachment")
check_text("${GMAX_OUT}" "case N118=0" "c432 incomplete attachment")
check_text("${GMAX_OUT}" "case analysis: 1" "c432 incomplete attachment")
check_text("${GMAX_OUT}" "constraints status: incomplete" "c432 incomplete attachment")

run_gmax(2 run_atpg "${incomplete_db}" "${faults}" 100000 0 "${incomplete_report}")
check_text("${GMAX_ERR}" "design constraints are incomplete" "c432 incomplete refusal")
check_text("${GMAX_ERR}" "refusing to grade" "c432 incomplete refusal")
check_absent("${GMAX_OUT}" "raw faults:" "c432 incomplete refusal")
if(EXISTS "${incomplete_report}/coverage.json" OR EXISTS "${incomplete_report}/manifest.json")
    message(FATAL_ERROR "run_atpg wrote reports for an incomplete constraint set")
endif()

file(MAKE_DIRECTORY "${GMAX_REPORTS_DIR}/sdc-attach")
file(COPY "${coverage}" "${manifest}" DESTINATION "${GMAX_REPORTS_DIR}/sdc-attach")

file(REMOVE_RECURSE "${WORK_DIR}")

# G0.2 SAT smoke evidence: one testable and one redundant c432 fault go through
# the internal CDCL adapter, then the same faults are checked independently by
# the pinned Yosys SAT engine on a two-copy miter. When GMAX_MINISAT points at
# the pinned MiniSat 2.2.1 binary, the same Yosys-dumped CNFs are solved by
# MiniSat as a second independent engine. The Python side fails on a
# disagreement before it records `agree: true`.
#
# The default developer run skips when the pinned Yosys executable is not
# available, like the larger ISCAS'85 process test, and runs Yosys-only when
# GMAX_MINISAT is not set. The G0.2 gate is opt-in:
#
#   GMAX_G0_2_GATE=1
#
# With the gate set, a missing Yosys or MiniSat binary is a hard failure; the
# gate cannot pass on Yosys alone.

foreach(required GMAX_BINARY GMAX_SOURCE_DIR PYTHON_EXECUTABLE WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

string(TOUPPER "$ENV{GMAX_G0_2_GATE}" gate_value)
set(g0_2_gate FALSE)
if(gate_value STREQUAL "1" OR gate_value STREQUAL "ON" OR gate_value STREQUAL "TRUE" OR gate_value STREQUAL "YES")
    set(g0_2_gate TRUE)
elseif(NOT gate_value STREQUAL "" AND NOT gate_value STREQUAL "0" AND NOT gate_value STREQUAL "OFF" AND NOT gate_value STREQUAL "FALSE" AND NOT gate_value STREQUAL "NO")
    message(FATAL_ERROR
        "GMAX_G0_2_GATE must be 1/ON/TRUE/YES or 0/OFF/FALSE/NO, got '$ENV{GMAX_G0_2_GATE}'")
endif()

if(NOT DEFINED ENV{GMAX_YOSYS} OR NOT EXISTS "$ENV{GMAX_YOSYS}")
    if(g0_2_gate)
        message(FATAL_ERROR
            "G0.2 gate: GMAX_YOSYS must point at the pinned Yosys binary; "
            "the SAT smoke evidence cannot be produced without it")
    endif()
    message(STATUS "Skipping SAT smoke test: GMAX_YOSYS is not set to an existing binary")
    return()
endif()

# A set-but-broken GMAX_MINISAT is always a configuration error. In gate mode,
# an absent MiniSat is also a hard failure: G0.2 must not pass on Yosys alone.
if(DEFINED ENV{GMAX_MINISAT} AND NOT EXISTS "$ENV{GMAX_MINISAT}")
    message(FATAL_ERROR
        "GMAX_MINISAT is set but does not name an existing binary: $ENV{GMAX_MINISAT}")
endif()
if(g0_2_gate AND NOT DEFINED ENV{GMAX_MINISAT})
    message(FATAL_ERROR
        "G0.2 gate: GMAX_MINISAT is not set; the pinned MiniSat 2.2.1 "
        "cross-check is required and Yosys-only evidence is not sufficient")
endif()
if(NOT DEFINED ENV{GMAX_MINISAT})
    message(STATUS "SAT smoke: GMAX_MINISAT is not set; running Yosys-only developer check (set GMAX_G0_2_GATE=1 to require MiniSat)")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

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

set(netlist "${GMAX_SOURCE_DIR}/tests/benchmarks/iscas85/c432.v")
set(dfdb "${WORK_DIR}/c432.dfdb")
set(faults "${WORK_DIR}/c432.faults")
set(report "${WORK_DIR}/sat_smoke.json")

run_gmax(0 read_verilog "${netlist}" "${dfdb}" c432)
check_text("${GMAX_OUT}" "netlist hash: 413dd789" "SAT smoke ingest")
run_gmax(0 write_faults "${dfdb}" "${faults}")
check_text("${GMAX_OUT}" "collapsed faults: 484" "SAT smoke faults")

# 168 SA1 is the SAT/testable case; 51 SA0 is the redundant/UNSAT case. These
# are the same faults the ATPG run already treats as SAT-recovered and proven
# redundant, respectively.
run_gmax(0 sat_smoke "${dfdb}" "${faults}" "${report}" 168:1 51:0)
if(NOT GMAX_ERR STREQUAL "")
    message(FATAL_ERROR "sat_smoke wrote to stderr: ${GMAX_ERR}")
endif()
check_text("${GMAX_OUT}" "sat smoke cases: 2" "SAT smoke")

set(sat_check_command
    "${PYTHON_EXECUTABLE}"
    "${GMAX_SOURCE_DIR}/tests/process/sat_smoke.py"
    "$ENV{GMAX_YOSYS}"
    "${netlist}"
    "${report}"
    "${WORK_DIR}"
    "tests/benchmarks/iscas85/c432.v")
if(DEFINED ENV{GMAX_MINISAT})
    list(APPEND sat_check_command "$ENV{GMAX_MINISAT}")
endif()

execute_process(
    COMMAND ${sat_check_command}
    RESULT_VARIABLE sat_result
    OUTPUT_VARIABLE sat_out
    ERROR_VARIABLE sat_err)
if(NOT "${sat_result}" STREQUAL "0")
    message(FATAL_ERROR
        "independent SAT checks failed (${sat_result}): ${sat_err}${sat_out}")
endif()
message(STATUS "${sat_out}")

file(READ "${report}" evidence)
check_text("${evidence}" "\"schema\": \"gmax-sat-smoke-1\"" "SAT smoke schema")
check_text("${evidence}" "\"backend\": \"internal-cdcl\"" "SAT smoke backend")
check_text("${evidence}" "\"net\": 168" "SAT smoke testable case")
check_text("${evidence}" "\"net\": 51" "SAT smoke redundant case")
check_text("${evidence}" "\"verdict\": \"sat\"" "SAT smoke SAT verdict")
check_text("${evidence}" "\"verdict\": \"unsat\"" "SAT smoke UNSAT verdict")
check_text("${evidence}" "\"solver_timed_out\": false" "SAT smoke timeout")
check_text("${evidence}" "\"tool\": \"Yosys SAT\"" "SAT smoke independent tool")
check_text("${evidence}" "\"proof\": \"model\"" "SAT smoke model proof")
check_text("${evidence}" "\"proof\": \"no model\"" "SAT smoke UNSAT proof")
check_text("${evidence}" "\"agree\": true" "SAT smoke agreement")

if(g0_2_gate OR DEFINED ENV{GMAX_MINISAT})
    check_text("${evidence}" "\"minisat_check\"" "SAT smoke MiniSat section")
    check_text("${evidence}" "\"tool\": \"MiniSat\"" "SAT smoke MiniSat tool")
    check_text("${evidence}" "\"version\": \"2.2.1\"" "SAT smoke MiniSat version")
    check_text("${evidence}" "\"proof\": \"SATISFIABLE\"" "SAT smoke MiniSat SAT proof")
    check_text("${evidence}" "\"proof\": \"UNSATISFIABLE\"" "SAT smoke MiniSat UNSAT proof")
    check_text("${evidence}" "\"exit_code\": 10" "SAT smoke MiniSat SAT exit")
    check_text("${evidence}" "\"exit_code\": 20" "SAT smoke MiniSat UNSAT exit")
    check_text("${evidence}" "\"agree\": true" "SAT smoke MiniSat agreement")
else()
    message(STATUS "SAT smoke: Yosys-only developer check passed; set GMAX_G0_2_GATE=1 to require MiniSat")
endif()

if(g0_2_gate)
    message(STATUS "G0.2 gate: Yosys and MiniSat both agree on the two c432 cases")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

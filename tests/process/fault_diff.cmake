# G0.7 differential evidence: every raw stuck-at fault on c432 (two per net, so
# every net is covered) is simulated by the 64-lane engine and by the scalar
# reference over the same deterministic pattern set. The harness fails on any
# disagreement in the detection set or in the first detecting pattern. A
# missing pinned Yosys is a skip by default and a hard failure under
# GMAX_G0_7_GATE=1, mirroring the G0.2 and G0.3 gates.
foreach(required GMAX_BINARY DIFF_BINARY GMAX_SOURCE_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT DEFINED ENV{GMAX_YOSYS} OR NOT EXISTS "$ENV{GMAX_YOSYS}")
    if(DEFINED ENV{GMAX_G0_7_GATE})
        message(FATAL_ERROR "G0.7 gate requires GMAX_YOSYS to point at the pinned Yosys binary")
    endif()
    message(STATUS "Skipping G0.7 fault differential: GMAX_YOSYS is not set to an existing binary")
    return()
endif()

set(netlist_dir "${GMAX_SOURCE_DIR}/tests/benchmarks/iscas85")
set(work_dir "${WORK_DIR}")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

set(c432_dfdb "${work_dir}/c432.dfdb")
execute_process(
    COMMAND "${GMAX_BINARY}" read_verilog "${netlist_dir}/c432.v" "${c432_dfdb}" c432
    RESULT_VARIABLE result
    OUTPUT_VARIABLE ingest_out
    ERROR_VARIABLE ingest_err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "c432 ingest failed (${result}): ${ingest_err}${ingest_out}")
endif()

execute_process(
    COMMAND "${DIFF_BINARY}" "${c432_dfdb}" 256
    RESULT_VARIABLE result
    OUTPUT_VARIABLE diff_out
    ERROR_VARIABLE diff_err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "fault differential failed (${result}): ${diff_err}${diff_out}")
endif()

foreach(needle
        "nets: 350"
        "faults: 700"
        "patterns: 256"
        "detected: 691"
        "the 64-lane engine and the scalar reference agree on every fault")
    string(FIND "${diff_out}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "fault differential output is missing '${needle}':\n${diff_out}")
    endif()
endforeach()

file(REMOVE_RECURSE "${work_dir}")

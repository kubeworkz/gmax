# G0.3 / ADR-011: the same c432 flow run with one worker thread and with four
# must write byte-identical coverage and manifests that differ only in the
# recorded worker count. The wall-clock budget is disabled so a timeout cannot
# mask a merge-order defect. GMAX_G0_3_GATE=1 makes a missing pinned Yosys a
# hard failure instead of a skip, mirroring the G0.2 gate.
foreach(required GMAX_BINARY GMAX_SOURCE_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT DEFINED ENV{GMAX_YOSYS} OR NOT EXISTS "$ENV{GMAX_YOSYS}")
    if(DEFINED ENV{GMAX_G0_3_GATE})
        message(FATAL_ERROR "G0.3 gate requires GMAX_YOSYS to point at the pinned Yosys binary")
    endif()
    message(STATUS "Skipping G0.3 thread determinism test: GMAX_YOSYS is not set to an existing binary")
    return()
endif()

set(netlist_dir "${GMAX_SOURCE_DIR}/tests/benchmarks/iscas85")
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

set(c432_dfdb "${WORK_DIR}/c432.dfdb")
set(c432_faults "${WORK_DIR}/c432.faults")
run_gmax(0 read_verilog "${netlist_dir}/c432.v" "${c432_dfdb}" c432)
run_gmax(0 write_faults "${c432_dfdb}" "${c432_faults}")

# Both runs write into the same directory and the same report paths, so the
# only manifest field that may differ is the recorded worker count. The first
# run's reports are preserved before the second one overwrites them.
set(report "${WORK_DIR}/report")
set(one_coverage "${WORK_DIR}/coverage.one.json")
set(one_manifest "${WORK_DIR}/manifest.one.json")
run_gmax(0 run_atpg "${c432_dfdb}" "${c432_faults}" 100000 0 --threads 1 "${report}")
if(NOT GMAX_OUT MATCHES "detected: 479" OR NOT GMAX_OUT MATCHES "untestable: 5")
    message(FATAL_ERROR "one-thread c432 run did not reach the expected coverage:\n${GMAX_OUT}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${report}/coverage.json" "${one_coverage}"
    RESULT_VARIABLE copy_coverage)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${report}/manifest.json" "${one_manifest}"
    RESULT_VARIABLE copy_manifest)
if(NOT copy_coverage STREQUAL "0" OR NOT copy_manifest STREQUAL "0")
    message(FATAL_ERROR "cannot preserve the one-thread reports")
endif()

run_gmax(0 run_atpg "${c432_dfdb}" "${c432_faults}" 100000 0 --threads 4 "${report}")
if(NOT GMAX_OUT MATCHES "detected: 479" OR NOT GMAX_OUT MATCHES "untestable: 5")
    message(FATAL_ERROR "four-thread c432 run did not reach the expected coverage:\n${GMAX_OUT}")
endif()
if(NOT EXISTS "${report}/coverage.json" OR NOT EXISTS "${report}/manifest.json")
    message(FATAL_ERROR "missing coverage.json or manifest.json in ${report}")
endif()

file(READ "${one_coverage}" coverage_one)
file(READ "${report}/coverage.json" coverage_four)
if(NOT "${coverage_one}" STREQUAL "${coverage_four}")
    message(FATAL_ERROR "coverage.json differs between the 1-thread and 4-thread runs")
endif()

file(READ "${one_manifest}" manifest_one)
file(READ "${report}/manifest.json" manifest_four)
string(REGEX MATCH "\"threads\":[0-9]+" threads_one "${manifest_one}")
string(REGEX MATCH "\"threads\":[0-9]+" threads_four "${manifest_four}")
if(NOT "${threads_one}" STREQUAL "\"threads\":1")
    message(FATAL_ERROR "the 1-thread manifest does not record threads 1: ${threads_one}")
endif()
if(NOT "${threads_four}" STREQUAL "\"threads\":4")
    message(FATAL_ERROR "the 4-thread manifest does not record threads 4: ${threads_four}")
endif()

# The only permitted manifest difference is the recorded worker count.
string(REGEX REPLACE "\"threads\":[0-9]+" "\"threads\":N" normalized_one "${manifest_one}")
string(REGEX REPLACE "\"threads\":[0-9]+" "\"threads\":N" normalized_four "${manifest_four}")
if(NOT "${normalized_one}" STREQUAL "${normalized_four}")
    message(FATAL_ERROR "manifests differ beyond options.threads")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")

# G0.1 / WP-011: c432 and c499 must go from a netlist through the pinned Yosys
# frontend, a .dfdb, a .faults list, and PODEM plus the SAT closer to a coverage
# report and run manifest. The test is skipped when the pinned Yosys executable
# is not available; GMAX_G0_1_GATE=1 makes that absence a hard failure so CI
# cannot pass without the coverage evidence. Otherwise it validates the expected
# benchmark counts and hashes and reruns c499 in place to catch nondeterministic
# report bytes.

foreach(required GMAX_BINARY GMAX_SOURCE_DIR WORK_DIR GMAX_REPORTS_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

if(NOT DEFINED ENV{GMAX_YOSYS} OR NOT EXISTS "$ENV{GMAX_YOSYS}")
    if(DEFINED ENV{GMAX_G0_1_GATE})
        message(FATAL_ERROR "G0.1 gate requires GMAX_YOSYS to point at the pinned Yosys binary")
    endif()
    message(STATUS "Skipping ISCAS'85 end-to-end test: GMAX_YOSYS is not set to an existing binary")
    return()
endif()

set(netlist_dir "${GMAX_SOURCE_DIR}/tests/benchmarks/iscas85")
file(REMOVE_RECURSE "${WORK_DIR}" "${GMAX_REPORTS_DIR}/iscas85")
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

function(check_absent text needle label)
    string(FIND "${text}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${label}: unexpected '${needle}' in:\n${text}")
    endif()
endfunction()

set(c432_netlist "${netlist_dir}/c432.v")
set(c432_dfdb "${WORK_DIR}/c432.dfdb")
set(c432_faults "${WORK_DIR}/c432.faults")
set(c432_report "${WORK_DIR}/c432-report")

run_gmax(0 read_verilog "${c432_netlist}" "${c432_dfdb}" c432)
check_text("${GMAX_OUT}" "gates: 314" "c432 ingest")
check_text("${GMAX_OUT}" "nets: 350" "c432 ingest")
check_text("${GMAX_OUT}" "netlist hash: 413dd789" "c432 ingest")

run_gmax(0 write_faults "${c432_dfdb}" "${c432_faults}")
check_text("${GMAX_OUT}" "raw faults: 700" "c432 fault list")
check_text("${GMAX_OUT}" "collapsed faults: 484" "c432 fault list")

run_gmax(0 run_atpg "${c432_dfdb}" "${c432_faults}" 100000 0 "${c432_report}")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "c432 run_atpg wrote to stderr: ${GMAX_ERR}")
endif()
check_text("${GMAX_OUT}" "detected: 479" "c432 ATPG")
check_text("${GMAX_OUT}" "untestable: 5" "c432 ATPG")
check_text("${GMAX_OUT}" "aborted: 0" "c432 ATPG")
check_text("${GMAX_OUT}" "coverage raw: 692/700" "c432 ATPG")
check_text("${GMAX_OUT}" "coverage collapsed: 479/484" "c432 ATPG")
check_text("${GMAX_OUT}" "coverage testable: 479/479" "c432 ATPG")
check_text("${GMAX_OUT}" "coverage atpg: 479/484" "c432 ATPG")
check_text("${GMAX_OUT}" "netlist hash: 413dd789" "c432 ATPG")
check_text("${GMAX_OUT}" "fault-list hash: 424032e8" "c432 ATPG")
check_text("${GMAX_OUT}" "fault 51 0: untestable" "c432 SAT proof")
check_text("${GMAX_OUT}" "fault 168 1: detected" "c432 SAT recovery")
check_text("${GMAX_OUT}" "coverage report: '${c432_report}/coverage.json'" "c432 ATPG")
check_text("${GMAX_OUT}" "run manifest: '${c432_report}/manifest.json'" "c432 ATPG")

set(c432_coverage "${c432_report}/coverage.json")
set(c432_manifest "${c432_report}/manifest.json")
if(NOT EXISTS "${c432_coverage}" OR NOT EXISTS "${c432_manifest}")
    message(FATAL_ERROR "c432 reports were not written")
endif()
file(READ "${c432_coverage}" coverage)
file(READ "${c432_manifest}" manifest)
check_text("${coverage}" "gmax-coverage-3" "c432 coverage schema")
check_text("${coverage}" "\"netlist_hash\":\"413dd789\"" "c432 coverage netlist hash")
check_text("${coverage}" "\"fault_list_hash\":\"424032e8\"" "c432 coverage fault hash")
check_text("${coverage}" "\"counts\":{\"raw\":700,\"collapsed\":484,\"detected\":479,\"untestable\":5,\"aborted\":0,\"raw_detected\":692,\"raw_untestable\":8,\"raw_aborted\":0}" "c432 coverage counts")
check_text("${coverage}" "\"raw\":{\"detected\":692,\"denominator\":700,\"percent\":98.857143}" "c432 raw coverage")
check_text("${coverage}" "\"collapsed\":{\"detected\":479,\"denominator\":484,\"percent\":98.966942}" "c432 collapsed coverage")
check_text("${coverage}" "\"testable\":{\"detected\":479,\"denominator\":479,\"percent\":100.000000}" "c432 testable coverage")
check_text("${coverage}" "\"atpg\":{\"detected\":479,\"denominator\":484,\"percent\":98.966942}" "c432 ATPG coverage")
check_text("${coverage}" "\"net\":51,\"name\":" "c432 SAT-proven fault record")
check_text("${coverage}" "\"stuck\":0,\"status\":\"untestable\",\"method\":\"sat\"" "c432 SAT proof method")
check_text("${manifest}" "gmax-run-manifest-3" "c432 manifest schema")
check_text("${manifest}" "\"netlist\":{\"path\":\"${c432_dfdb}\",\"hash\":\"413dd789\",\"algorithm\":\"crc32\"}" "c432 manifest netlist")
check_text("${manifest}" "\"fault_list\":{\"path\":\"${c432_faults}\",\"hash\":\"424032e8\",\"algorithm\":\"crc32\",\"raw_count\":700,\"collapsed_count\":484}" "c432 manifest faults")
check_text("${manifest}" "\"max_backtracks\":100000" "c432 manifest options")
check_text("${manifest}" "\"status\":\"complete\"" "c432 manifest result")
check_text("${manifest}" "\"detected\":479" "c432 manifest result")
check_text("${manifest}" "\"untestable\":5" "c432 manifest result")

# A time-budget abort must not hand the fault to the SAT closer with a fixed
# conflict allowance. With a 1 ms per-fault budget PODEM exhausts its deadline
# before proving the hard faults, so those faults must stay aborted and no SAT
# line may appear. This is the regression for overrunning max-time-ms.
set(c432_time_report "${WORK_DIR}/c432-time-report")
run_gmax(4 run_atpg "${c432_dfdb}" "${c432_faults}" 100000 1 "${c432_time_report}")
if(NOT GMAX_OUT MATCHES "aborted: [1-9][0-9]*")
    message(FATAL_ERROR "c432 1ms run did not report a time-budget abort:\n${GMAX_OUT}")
endif()
check_absent("${GMAX_OUT}" "[sat]" "c432 1ms SAT fallback")

set(c499_netlist "${netlist_dir}/c499.v")
set(c499_dfdb "${WORK_DIR}/c499.dfdb")
set(c499_faults "${WORK_DIR}/c499.faults")
set(c499_report "${WORK_DIR}/c499-report")

run_gmax(0 read_verilog "${c499_netlist}" "${c499_dfdb}" c499)
check_text("${GMAX_OUT}" "gates: 246" "c499 ingest")
check_text("${GMAX_OUT}" "nets: 287" "c499 ingest")
check_text("${GMAX_OUT}" "netlist hash: bebace97" "c499 ingest")

run_gmax(0 write_faults "${c499_dfdb}" "${c499_faults}")
check_text("${GMAX_OUT}" "raw faults: 574" "c499 fault list")
check_text("${GMAX_OUT}" "collapsed faults: 574" "c499 fault list")

run_gmax(0 run_atpg "${c499_dfdb}" "${c499_faults}" 100000 0 "${c499_report}")
if(NOT "${GMAX_ERR}" STREQUAL "")
    message(FATAL_ERROR "c499 run_atpg wrote to stderr: ${GMAX_ERR}")
endif()
check_text("${GMAX_OUT}" "detected: 574" "c499 ATPG")
check_text("${GMAX_OUT}" "untestable: 0" "c499 ATPG")
check_text("${GMAX_OUT}" "aborted: 0" "c499 ATPG")
check_text("${GMAX_OUT}" "coverage raw: 574/574" "c499 ATPG")
check_text("${GMAX_OUT}" "coverage collapsed: 574/574" "c499 ATPG")
check_text("${GMAX_OUT}" "coverage testable: 574/574" "c499 ATPG")
check_text("${GMAX_OUT}" "netlist hash: bebace97" "c499 ATPG")
check_text("${GMAX_OUT}" "fault-list hash: 6796ff49" "c499 ATPG")
check_text("${GMAX_OUT}" "fault 270 1: detected" "c499 SAT recovery")

set(c499_coverage "${c499_report}/coverage.json")
set(c499_manifest "${c499_report}/manifest.json")
if(NOT EXISTS "${c499_coverage}" OR NOT EXISTS "${c499_manifest}")
    message(FATAL_ERROR "c499 reports were not written")
endif()
file(READ "${c499_coverage}" coverage)
file(READ "${c499_manifest}" manifest)
check_text("${coverage}" "\"netlist_hash\":\"bebace97\"" "c499 coverage netlist hash")
check_text("${coverage}" "\"fault_list_hash\":\"6796ff49\"" "c499 coverage fault hash")
check_text("${coverage}" "\"counts\":{\"raw\":574,\"collapsed\":574,\"detected\":574,\"untestable\":0,\"aborted\":0,\"raw_detected\":574,\"raw_untestable\":0,\"raw_aborted\":0}" "c499 coverage counts")
check_text("${coverage}" "\"testable\":{\"detected\":574,\"denominator\":574,\"percent\":100.000000}" "c499 testable coverage")
check_text("${coverage}" "\"net\":270,\"name\":" "c499 SAT-recovered fault record")
check_text("${coverage}" "\"stuck\":1,\"status\":\"detected\",\"method\":\"sat\"" "c499 SAT recovery method")
check_text("${manifest}" "\"fault_list\":{\"path\":\"${c499_faults}\",\"hash\":\"6796ff49\",\"algorithm\":\"crc32\",\"raw_count\":574,\"collapsed_count\":574}" "c499 manifest faults")

# Run the same c499 command again in the same directory. All report paths are
# unchanged, so the manifest must be byte-identical as well as the coverage.
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${c499_coverage}" "${WORK_DIR}/c499.coverage.first"
    RESULT_VARIABLE copy_result)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy "${c499_manifest}" "${WORK_DIR}/c499.manifest.first"
    RESULT_VARIABLE copy_result2)
if(NOT copy_result STREQUAL "0" OR NOT copy_result2 STREQUAL "0")
    message(FATAL_ERROR "cannot preserve the first c499 reports")
endif()
run_gmax(0 run_atpg "${c499_dfdb}" "${c499_faults}" 100000 0 "${c499_report}")
file(READ "${c499_coverage}" coverage_again)
file(READ "${c499_manifest}" manifest_again)
file(READ "${WORK_DIR}/c499.coverage.first" coverage_first)
file(READ "${WORK_DIR}/c499.manifest.first" manifest_first)
if(NOT "${coverage_again}" STREQUAL "${coverage_first}")
    message(FATAL_ERROR "c499 coverage report is not byte-identical across repeated runs")
endif()
if(NOT "${manifest_again}" STREQUAL "${manifest_first}")
    message(FATAL_ERROR "c499 run manifest is not byte-identical across repeated runs")
endif()

# Preserve the gate evidence outside the work directory. The manifest embeds
# the input paths, so these hashes identify this run's artifacts; the gate pack
# records them alongside the run identity.
file(MAKE_DIRECTORY "${GMAX_REPORTS_DIR}/iscas85/c432" "${GMAX_REPORTS_DIR}/iscas85/c499")
file(COPY "${c432_coverage}" "${c432_manifest}" DESTINATION "${GMAX_REPORTS_DIR}/iscas85/c432")
file(COPY "${c499_coverage}" "${c499_manifest}" DESTINATION "${GMAX_REPORTS_DIR}/iscas85/c499")

file(REMOVE_RECURSE "${WORK_DIR}")

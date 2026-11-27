
# ADR-013 section 5: DFT003 across a sequential procedure's clock boundary. The
# fixture has two clock domains with the first feeding the second, so the derived
# procedure captures clk_a and then clk_b across a boundary.
#
#   crossing        no lockup latch and no false path, so the run must refuse;
#   lockup          a DLAT on the path is the lockup latch section 5 accepts;
#   object path     a false path between the two flops (object endpoints) excuses it;
#   clock path      a false path between the two pulsed clocks is DFT019's subject,
#                   removes the capture edge, and therefore excuses nothing.
#
# The clock-path case is also the provenance test: `-from [get_clocks clk_a]` and
# `-from [get_ports clk_a]` produce the same name, and only the recorded provenance
# tells the two apart after the database is re-saved.

foreach(required GMAX_BINARY FIXTURE_BINARY WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(check_contains text needle what)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${what}: '${needle}' not found in: ${text}")
    endif()
endfunction()

function(check_absent text needle what)
    string(FIND "${text}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${what}: '${needle}' must be absent from: ${text}")
    endif()
endfunction()

function(build_fixture mode)
    execute_process(
        COMMAND "${FIXTURE_BINARY}" "${mode}" "${WORK_DIR}/${mode}.dfdb" "${WORK_DIR}/${mode}.faults"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE fixture_out
        ERROR_VARIABLE fixture_err)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "fixture ${mode} failed (${result}): ${fixture_err}${fixture_out}")
    endif()
endfunction()

function(attach name sdc)
    execute_process(
        COMMAND "${GMAX_BINARY}" read_sdc "${sdc}" --design "${WORK_DIR}/${name}.dfdb"
                --output "${WORK_DIR}/${name}_attached.dfdb"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE attach_out
        ERROR_VARIABLE attach_err)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: read_sdc failed (${result}): ${attach_err}${attach_out}")
    endif()
endfunction()

function(run name expect)
    file(REMOVE_RECURSE "${WORK_DIR}/${name}-report")
    execute_process(
        COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/${name}_attached.dfdb"
                "${WORK_DIR}/${name}.faults" 100000 0 "${WORK_DIR}/${name}-report"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE run_out
        ERROR_VARIABLE run_err)
    if(NOT "${result}" STREQUAL "${expect}")
        message(FATAL_ERROR
            "${name}: expected exit ${expect}, got ${result}: ${run_err}${run_out}")
    endif()
    set(RUN_OUT "${run_out}" PARENT_SCOPE)
    set(RUN_ERR "${run_err}" PARENT_SCOPE)
endfunction()

# The refusal block wraps its detail at the printer's width, so the assertions name
# the head and the tail of the finding rather than one unwrapped line.
set(finding_head "crossing-without-lockup: cycle 0 captures 'clk_a' before cycle 1")
set(finding_tail "no lockup latch or false path")

set(clock_sdc "${WORK_DIR}/clocks.sdc")
file(WRITE "${clock_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n")

set(object_sdc "${WORK_DIR}/object-path.sdc")
file(WRITE "${object_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_false_path -from [get_nets q_a] -to [get_nets n_cross]\n")

set(clock_path_sdc "${WORK_DIR}/clock-path.sdc")
file(WRITE "${clock_path_sdc}"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n"
    "set_false_path -from [get_clocks clk_a] -to [get_clocks clk_b]\n")

build_fixture(crossing)
build_fixture(lockup)

# 1. An unexcused crossing refuses before grading and writes nothing.
attach(crossing "${clock_sdc}")
run(crossing 2)
check_contains("${RUN_ERR}" "gmax: cannot apply the capture procedure; refusing to grade"
    "crossing header")
check_contains("${RUN_ERR}" "${finding_head}" "crossing finding head")
check_contains("${RUN_ERR}" "${finding_tail}" "crossing finding tail")
if(EXISTS "${WORK_DIR}/crossing-report/coverage.json")
    message(FATAL_ERROR "a refused crossing wrote a coverage report")
endif()

# 2. The same procedure with a lockup latch on the path grades.
attach(lockup "${clock_sdc}")
run(lockup 0)
check_absent("${RUN_ERR}" "crossing-without-lockup" "lockup run refuses nothing")
file(READ "${WORK_DIR}/lockup-report/coverage.json" lockup_coverage)
check_contains("${lockup_coverage}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"sequential\",\"derived\":true,\"hash\":\""
    "lockup capture")
file(READ "${WORK_DIR}/lockup-report/manifest.json" lockup_manifest)
check_contains("${lockup_manifest}"
    "\"cycles\":[{\"groups\":[{\"clocks\":[\"clk_a\"],\"edge\":0}]},{\"groups\":[{\"clocks\":[\"clk_b\"],\"edge\":0}]}]"
    "lockup procedure body")

# 3. A false path between the two flops names objects, so it excuses the crossing.
configure_file("${WORK_DIR}/crossing.dfdb" "${WORK_DIR}/object_path.dfdb" COPYONLY)
configure_file("${WORK_DIR}/crossing.faults" "${WORK_DIR}/object_path.faults" COPYONLY)
attach(object_path "${object_sdc}")
run(object_path 0)
check_absent("${RUN_ERR}" "crossing-without-lockup" "an object false path excuses the crossing")

# 4. A false path between the two pulsed clocks is DFT019's edge, not an excuse.
configure_file("${WORK_DIR}/crossing.dfdb" "${WORK_DIR}/clock_path.dfdb" COPYONLY)
configure_file("${WORK_DIR}/crossing.faults" "${WORK_DIR}/clock_path.faults" COPYONLY)
attach(clock_path "${clock_path_sdc}")
run(clock_path 2)
check_contains("${RUN_ERR}" "${finding_head}" "a clock false path excuses nothing (head)")
check_contains("${RUN_ERR}" "${finding_tail}" "a clock false path excuses nothing (tail)")

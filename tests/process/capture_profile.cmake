# ADR-013 sections 1 and 5: the profile that declares the capture procedure, wired
# into run_atpg. A declared grouped procedure captures two clock domains at different
# edge times of one cycle, so the crossing between them is DFT003's same-cycle half:
# the design without a lockup latch refuses, and the one with it grades while both
# reports name `grouped` as the procedure and `false` as `derived`.
#
# The rest pins the ways a profile does not produce a procedure the run can apply: a
# shape the constraints forbid (condition 1), a document the reader rejects (an
# unknown clock, which refuses without the refusal block because no procedure reached
# the conditions), and a document that declares nothing, which must still derive.

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

function(attach name)
    execute_process(
        COMMAND "${GMAX_BINARY}" read_sdc "${WORK_DIR}/clocks.sdc" --design "${WORK_DIR}/${name}.dfdb"
                --output "${WORK_DIR}/${name}_attached.dfdb"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE attach_out
        ERROR_VARIABLE attach_err)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: read_sdc failed (${result}): ${attach_err}${attach_out}")
    endif()
endfunction()

# The profile is a flag, so one attached database grades with any of them. The report
# directory is removed first: a refusal must leave no directory behind.
function(run name profile expect)
    file(REMOVE_RECURSE "${WORK_DIR}/${name}-report")
    execute_process(
        COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/${name}_attached.dfdb"
                "${WORK_DIR}/${name}.faults" 100000 0 "${WORK_DIR}/${name}-report"
                --profile "${profile}"
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

file(WRITE "${WORK_DIR}/clocks.sdc"
    "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
    "create_clock -name clk_b -period 20 [get_ports clk_b]\n")

file(WRITE "${WORK_DIR}/grouped.json"
    "{\"capture\": {\"kind\": \"grouped\", \"cycles\": [{\"groups\": [\n"
    "  {\"name\": \"fast\", \"clocks\": [\"clk_a\"], \"edge\": 0},\n"
    "  {\"clocks\": [\"clk_b\"], \"edge\": 20}]}]}}\n")

file(WRITE "${WORK_DIR}/simultaneous.json"
    "{\"capture\": {\"kind\": \"simultaneous\", \"cycles\": [{\"groups\": [\n"
    "  {\"clocks\": [\"clk_a\", \"clk_b\"]}]}]}}\n")

file(WRITE "${WORK_DIR}/unknown-clock.json"
    "{\"capture\": {\"kind\": \"grouped\", \"cycles\": [{\"groups\": [\n"
    "  {\"clocks\": [\"clk_a\"], \"edge\": 0},\n"
    "  {\"clocks\": [\"clk_z\"], \"edge\": 20}]}]}}\n")

file(WRITE "${WORK_DIR}/no-capture.json" "{\"tool\": \"gmax\"}\n")

build_fixture(lockup)
build_fixture(crossing)
attach(lockup)
attach(crossing)

# 1. The declared grouped procedure on the design whose path holds the value: the run
#    grades, and neither report describes the derived sequential procedure.
run(lockup "${WORK_DIR}/grouped.json" 0)
check_absent("${RUN_ERR}" "cannot apply the capture procedure"
    "the declared grouped procedure applies")
file(READ "${WORK_DIR}/lockup-report/coverage.json" lockup_coverage)
check_contains("${lockup_coverage}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"grouped\",\"derived\":false,\"hash\":\""
    "the coverage report names the declared grouped procedure")
file(READ "${WORK_DIR}/lockup-report/manifest.json" lockup_manifest)
check_contains("${lockup_manifest}"
    "\"procedure\":\"grouped\",\"derived\":false"
    "the manifest repeats the provenance")
check_contains("${lockup_manifest}"
    "\"cycles\":[{\"groups\":[{\"clocks\":[\"clk_a\"],\"edge\":0},"
    "{\"clocks\":[\"clk_b\"],\"edge\":20}]}]"
    "the manifest carries the declared cycles and their edges")

# 2. The same procedure on the design with no lockup latch: section 5's same-cycle half
#    is DFT003 across the two groups of one cycle, so the finding names two edge times
#    of one cycle rather than two cycles.
run(crossing "${WORK_DIR}/grouped.json" 2)
check_contains("${RUN_ERR}" "gmax: cannot apply the capture procedure; refusing to grade"
    "crossing header")
check_contains("${RUN_ERR}" "crossing-without-lockup: cycle 0 pulses 'clk_a' at edge 0"
    "same-cycle finding head")
check_contains("${RUN_ERR}" "no lockup latch or false path" "same-cycle finding tail")
check_absent("${RUN_ERR}" "before cycle 1" "the finding names one cycle, not a pair")
if(EXISTS "${WORK_DIR}/crossing-report")
    message(FATAL_ERROR "a refused grouped procedure left a report directory")
endif()

# 3. A shape the constraints forbid: simultaneous capture puts both clocks in one group,
#    and with no set_clock_groups the SDC keeps them apart, so condition 1 refuses the
#    very design that graded in case 1. The declared kind is what the run applied.
run(lockup "${WORK_DIR}/simultaneous.json" 2)
check_contains("${RUN_ERR}"
    "simultaneous-across-groups: cycle 0 pulses 'clk_a' (no declared group) and 'clk_b' (no declared group), which set_clock_groups keeps apart"
    "condition 1 finding")
check_absent("${RUN_ERR}" "crossing-without-lockup" "one group is one capture edge")
if(EXISTS "${WORK_DIR}/lockup-report")
    message(FATAL_ERROR "a refused simultaneous procedure left a report directory")
endif()

# 4. A profile naming a clock the SDC does not declare is an unreadable procedure: the
#    reader's finding prints with its path and the run writes nothing. It carries no
#    refusal code and no refusal header, because no procedure reached the conditions.
run(lockup "${WORK_DIR}/unknown-clock.json" 2)
check_contains("${RUN_ERR}"
    "capture-profile-unknown-clock: clock 'clk_z' is not declared in the constraints"
    "unknown clock finding")
check_contains("${RUN_ERR}" "(capture.cycles[0].groups[1].clocks[0])" "unknown clock path")
check_absent("${RUN_ERR}" "cannot apply the capture procedure"
    "an unreadable profile is not a refusal block")
if(EXISTS "${WORK_DIR}/lockup-report")
    message(FATAL_ERROR "a refused profile left a report directory")
endif()

# 5. A profile with no capture object declares nothing, so the engine derives the
#    procedure and the report says so: provenance follows the document, not the flag.
run(lockup "${WORK_DIR}/no-capture.json" 0)
file(READ "${WORK_DIR}/lockup-report/coverage.json" derived_coverage)
check_contains("${derived_coverage}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"sequential\",\"derived\":true,\"hash\":\""
    "a profile without a capture object still derives")

# 6. An empty path is a usage error rather than a silent fall back to the derived
#    procedure: the flag was given, so the run must not ignore it.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/lockup_attached.dfdb" "${WORK_DIR}/lockup.faults"
            100000 0 "${WORK_DIR}/lockup-report" --profile ""
    RESULT_VARIABLE empty_result
    OUTPUT_VARIABLE empty_out
    ERROR_VARIABLE empty_err)
if(NOT "${empty_result}" STREQUAL "1")
    message(FATAL_ERROR
        "an empty profile path returned ${empty_result}, expected 1: ${empty_err}${empty_out}")
endif()
check_contains("${empty_err}" "gmax: profile path must not be empty" "empty profile path")

# ADR-013 section 4 condition 5: a design whose declared clocks clock no
# state-holding gate leaves nothing to pulse, so run_atpg refuses it. The finding
# is a stable code, the run exits 2, it prints no fault count, it writes no
# report, it leaves the database byte-identical, and a second run prints the same
# stderr (ADR-011).

foreach(required GMAX_BINARY FIXTURE_BINARY WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

function(check_contains text needle label)
    string(FIND "${text}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: missing '${needle}' in:\n${text}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(database "${WORK_DIR}/design.dfdb")
set(faults "${WORK_DIR}/design.faults")
set(reports "${WORK_DIR}/reports")

execute_process(
    COMMAND "${FIXTURE_BINARY}" no-clock "${database}" "${faults}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${result}): ${fixture_err}${fixture_out}")
endif()

# ADR-013 section 4: a refusal never rewrites the database, so the file hash must
# survive the run unchanged.
file(SHA256 "${database}" hash_before)

execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}" 100000 0 "${reports}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "a refused run returned ${result}, expected 2: ${err}${out}")
endif()

check_contains("${err}" "gmax: cannot apply the capture procedure; refusing to grade" "header")
check_contains("${err}"
    "gmax:   no-capture-clock: 'clk_a' clocks no state-holding gate and was dropped"
    "clk_a finding")
check_contains("${err}"
    "gmax:   no-capture-clock: 'clk_b' clocks no state-holding gate and was dropped"
    "clk_b finding")

# The header appears once even when several findings do, and every dropped clock
# gets exactly one line.
string(REGEX MATCHALL "refusing to grade" headers "${err}")
list(LENGTH headers header_count)
if(NOT header_count EQUAL 1)
    message(FATAL_ERROR "expected one header, found ${header_count}: ${err}")
endif()
string(REGEX MATCHALL "no-capture-clock:" findings "${err}")
list(LENGTH findings finding_count)
if(NOT finding_count EQUAL 2)
    message(FATAL_ERROR "expected two findings, found ${finding_count}: ${err}")
endif()

# A refused run stops before the fault list is read, so stdout stays empty.
if(out MATCHES "raw faults:")
    message(FATAL_ERROR "a refused run printed fault counts: ${out}")
endif()

# Section 3: a refusal writes no coverage report and no manifest. The report
# directory itself is still created during argument handling on this branch, so
# this test asserts the artifacts rather than the directory; the deferred-creation
# change asserts the directory elsewhere.
foreach(artifact coverage.json manifest.json)
    if(EXISTS "${reports}/${artifact}")
        message(FATAL_ERROR "a refused run wrote ${artifact}")
    endif()
endforeach()

file(SHA256 "${database}" hash_after)
if(NOT hash_before STREQUAL hash_after)
    message(FATAL_ERROR "a refused run rewrote the database")
endif()

# Determinism: the same inputs print the same block, byte for byte.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${database}" "${faults}" 100000 0 "${reports}"
    RESULT_VARIABLE second_result
    OUTPUT_VARIABLE second_out
    ERROR_VARIABLE second_err)
if(NOT "${second_result}" STREQUAL "2")
    message(FATAL_ERROR "the repeat run returned ${second_result}, expected 2: ${second_err}")
endif()
if(NOT "${second_err}" STREQUAL "${err}")
    message(FATAL_ERROR
        "the refusal block is not deterministic:\nfirst:\n${err}\nsecond:\n${second_err}")
endif()

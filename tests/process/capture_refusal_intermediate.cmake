# ADR-013 section 4 condition 2: a domain a non-final cycle captures holds a value
# later cycles read, so it is usable only when every element that captures it is a
# scan cell, or when the cycle declares that domain masked. The fixture captures clk_a
# and then clk_b across a lockup latch, so DFT003 is satisfied and the scan state is
# the only question: partial-scan marks three of the eight clk_a elements, full-scan
# marks all eight.
#
# A refusal owes what section 4 asks of every condition: exit 2, one header, no fault
# count, no report directory and an untouched database.

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

function(check_absent text needle label)
    string(FIND "${text}" "${needle}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "${label}: unexpected '${needle}' in:\n${text}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(build_fixture mode stem)
    execute_process(
        COMMAND "${FIXTURE_BINARY}" "${mode}" "${WORK_DIR}/${stem}.dfdb" "${WORK_DIR}/${stem}.faults"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE fixture_out
        ERROR_VARIABLE fixture_err)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "fixture ${mode} failed (${result}): ${fixture_err}${fixture_out}")
    endif()
endfunction()

build_fixture(partial-scan partial)
build_fixture(full-scan full)

# The declared procedure captures clk_a first, so clk_a is the domain section 2 asks
# about. No cycle states an intermediate value, which is the reader's `scanned`
# default and therefore the claim the run has to check.
file(WRITE "${WORK_DIR}/sequential.json"
    "{\"capture\": {\"kind\": \"sequential\", \"cycles\": [\n"
    "  {\"groups\": [{\"clocks\": [\"clk_a\"], \"edge\": 0}]},\n"
    "  {\"groups\": [{\"clocks\": [\"clk_b\"], \"edge\": 0}]}]}}\n")

# The same shape with the domain declared masked, which condition 2 accepts.
file(WRITE "${WORK_DIR}/masked.json"
    "{\"capture\": {\"kind\": \"sequential\", \"cycles\": [\n"
    "  {\"intermediate\": \"masked\", \"groups\": [{\"clocks\": [\"clk_a\"], \"edge\": 0}]},\n"
    "  {\"groups\": [{\"clocks\": [\"clk_b\"], \"edge\": 0}]}]}}\n")

file(SHA256 "${WORK_DIR}/partial.dfdb" hash_before)

# 1. Three of eight clk_a elements are scan cells: the domain is neither fully scanned
#    nor masked, so the run refuses and names how far it is from scanned.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/partial.dfdb" "${WORK_DIR}/partial.faults"
            100000 0 "${WORK_DIR}/partial-report" --profile "${WORK_DIR}/sequential.json"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${result}" STREQUAL "2")
    message(FATAL_ERROR "a partially scanned domain returned ${result}, expected 2: ${err}${out}")
endif()
check_contains("${err}" "gmax: cannot apply the capture procedure; refusing to grade" "header")
check_contains("${err}"
    "gmax:   intermediate-state: domain 'clk_a' in cycle 0 is neither fully scanned nor masked"
    "the finding names the cycle and the clock")
check_contains("${err}" "gmax:     3 of 8 flops on 'clk_a' are scan cells"
    "the fact states how far the domain is from scanned")

string(REGEX MATCHALL "refusing to grade" headers "${err}")
list(LENGTH headers header_count)
if(NOT header_count EQUAL 1)
    message(FATAL_ERROR "expected one header, found ${header_count}: ${err}")
endif()
string(REGEX MATCHALL "intermediate-state:" findings "${err}")
list(LENGTH findings finding_count)
if(NOT finding_count EQUAL 1)
    message(FATAL_ERROR "expected one finding, found ${finding_count}: ${err}")
endif()
if(out MATCHES "raw faults:")
    message(FATAL_ERROR "a refused run printed fault counts: ${out}")
endif()
if(EXISTS "${WORK_DIR}/partial-report")
    message(FATAL_ERROR "a refused run left a report directory")
endif()

# Determinism: the same inputs print the same block, byte for byte.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/partial.dfdb" "${WORK_DIR}/partial.faults"
            100000 0 "${WORK_DIR}/partial-report" --profile "${WORK_DIR}/sequential.json"
    RESULT_VARIABLE repeat_result
    OUTPUT_VARIABLE repeat_out
    ERROR_VARIABLE repeat_err)
if(NOT "${repeat_result}" STREQUAL "2")
    message(FATAL_ERROR "the repeat run returned ${repeat_result}, expected 2: ${repeat_err}")
endif()
if(NOT "${repeat_err}" STREQUAL "${err}")
    message(FATAL_ERROR
        "the refusal block is not deterministic:\nfirst:\n${err}\nsecond:\n${repeat_err}")
endif()

file(SHA256 "${WORK_DIR}/partial.dfdb" hash_after)
if(NOT hash_before STREQUAL hash_after)
    message(FATAL_ERROR "a refused run rewrote the database")
endif()

# 2. The boundary: the same procedure on the fully scanned design grades, and the
#    reports name the declared sequential procedure.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/full.dfdb" "${WORK_DIR}/full.faults"
            100000 0 "${WORK_DIR}/full-report" --profile "${WORK_DIR}/sequential.json"
    RESULT_VARIABLE full_result
    OUTPUT_VARIABLE full_out
    ERROR_VARIABLE full_err)
if(NOT "${full_result}" STREQUAL "0")
    message(FATAL_ERROR
        "a fully scanned domain must grade, returned ${full_result}: ${full_err}${full_out}")
endif()
check_absent("${full_err}" "intermediate-state" "the fully scanned domain is not refused")
check_absent("${full_err}" "cannot apply the capture procedure" "the procedure applies")
if(NOT full_out MATCHES "raw faults:")
    message(FATAL_ERROR "the boundary run printed no fault count: ${full_out}")
endif()
file(READ "${WORK_DIR}/full-report/coverage.json" full_coverage)
check_contains("${full_coverage}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"sequential\",\"derived\":false,\"hash\":\""
    "the coverage report names the declared sequential procedure")
file(READ "${WORK_DIR}/full-report/manifest.json" full_manifest)
check_contains("${full_manifest}"
    "\"cycles\":[{\"groups\":[{\"clocks\":[\"clk_a\"],\"edge\":0}]},{\"groups\":[{\"clocks\":[\"clk_b\"],\"edge\":0}]}]"
    "the manifest carries both cycles")

# 3. The second boundary: a masked domain is not refused, because section 2 makes the
#    declaration legal. Masking the observations a masked domain feeds is the pattern
#    IR's job and is not implemented yet, so this run grades without applying the
#    mask; the case exists to pin the refusal contract, not that execution.
execute_process(
    COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/partial.dfdb" "${WORK_DIR}/partial.faults"
            100000 0 "${WORK_DIR}/masked-report" --profile "${WORK_DIR}/masked.json"
    RESULT_VARIABLE masked_result
    OUTPUT_VARIABLE masked_out
    ERROR_VARIABLE masked_err)
if(NOT "${masked_result}" STREQUAL "0")
    message(FATAL_ERROR
        "a masked domain must not be refused, returned ${masked_result}: ${masked_err}")
endif()
check_absent("${masked_err}" "intermediate-state" "a masked domain is not refused")
if(NOT masked_out MATCHES "raw faults:")
    message(FATAL_ERROR "the masked run printed no fault count: ${masked_out}")
endif()

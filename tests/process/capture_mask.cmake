# ADR-013 section 2: a masked domain's intermediate value is X, so the run may not
# observe the nets the domain's state elements reach. The fixture's clk_a elements
# drive q_a0 and nothing else that is observable, so masking clk_a removes exactly
# the q_a0 observation: its own fault and the flop output behind it lose their only
# detection path. q_b belongs to the other domain and stays observable, and the dead
# clk_b element is untestable in every run.
#
# The masked run counts the two faults as `masked` inside `untestable`, leaves the
# coverage denominators untouched, and lists them with a status the genuinely
# untestable fault does not share.

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

execute_process(
    COMMAND "${FIXTURE_BINARY}" masking "${WORK_DIR}/mask.dfdb" "${WORK_DIR}/mask.faults"
    RESULT_VARIABLE fixture_result
    OUTPUT_VARIABLE fixture_out
    ERROR_VARIABLE fixture_err)
if(NOT "${fixture_result}" STREQUAL "0")
    message(FATAL_ERROR "fixture failed (${fixture_result}): ${fixture_err}${fixture_out}")
endif()

file(WRITE "${WORK_DIR}/sequential.json"
    "{\"capture\": {\"kind\": \"sequential\", \"cycles\": [\n"
    "  {\"groups\": [{\"clocks\": [\"clk_a\"], \"edge\": 0}]},\n"
    "  {\"groups\": [{\"clocks\": [\"clk_b\"], \"edge\": 0}]}]}}\n")
file(WRITE "${WORK_DIR}/masked.json"
    "{\"capture\": {\"kind\": \"sequential\", \"cycles\": [\n"
    "  {\"intermediate\": \"masked\", \"groups\": [{\"clocks\": [\"clk_a\"], \"edge\": 0}]},\n"
    "  {\"groups\": [{\"clocks\": [\"clk_b\"], \"edge\": 0}]}]}}\n")

# The profile's identity is its path and the CRC32 of its bytes. The bytes carry the
# platform's line endings, so the test pins the shape and the path and leaves the
# computation to the profile unit test.
function(check_profile_input manifest path label)
    string(FIND "${manifest}" "\"inputs\":{\"profile\":{\"path\":\"${path}\",\"hash\":\"" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label}: no profile input naming '${path}' in:
${manifest}")
    endif()
    string(REGEX MATCH
        "\"inputs\":\{\"profile\":\{\"path\":\"[^\"]*\",\"hash\":\"[0-9a-f]+\",\"algorithm\":\"crc32\"\}"
        profile_input "${manifest}")
    if(NOT profile_input)
        message(FATAL_ERROR "${label}: the profile input is not path/hash/algorithm:
${manifest}")
    endif()
endfunction()

function(run profile report)
    file(REMOVE_RECURSE "${report}")
    execute_process(
        COMMAND "${GMAX_BINARY}" run_atpg "${WORK_DIR}/mask.dfdb" "${WORK_DIR}/mask.faults"
                100000 0 "${report}" --profile "${profile}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(RUN_RESULT "${result}" PARENT_SCOPE)
    set(RUN_OUT "${out}" PARENT_SCOPE)
    set(RUN_ERR "${err}" PARENT_SCOPE)
endfunction()

# 1. The same design and procedure without a mask: both element faults are detected
#    and the dead element and the primary output's own fault are untestable, so the
#    four-fault denominator is the same one the masked run must keep.
run("${WORK_DIR}/sequential.json" "${WORK_DIR}/plain-report")
if(NOT "${RUN_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "unmasked run returned ${RUN_RESULT}: ${RUN_ERR}${RUN_OUT}")
endif()
check_contains("${RUN_OUT}" "detected: 2" "unmasked detected")
check_contains("${RUN_OUT}" "untestable: 2" "unmasked untestable")
check_contains("${RUN_OUT}" "masked: 0" "unmasked masked")
file(READ "${WORK_DIR}/plain-report/coverage.json" plain_coverage)
check_contains("${plain_coverage}"
    "\"counts\":{\"raw\":4,\"collapsed\":4,\"detected\":2,\"untestable\":2,\"aborted\":0,\"raw_detected\":2,\"raw_untestable\":2,\"raw_aborted\":0,\"masked\":0,\"raw_masked\":0}"
    "unmasked counts")
file(READ "${WORK_DIR}/plain-report/manifest.json" plain_manifest)
check_contains("${plain_manifest}" "\"capture\":{\"status\":\"enforced\"" "unmasked capture")
# The profile is an input of the run, so the manifest names the document it read and
# the CRC32 of its bytes: the run can be audited from its report alone.
check_profile_input("${plain_manifest}" "${WORK_DIR}/sequential.json" "plain run")

# 2. Masking clk_a removes the q_a0 observation, so the element fault that needed it
#    is counted masked inside untestable and the denominators do not move.
run("${WORK_DIR}/masked.json" "${WORK_DIR}/masked-report")
if(NOT "${RUN_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "masked run returned ${RUN_RESULT}: ${RUN_ERR}${RUN_OUT}")
endif()
check_contains("${RUN_OUT}" "detected: 1" "masked detected")
check_contains("${RUN_OUT}" "untestable: 3" "masked untestable counts the masked fault")
check_contains("${RUN_OUT}" "masked: 1" "masked count")
if(NOT RUN_OUT MATCHES "fault [0-9]+ 0: masked")
    message(FATAL_ERROR "no masked fault line: ${RUN_OUT}")
endif()

file(READ "${WORK_DIR}/masked-report/coverage.json" masked_coverage)
check_contains("${masked_coverage}"
    "\"counts\":{\"raw\":4,\"collapsed\":4,\"detected\":1,\"untestable\":3,\"aborted\":0,\"raw_detected\":1,\"raw_untestable\":3,\"raw_aborted\":0,\"masked\":1,\"raw_masked\":1}"
    "masked counts keep the denominators and move two faults")
check_contains("${masked_coverage}" "\"testable\":{\"detected\":1,\"denominator\":1,"
    "the masked fault is not in the testable denominator")
check_contains("${masked_coverage}" "\"atpg\":{\"detected\":1,\"denominator\":4,"
    "the atpg denominator is unchanged")

# The per-fault list distinguishes the masked faults from the untestable one.
check_contains("${masked_coverage}" "\"name\":\"n_a0\",\"stuck\":0,\"status\":\"masked\""
    "the fault behind the masked observation is masked")
check_contains("${masked_coverage}" "\"name\":\"n_b\",\"stuck\":0,\"status\":\"detected\""
    "the other domain's fault is still detected")
check_contains("${masked_coverage}" "\"name\":\"n_dead\",\"stuck\":0,\"status\":\"untestable\""
    "the genuinely untestable fault keeps its own status")
check_contains("${masked_coverage}" "\"name\":\"q_a0\",\"stuck\":0,\"status\":\"untestable\""
    "a fault the mask cannot explain stays untestable")
string(REGEX MATCHALL "\"status\":\"masked\"" masked_entries "${masked_coverage}")
list(LENGTH masked_entries masked_entry_count)
if(NOT masked_entry_count EQUAL 1)
    message(FATAL_ERROR "expected one masked fault, found ${masked_entry_count}")
endif()

# 3. The mask is part of the procedure the run applied, and the run is deterministic.
file(READ "${WORK_DIR}/masked-report/manifest.json" masked_manifest)
check_profile_input("${masked_manifest}" "${WORK_DIR}/masked.json" "masked run")
check_contains("${masked_coverage}"
    "\"capture\":{\"status\":\"enforced\",\"procedure\":\"sequential\"" "masked capture")
run("${WORK_DIR}/masked.json" "${WORK_DIR}/masked-repeat")
if(NOT "${RUN_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "repeat masked run returned ${RUN_RESULT}: ${RUN_ERR}${RUN_OUT}")
endif()
file(READ "${WORK_DIR}/masked-repeat/coverage.json" repeat_coverage)
if(NOT repeat_coverage STREQUAL masked_coverage)
    message(FATAL_ERROR "the masked coverage is not deterministic")
endif()

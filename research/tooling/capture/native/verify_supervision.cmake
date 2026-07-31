if(SCENARIO STREQUAL "normal")
    set(EXPECTED_REASON "process-exit")
    set(EXPECTED_STATE "complete")
    set(EXPECTED_RESULT 0)
    set(TIMEOUT_MS 5000)
    set(TARGET_ARGUMENTS
        --initial-delay-ms 0
        --module-delay-ms 1
        --lifetime-ms 1
    )
elseif(SCENARIO STREQUAL "timeout")
    set(EXPECTED_REASON "timeout")
    set(EXPECTED_STATE "partial")
    set(EXPECTED_RESULT nonzero)
    set(TIMEOUT_MS 25)
    set(TARGET_ARGUMENTS
        --initial-delay-ms 0
        --module-delay-ms 1
        --lifetime-ms 5000
    )
elseif(SCENARIO STREQUAL "crash")
    set(EXPECTED_REASON "process-crash")
    set(EXPECTED_STATE "partial")
    set(EXPECTED_RESULT nonzero)
    set(TIMEOUT_MS 5000)
    set(TARGET_ARGUMENTS
        --initial-delay-ms 0
        --module-delay-ms 1
        --lifetime-ms 1
        --exit-code 42
    )
elseif(SCENARIO STREQUAL "collector")
    set(EXPECTED_REASON "collector-exit")
    set(EXPECTED_STATE "partial")
    set(EXPECTED_RESULT nonzero)
    set(TIMEOUT_MS 5000)
    set(COLLECTOR_ARGUMENTS
        --collector-argument --exit-after-ms
        --collector-argument 10
        --collector-argument --exit-code
        --collector-argument 23
    )
    set(TARGET_ARGUMENTS
        --initial-delay-ms 0
        --module-delay-ms 1
        --lifetime-ms 5000
    )
else()
    message(FATAL_ERROR "unknown supervision scenario: ${SCENARIO}")
endif()

file(REMOVE "${REPORT}" "${REPORT}.tmp")
execute_process(
    COMMAND
        "${LAUNCHER}"
        --executable "${TARGET}"
        --working-directory "${BIN_DIR}"
        --distribution synthetic-test
        --startup-profile direct
        --probe-host "${PROBE}"
        --collector "${COLLECTOR}"
        ${COLLECTOR_ARGUMENTS}
        --finalization "${REPORT}"
        --timeout-ms "${TIMEOUT_MS}"
        --supervise
        --
        ${TARGET_ARGUMENTS}
    RESULT_VARIABLE RESULT
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERROR_OUTPUT
    TIMEOUT 12
)
string(CONCAT COMBINED_OUTPUT "${OUTPUT}" "${ERROR_OUTPUT}")
if(EXPECTED_RESULT STREQUAL "0")
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR
            "normal supervision failed with ${RESULT}:\n${COMBINED_OUTPUT}")
    endif()
elseif(RESULT EQUAL 0)
    message(FATAL_ERROR
        "partial supervision unexpectedly returned zero:\n${COMBINED_OUTPUT}")
endif()
if(NOT COMBINED_OUTPUT MATCHES
    "event=supervision_finalized.*reason=${EXPECTED_REASON}.*partial=")
    message(FATAL_ERROR
        "supervision output has the wrong reason:\n${COMBINED_OUTPUT}")
endif()
if(NOT EXISTS "${REPORT}")
    message(FATAL_ERROR "supervision report was not written: ${REPORT}")
endif()
file(READ "${REPORT}" REPORT_TEXT)
foreach(REQUIRED
    "contract=elysium.retail-capture-finalization"
    "version=1"
    "state=${EXPECTED_STATE}"
    "reason=${EXPECTED_REASON}"
)
    string(FIND "${REPORT_TEXT}" "${REQUIRED}" POSITION)
    if(POSITION EQUAL -1)
        message(FATAL_ERROR
            "supervision report lacks ${REQUIRED}:\n${REPORT_TEXT}")
    endif()
endforeach()

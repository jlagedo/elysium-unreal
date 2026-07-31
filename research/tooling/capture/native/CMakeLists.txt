cmake_minimum_required(VERSION 3.25)

cmake_policy(SET CMP0091 NEW)
project(ElysiumRetailCaptureNative LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The retail capture native project requires MSVC on Windows.")
endif()
if(NOT ELYSIUM_TARGET_ARCH STREQUAL "x86")
    message(FATAL_ERROR "ELYSIUM_TARGET_ARCH must be x86.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Retail capture binaries must be 32-bit.")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

function(elysium_native_target target)
    target_compile_definitions(${target} PRIVATE NOMINMAX)
    target_compile_options(
        ${target}
        PRIVATE
            /W4
            /WX
            /permissive-
            /sdl
            /utf-8
    )
    target_link_options(${target} PRIVATE /INCREMENTAL:NO /Brepro)
endfunction()

set(ELYSIUM_FLATBUFFERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FlatBuffers")
if(NOT EXISTS "${ELYSIUM_FLATBUFFERS_ROOT}/LICENSE")
    message(
        FATAL_ERROR
        "Pinned FlatBuffers source is missing; run `uv run elysium deps sync`."
    )
endif()
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATLIB OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_SHAREDLIB OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_GRPCTEST OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${ELYSIUM_FLATBUFFERS_ROOT}"
    "${CMAKE_BINARY_DIR}/third_party/FlatBuffers"
    EXCLUDE_FROM_ALL
)

set(
    FLATBUFFERS_SMOKE_SCHEMA
    "${CMAKE_CURRENT_SOURCE_DIR}/../contracts/flatbuffers_smoke.fbs"
)
set(
    FLATBUFFERS_SMOKE_GENERATED_ROOT
    "${CMAKE_CURRENT_BINARY_DIR}/generated/flatbuffers_smoke"
)
set(
    FLATBUFFERS_SMOKE_CPP_ROOT
    "${FLATBUFFERS_SMOKE_GENERATED_ROOT}/cpp"
)
set(
    FLATBUFFERS_SMOKE_PYTHON_ROOT
    "${FLATBUFFERS_SMOKE_GENERATED_ROOT}/python"
)
set(
    FLATBUFFERS_SMOKE_HEADER
    "${FLATBUFFERS_SMOKE_CPP_ROOT}/flatbuffers_smoke_generated.h"
)
set(
    FLATBUFFERS_SMOKE_PYTHON_MODULE
    "${FLATBUFFERS_SMOKE_PYTHON_ROOT}/Elysium/Capture/Smoke/SmokeRecord.py"
)
add_custom_command(
    OUTPUT
        "${FLATBUFFERS_SMOKE_HEADER}"
        "${FLATBUFFERS_SMOKE_PYTHON_MODULE}"
    COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "${FLATBUFFERS_SMOKE_CPP_ROOT}"
        "${FLATBUFFERS_SMOKE_PYTHON_ROOT}"
    COMMAND
        $<TARGET_FILE:flatc>
        --cpp
        -o "${FLATBUFFERS_SMOKE_CPP_ROOT}"
        "${FLATBUFFERS_SMOKE_SCHEMA}"
    COMMAND
        $<TARGET_FILE:flatc>
        --python
        -o "${FLATBUFFERS_SMOKE_PYTHON_ROOT}"
        "${FLATBUFFERS_SMOKE_SCHEMA}"
    DEPENDS
        flatc
        "${FLATBUFFERS_SMOKE_SCHEMA}"
    COMMENT "Generating FlatBuffers smoke bindings with pinned flatc"
    VERBATIM
)
add_custom_target(
    flatbuffers_smoke_bindings
    DEPENDS
        "${FLATBUFFERS_SMOKE_HEADER}"
        "${FLATBUFFERS_SMOKE_PYTHON_MODULE}"
)

add_executable(
    flatbuffers_smoke_producer
    flatbuffers_smoke_producer.cpp
    "${FLATBUFFERS_SMOKE_HEADER}"
)
add_dependencies(flatbuffers_smoke_producer flatbuffers_smoke_bindings)
target_include_directories(
    flatbuffers_smoke_producer
    PRIVATE "${FLATBUFFERS_SMOKE_CPP_ROOT}"
)
target_include_directories(
    flatbuffers_smoke_producer
    SYSTEM PRIVATE "${ELYSIUM_FLATBUFFERS_ROOT}/include"
)
elysium_native_target(flatbuffers_smoke_producer)

add_library(retail_hook_backends STATIC hook_backend.cpp)
target_include_directories(retail_hook_backends PUBLIC .)
elysium_native_target(retail_hook_backends)

# Retain the existing attach-time tools in the common native build while the
# launcher-controlled path is brought up beside them.
add_library(retail_live_pose_hook SHARED ../live_pose_hook.cpp)
set_target_properties(retail_live_pose_hook PROPERTIES OUTPUT_NAME live_pose_hook)
target_include_directories(retail_live_pose_hook PRIVATE ..)
target_include_directories(retail_live_pose_hook PRIVATE .)
target_link_libraries(retail_live_pose_hook PRIVATE retail_hook_backends)
elysium_native_target(retail_live_pose_hook)

add_executable(retail_live_pose_injector ../live_pose_injector.cpp)
set_target_properties(
    retail_live_pose_injector
    PROPERTIES OUTPUT_NAME live_pose_injector
)
elysium_native_target(retail_live_pose_injector)

function(add_synthetic_module target output_name identity offset)
    add_library(
        ${target}
        SHARED
        synthetic_module.cpp
        synthetic_module.def
    )
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${output_name}")
    target_compile_definitions(
        ${target}
        PRIVATE
            ELYSIUM_SYNTHETIC_IDENTITY=L"${identity}"
            ELYSIUM_SYNTHETIC_OFFSET=${offset}
    )
    elysium_native_target(${target})
endfunction()

add_synthetic_module(synthetic_client client client.dll 1000)
add_synthetic_module(synthetic_engine engine engine.dll 2000)
add_synthetic_module(
    synthetic_studiorender
    StudioRender
    StudioRender.dll
    3000
)

add_executable(
    synthetic_retail
    synthetic_retail.cpp
    synthetic_retail.def
)
set_target_properties(synthetic_retail PROPERTIES OUTPUT_NAME vampire)
elysium_native_target(synthetic_retail)
target_link_options(
    synthetic_retail
    PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/synthetic_retail.def"
)
add_dependencies(
    synthetic_retail
    synthetic_client
    synthetic_engine
    synthetic_studiorender
)

add_executable(
    retail_launcher
    retail_launcher.cpp
    retail_supervision.cpp
)
elysium_native_target(retail_launcher)
target_include_directories(retail_launcher PRIVATE .)

add_library(retail_probe_host SHARED retail_probe_host.cpp)
set_target_properties(retail_probe_host PROPERTIES OUTPUT_NAME retail_probe_host)
target_include_directories(retail_probe_host PRIVATE .)
target_sources(
    retail_probe_host
    PRIVATE
        binary_profile_registry.cpp
        module_observer.cpp
        probe_validation.cpp
)
target_link_libraries(retail_probe_host PRIVATE bcrypt)
elysium_native_target(retail_probe_host)

add_executable(synthetic_collector synthetic_collector.cpp)
elysium_native_target(synthetic_collector)

add_executable(
    module_observer_test
    module_observer_test.cpp
    module_observer.cpp
)
target_include_directories(module_observer_test PRIVATE .)
target_link_libraries(module_observer_test PRIVATE bcrypt)
elysium_native_target(module_observer_test)
add_dependencies(module_observer_test synthetic_client)

add_executable(
    binary_profile_registry_test
    binary_profile_registry_test.cpp
    binary_profile_registry.cpp
)
target_include_directories(binary_profile_registry_test PRIVATE .)
elysium_native_target(binary_profile_registry_test)

add_executable(
    probe_validation_test
    probe_validation_test.cpp
    probe_validation.cpp
)
target_include_directories(probe_validation_test PRIVATE .)
elysium_native_target(probe_validation_test)

add_executable(hook_backend_test hook_backend_test.cpp)
target_link_libraries(hook_backend_test PRIVATE retail_hook_backends)
elysium_native_target(hook_backend_test)

enable_testing()
add_test(
    NAME flatbuffers_generated_drift
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../contracts/generate_flatbuffers_smoke.py
        --flatc $<TARGET_FILE:flatc>
        --check
)
set_tests_properties(
    flatbuffers_generated_drift
    PROPERTIES
        PASS_REGULAR_EXPRESSION "flatbuffers-smoke-generated-v1 state=current"
        TIMEOUT 30
)

add_test(
    NAME flatbuffers_cross_language_smoke
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../flatbuffers_smoke_reader.py
        --producer $<TARGET_FILE:flatbuffers_smoke_producer>
        --generated-python "${FLATBUFFERS_SMOKE_PYTHON_ROOT}"
)
set_tests_properties(
    flatbuffers_cross_language_smoke
    PROPERTIES
        PASS_REGULAR_EXPRESSION "flatbuffers-smoke-python-v1 state=complete"
        TIMEOUT 30
)

add_test(
    NAME binary_profile_contract_check
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../contracts/generate_binary_profiles.py
        --check
)

add_test(
    NAME binary_profile_registry_exact_match
    COMMAND $<TARGET_FILE:binary_profile_registry_test>
)
set_tests_properties(
    binary_profile_registry_exact_match
    PROPERTIES
        PASS_REGULAR_EXPRESSION
            "binary-profile-registry-test-v1 state=complete"
        TIMEOUT 15
)

add_test(
    NAME probe_validation_fail_closed
    COMMAND $<TARGET_FILE:probe_validation_test>
)

add_test(
    NAME hook_backends_instruction_aware
    COMMAND $<TARGET_FILE:hook_backend_test>
)
set_tests_properties(
    hook_backends_instruction_aware
    PROPERTIES
        PASS_REGULAR_EXPRESSION
            "hook-backend-test-v1 state=complete.*relative_relocations=1"
        TIMEOUT 15
)
set_tests_properties(
    probe_validation_fail_closed
    PROPERTIES
        PASS_REGULAR_EXPRESSION
            "probe-validation-test-v1 state=complete.*rejected_installs=0"
        TIMEOUT 15
)

add_test(
    NAME module_observer_bootstrap_load_unload
    COMMAND
        $<TARGET_FILE:module_observer_test>
        $<TARGET_FILE:synthetic_client>
)
set_tests_properties(
    module_observer_bootstrap_load_unload
    PROPERTIES
        PASS_REGULAR_EXPRESSION "module-observer-test-v1 state=complete"
        TIMEOUT 30
)

add_test(
    NAME synthetic_retail_lifecycle
    COMMAND
        $<TARGET_FILE:synthetic_retail>
        --initial-delay-ms 1
        --module-delay-ms 1
        --lifetime-ms 1
)
set_tests_properties(
    synthetic_retail_lifecycle
    PROPERTIES
        PASS_REGULAR_EXPRESSION "event=shutdown_complete modules=3"
        TIMEOUT 15
)

add_test(
    NAME synthetic_suspended_launch
    COMMAND
        $<TARGET_FILE:retail_launcher>
        --executable $<TARGET_FILE:synthetic_retail>
        --working-directory $<TARGET_FILE_DIR:synthetic_retail>
        --distribution synthetic-test
        --startup-profile unofficial-patch
        --environment ELYSIUM_CAP1_OVERRIDE=override-value
        --verify-suspended-ms 25
        --probe-host $<TARGET_FILE:retail_probe_host>
        --resume-synthetic
        --
        --initial-delay-ms 0
        --module-delay-ms 1
        --lifetime-ms 1
        --expect-environment ELYSIUM_CAP1_PARENT=parent-value
        --expect-environment ELYSIUM_CAP1_OVERRIDE=override-value
        --expect-working-directory $<TARGET_FILE_DIR:synthetic_retail>
        --expect-mod-directory Unofficial_Patch
        --command-token "argument with spaces"
)
set_tests_properties(
    synthetic_suspended_launch
    PROPERTIES
        ENVIRONMENT "ELYSIUM_CAP1_PARENT=parent-value"
        PASS_REGULAR_EXPRESSION "event=launch_complete.*exit_code=0"
        TIMEOUT 15
)

add_test(
    NAME synthetic_bootstrap_termination
    COMMAND
        $<TARGET_FILE:retail_launcher>
        --executable $<TARGET_FILE:synthetic_retail>
        --working-directory $<TARGET_FILE_DIR:synthetic_retail>
        --distribution synthetic-test
        --startup-profile direct
        --verify-suspended-ms 25
        --probe-host $<TARGET_FILE:retail_probe_host>
        --inject-and-terminate
)
set_tests_properties(
    synthetic_bootstrap_termination
    PROPERTIES
        PASS_REGULAR_EXPRESSION "event=bootstrap_verified"
        TIMEOUT 15
)

add_test(
    NAME synthetic_direct_save_startup
    COMMAND
        $<TARGET_FILE:retail_launcher>
        --executable $<TARGET_FILE:synthetic_retail>
        --working-directory $<TARGET_FILE_DIR:synthetic_retail>
        --distribution synthetic-test
        --startup-profile unofficial-patch-save
        --verify-suspended-ms 25
        --probe-host $<TARGET_FILE:retail_probe_host>
        --resume-synthetic
        --
        --initial-delay-ms 0
        --module-delay-ms 1
        --lifetime-ms 1
        --expect-mod-directory Unofficial_Patch
        --expect-exec-config elysium_load.cfg
)
set_tests_properties(
    synthetic_direct_save_startup
    PROPERTIES
        PASS_REGULAR_EXPRESSION
            "event=launch_complete.*exit_code=0"
        TIMEOUT 15
)

add_test(
    NAME synthetic_suspended_termination
    COMMAND
        $<TARGET_FILE:retail_launcher>
        --executable $<TARGET_FILE:synthetic_retail>
        --working-directory $<TARGET_FILE_DIR:synthetic_retail>
        --distribution synthetic-test
        --startup-profile direct
        --verify-suspended-ms 25
        --terminate-after-verification
)
set_tests_properties(
    synthetic_suspended_termination
    PROPERTIES
        PASS_REGULAR_EXPRESSION "event=verification_complete"
        TIMEOUT 15
)

add_test(
    NAME synthetic_supervision_normal_exit
    COMMAND
        ${CMAKE_COMMAND}
        -DLAUNCHER=$<TARGET_FILE:retail_launcher>
        -DTARGET=$<TARGET_FILE:synthetic_retail>
        -DBIN_DIR=$<TARGET_FILE_DIR:synthetic_retail>
        -DPROBE=$<TARGET_FILE:retail_probe_host>
        -DCOLLECTOR=$<TARGET_FILE:synthetic_collector>
        -DREPORT=${CMAKE_BINARY_DIR}/supervision-normal.txt
        -DSCENARIO=normal
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_supervision.cmake
)

add_test(
    NAME synthetic_attach_fallback
    COMMAND
        powershell
        -NoProfile
        -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/verify_attach.ps1
        -Launcher $<TARGET_FILE:retail_launcher>
        -Target $<TARGET_FILE:synthetic_retail>
        -BinDir $<TARGET_FILE_DIR:synthetic_retail>
        -Probe $<TARGET_FILE:retail_probe_host>
)
set_tests_properties(
    synthetic_attach_fallback
    PROPERTIES TIMEOUT 15
)

add_test(
    NAME synthetic_supervision_timeout
    COMMAND
        ${CMAKE_COMMAND}
        -DLAUNCHER=$<TARGET_FILE:retail_launcher>
        -DTARGET=$<TARGET_FILE:synthetic_retail>
        -DBIN_DIR=$<TARGET_FILE_DIR:synthetic_retail>
        -DPROBE=$<TARGET_FILE:retail_probe_host>
        -DCOLLECTOR=$<TARGET_FILE:synthetic_collector>
        -DREPORT=${CMAKE_BINARY_DIR}/supervision-timeout.txt
        -DSCENARIO=timeout
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_supervision.cmake
)

add_test(
    NAME synthetic_supervision_crash
    COMMAND
        ${CMAKE_COMMAND}
        -DLAUNCHER=$<TARGET_FILE:retail_launcher>
        -DTARGET=$<TARGET_FILE:synthetic_retail>
        -DBIN_DIR=$<TARGET_FILE_DIR:synthetic_retail>
        -DPROBE=$<TARGET_FILE:retail_probe_host>
        -DCOLLECTOR=$<TARGET_FILE:synthetic_collector>
        -DREPORT=${CMAKE_BINARY_DIR}/supervision-crash.txt
        -DSCENARIO=crash
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_supervision.cmake
)

add_test(
    NAME synthetic_supervision_accepted_exit_one
    COMMAND
        ${CMAKE_COMMAND}
        -DLAUNCHER=$<TARGET_FILE:retail_launcher>
        -DTARGET=$<TARGET_FILE:synthetic_retail>
        -DBIN_DIR=$<TARGET_FILE_DIR:synthetic_retail>
        -DPROBE=$<TARGET_FILE:retail_probe_host>
        -DCOLLECTOR=$<TARGET_FILE:synthetic_collector>
        -DREPORT=${CMAKE_BINARY_DIR}/supervision-accepted-exit-one.txt
        -DSCENARIO=accepted-exit-one
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_supervision.cmake
)

add_test(
    NAME synthetic_supervision_collector_exit
    COMMAND
        ${CMAKE_COMMAND}
        -DLAUNCHER=$<TARGET_FILE:retail_launcher>
        -DTARGET=$<TARGET_FILE:synthetic_retail>
        -DBIN_DIR=$<TARGET_FILE_DIR:synthetic_retail>
        -DPROBE=$<TARGET_FILE:retail_probe_host>
        -DCOLLECTOR=$<TARGET_FILE:synthetic_collector>
        -DREPORT=${CMAKE_BINARY_DIR}/supervision-collector.txt
        -DSCENARIO=collector
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_supervision.cmake
)

add_test(
    NAME synthetic_lifecycle_soak_100
    COMMAND
        powershell
        -NoProfile
        -ExecutionPolicy Bypass
        -File ${CMAKE_CURRENT_SOURCE_DIR}/verify_lifecycle_soak.ps1
        -Launcher $<TARGET_FILE:retail_launcher>
        -Target $<TARGET_FILE:synthetic_retail>
        -BinDir $<TARGET_FILE_DIR:synthetic_retail>
        -Probe $<TARGET_FILE:retail_probe_host>
        -Collector $<TARGET_FILE:synthetic_collector>
        -OutputDir ${CMAKE_BINARY_DIR}/lifecycle-soak
        -Cycles 100
)

set_tests_properties(
    synthetic_supervision_normal_exit
    synthetic_supervision_timeout
    synthetic_supervision_crash
    synthetic_supervision_accepted_exit_one
    synthetic_supervision_collector_exit
    PROPERTIES TIMEOUT 15
)
set_tests_properties(
    synthetic_lifecycle_soak_100
    PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 180
)

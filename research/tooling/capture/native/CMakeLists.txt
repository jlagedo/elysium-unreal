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

# Retain the existing attach-time tools in the common native build while the
# launcher-controlled path is brought up beside them.
add_library(retail_live_pose_hook SHARED ../live_pose_hook.cpp)
set_target_properties(retail_live_pose_hook PROPERTIES OUTPUT_NAME live_pose_hook)
target_include_directories(retail_live_pose_hook PRIVATE ..)
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

enable_testing()
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

cmake_minimum_required(VERSION 3.16)
project(Chipgoodizer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHIPGOODIZER_BUILD_CAPI "Build the C ABI wrapper (chipgoodizer_capi)" ON)
option(CHIPGOODIZER_BUILD_IO "Build the WAV file I/O helper (chipgoodizer_io)" ON)
option(CHIPGOODIZER_BUILD_CLI "Build the offline demo renderer (CLI)" ON)
option(CHIPGOODIZER_INSTALL "Generate install and CMake package rules" ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CHIPGOODIZER_TESTS_DEFAULT ON)
else()
    set(CHIPGOODIZER_TESTS_DEFAULT OFF)
endif()

option(CHIPGOODIZER_BUILD_TESTS "Build the engine test suite" ${CHIPGOODIZER_TESTS_DEFAULT})

include(GNUInstallDirs)

if(CHIPGOODIZER_BUILD_CLI AND NOT CHIPGOODIZER_BUILD_IO)
    message(FATAL_ERROR "CHIPGOODIZER_BUILD_CLI requires CHIPGOODIZER_BUILD_IO")
endif()

function(chipgoodizer_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# -----------------------------------------------------------------------------
# chipgoodizer_dsp: the effect itself. No I/O, no host framework, no globals.
# This is the target host applications link against.
# -----------------------------------------------------------------------------
add_library(chipgoodizer_dsp STATIC
    src/dsp/Chipgoodizer.cpp
)

add_library(chipgoodizer::dsp ALIAS chipgoodizer_dsp)

target_compile_features(chipgoodizer_dsp PUBLIC cxx_std_17)

target_include_directories(chipgoodizer_dsp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(chipgoodizer_dsp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    EXPORT_NAME dsp
)

chipgoodizer_set_warnings(chipgoodizer_dsp)

# -----------------------------------------------------------------------------
# chipgoodizer_capi: C ABI (include/chipgoodizer/chipgoodizer_c.h) for hosts that
# cannot take a C++ dependency, e.g. Silicon Player's JNI/desktop layer.
# -----------------------------------------------------------------------------
if(CHIPGOODIZER_BUILD_CAPI)
    add_library(chipgoodizer_capi STATIC
        src/capi/chipgoodizer_c.cpp
    )

    add_library(chipgoodizer::capi ALIAS chipgoodizer_capi)

    target_compile_features(chipgoodizer_capi PUBLIC cxx_std_17)

    target_link_libraries(chipgoodizer_capi PUBLIC chipgoodizer_dsp)
    set_target_properties(chipgoodizer_capi PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME capi
    )

    chipgoodizer_set_warnings(chipgoodizer_capi)
endif()

# -----------------------------------------------------------------------------
# chipgoodizer_io: minimal RIFF/WAVE reader + 24-bit writer used by the CLI and
# by offline verification. Not needed inside an audio host.
# -----------------------------------------------------------------------------
if(CHIPGOODIZER_BUILD_IO)
    add_library(chipgoodizer_io STATIC
        src/io/WavFile.cpp
    )

    add_library(chipgoodizer::io ALIAS chipgoodizer_io)

    target_compile_features(chipgoodizer_io PUBLIC cxx_std_17)

    target_include_directories(chipgoodizer_io PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    set_target_properties(chipgoodizer_io PROPERTIES EXPORT_NAME io)
    chipgoodizer_set_warnings(chipgoodizer_io)
endif()

# -----------------------------------------------------------------------------
# chipgoodizer: offline demo/audition CLI.
# -----------------------------------------------------------------------------
if(CHIPGOODIZER_BUILD_CLI)
    add_executable(chipgoodizer_cli
        src/cli/main.cpp
    )

    target_link_libraries(chipgoodizer_cli PRIVATE chipgoodizer_dsp)

    if(CHIPGOODIZER_BUILD_IO)
        target_link_libraries(chipgoodizer_cli PRIVATE chipgoodizer_io)
    endif()

    set_target_properties(chipgoodizer_cli PROPERTIES OUTPUT_NAME chipgoodizer)
    set_target_properties(chipgoodizer_cli PROPERTIES EXPORT_NAME cli)
    chipgoodizer_set_warnings(chipgoodizer_cli)
endif()

# -----------------------------------------------------------------------------
# Tests: plain executable, no framework, run with ctest.
# -----------------------------------------------------------------------------
if(CHIPGOODIZER_BUILD_TESTS)
    enable_testing()

    add_executable(chipgoodizer_tests
        tests/chipgoodizer_tests.cpp
    )

    target_link_libraries(chipgoodizer_tests PRIVATE chipgoodizer_dsp)

    if(CHIPGOODIZER_BUILD_CAPI)
        target_link_libraries(chipgoodizer_tests PRIVATE chipgoodizer_capi)
        target_compile_definitions(chipgoodizer_tests PRIVATE CHIPGOODIZER_HAVE_CAPI)
    endif()

    chipgoodizer_set_warnings(chipgoodizer_tests)
    add_test(NAME chipgoodizer_tests COMMAND chipgoodizer_tests)
endif()

# -----------------------------------------------------------------------------
# Install / export
# -----------------------------------------------------------------------------
if(CHIPGOODIZER_INSTALL)
    set(chipgoodizer_install_targets chipgoodizer_dsp)

    if(CHIPGOODIZER_BUILD_CAPI)
        list(APPEND chipgoodizer_install_targets chipgoodizer_capi)
    endif()

    if(CHIPGOODIZER_BUILD_IO)
        list(APPEND chipgoodizer_install_targets chipgoodizer_io)
    endif()

    install(TARGETS ${chipgoodizer_install_targets}
        EXPORT chipgoodizerTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT chipgoodizerTargets
        FILE chipgoodizerTargets.cmake
        NAMESPACE chipgoodizer::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chipgoodizer
    )

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        cmake/chipgoodizerConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/chipgoodizerConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chipgoodizer
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/chipgoodizerConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/chipgoodizerConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/chipgoodizerConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/chipgoodizer
    )
endif()

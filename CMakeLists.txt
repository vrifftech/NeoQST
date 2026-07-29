cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime library" FORCE)

project(NeoQST VERSION 1.0.0 DESCRIPTION "Jade Empire QST/QST2 quest editor" LANGUAGES CXX)

set(NEOSHARED_ROOT "" CACHE PATH
    "Path to the NeoShared source repository; defaults to ../neoshared")
if(NOT NEOSHARED_ROOT AND DEFINED ENV{NEOSHARED_ROOT}
   AND NOT "$ENV{NEOSHARED_ROOT}" STREQUAL "")
    set(NEOSHARED_ROOT "$ENV{NEOSHARED_ROOT}" CACHE PATH
        "Path to the NeoShared source repository" FORCE)
endif()
if(NOT NEOSHARED_ROOT)
    get_filename_component(NEOSHARED_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/../neoshared" ABSOLUTE)
else()
    get_filename_component(NEOSHARED_ROOT "${NEOSHARED_ROOT}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
endif()
set(NEOSHARED_ROOT "${NEOSHARED_ROOT}" CACHE PATH
    "Path to the NeoShared source repository" FORCE)

if(NOT EXISTS "${NEOSHARED_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "NeoShared was not found at '${NEOSHARED_ROOT}'. "
        "Clone NeoShared beside this repository, or configure with "
        "-DNEOSHARED_ROOT=/path/to/NeoShared.")
endif()

if(MSVC)
    foreach(_neo_runtime_flag_var IN ITEMS
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        if(DEFINED ${_neo_runtime_flag_var})
            string(REGEX REPLACE "(^|[ /-])MDd?($|[ ]|/)" "\\1MT\\2" ${_neo_runtime_flag_var} "${${_neo_runtime_flag_var}}")
        endif()
    endforeach()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(neo_force_static_msvc_runtime target_name)
    if(MSVC AND TARGET ${target_name})
        set_property(TARGET ${target_name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:/MTd>
            $<$<NOT:$<CONFIG:Debug>>:/MT>
        )
    endif()
endfunction()

function(neo_force_static_msvc_runtime_for_directory_targets)
    if(MSVC)
        get_property(_neo_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_neo_target IN LISTS _neo_targets)
            get_target_property(_neo_type ${_neo_target} TYPE)
            if(NOT _neo_type STREQUAL "INTERFACE_LIBRARY" AND NOT _neo_type STREQUAL "UTILITY")
                neo_force_static_msvc_runtime(${_neo_target})
            endif()
        endforeach()
    endif()
endfunction()

function(neoqst_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

include(GNUInstallDirs)

message(STATUS "Using NeoShared: ${NEOSHARED_ROOT}")
include("${NEOSHARED_ROOT}/cmake/NeoMinimalRelease.cmake")
if(NOT TARGET neoshared::gff)
    add_subdirectory("${NEOSHARED_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/neoshared" EXCLUDE_FROM_ALL)
endif()

option(NEOQST_BUILD_CLI "Build the NeoQST command-line utility" ON)
option(NEOQST_BUILD_WX_GUI "Build the wxWidgets desktop GUI when wxWidgets is available" ON)
option(NEOQST_REQUIRE_WX_GUI "Fail configure if wxWidgets is unavailable while NEOQST_BUILD_WX_GUI is ON" OFF)

add_library(neoqst_core
    src/QstOperations.cpp
)
target_include_directories(neoqst_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(neoqst_core PUBLIC neoshared::gff)
target_compile_features(neoqst_core PUBLIC cxx_std_17)
neoqst_enable_warnings(neoqst_core)

if(NEOQST_BUILD_CLI)
    add_executable(neoqst-cli src/CliMain.cpp)
    target_link_libraries(neoqst-cli PRIVATE
        neoqst_core
        neoshared::tlk
        neoshared::tabular
    )
    neoqst_enable_warnings(neoqst-cli)
    install(TARGETS neoqst-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()


function(neoqst_link_wx target_name)
    target_compile_definitions(${target_name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    set(_wx_targets)
    foreach(_wx_target IN ITEMS wx::core wx::base wx::adv wxWidgets::wxWidgets)
        if(TARGET ${_wx_target})
            list(APPEND _wx_targets ${_wx_target})
        endif()
    endforeach()

    if(_wx_targets)
        target_link_libraries(${target_name} PRIVATE ${_wx_targets})
    else()
        if(wxWidgets_USE_FILE)
            include(${wxWidgets_USE_FILE})
        endif()
        target_include_directories(${target_name} SYSTEM PRIVATE ${wxWidgets_INCLUDE_DIRS})
        target_compile_definitions(${target_name} PRIVATE ${wxWidgets_DEFINITIONS})
        target_link_libraries(${target_name} PRIVATE ${wxWidgets_LIBRARIES})
    endif()

    neo_force_static_msvc_runtime(${target_name})
endfunction()

if(NEOQST_BUILD_WX_GUI)
    set(NEOQST_WX_COMPONENTS core base adv)
    set(_neo_prefer_wx_config_package OFF)
    if(WIN32 OR DEFINED VCPKG_TARGET_TRIPLET OR CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
        set(_neo_prefer_wx_config_package ON)
    endif()

    if(_neo_prefer_wx_config_package)
        find_package(wxWidgets CONFIG QUIET COMPONENTS ${NEOQST_WX_COMPONENTS})
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets CONFIG QUIET)
        endif()
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets QUIET MODULE COMPONENTS ${NEOQST_WX_COMPONENTS})
        endif()
    else()
        find_package(wxWidgets QUIET MODULE COMPONENTS ${NEOQST_WX_COMPONENTS})
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets CONFIG QUIET COMPONENTS ${NEOQST_WX_COMPONENTS})
        endif()
        if(NOT wxWidgets_FOUND AND NOT TARGET wx::core AND NOT TARGET wxWidgets::wxWidgets)
            find_package(wxWidgets CONFIG QUIET)
        endif()
    endif()

    if(wxWidgets_FOUND OR TARGET wx::core OR TARGET wxWidgets::wxWidgets)
        set(NEOQST_WX_SOURCES src/wx/main.cpp)
        if(WIN32)
            list(APPEND NEOQST_WX_SOURCES resources/neoqst.rc)
        elseif(APPLE)
            list(APPEND NEOQST_WX_SOURCES resources/neoqst.icns)
            set_source_files_properties(resources/neoqst.icns PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources")
        endif()

        if(WIN32)
            add_executable(NeoQST WIN32 ${NEOQST_WX_SOURCES})
        elseif(APPLE)
            add_executable(NeoQST MACOSX_BUNDLE ${NEOQST_WX_SOURCES})
        else()
            add_executable(NeoQST ${NEOQST_WX_SOURCES})
        endif()
        target_link_libraries(NeoQST PRIVATE
            neoqst_core
            neoshared::tlk
            neoshared::wx
        )
        target_include_directories(NeoQST PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources)
        target_compile_features(NeoQST PRIVATE cxx_std_17)
        neoqst_link_wx(NeoQST)
        neoqst_enable_warnings(NeoQST)
        if(APPLE)
            set_target_properties(NeoQST PROPERTIES
                MACOSX_BUNDLE_ICON_FILE "neoqst.icns")
        endif()
        install(TARGETS NeoQST
            BUNDLE DESTINATION .
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )


        if(UNIX AND NOT APPLE)
            foreach(NEO_ICON_SIZE 16 24 32 48 64 128 256 512)
                install(FILES resources/icons/${NEO_ICON_SIZE}x${NEO_ICON_SIZE}/neoqst.png
                    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${NEO_ICON_SIZE}x${NEO_ICON_SIZE}/apps
                )
            endforeach()
            install(FILES resources/neoqst.svg
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
            )
            configure_file(resources/neoqst.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/neoqst.desktop @ONLY)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/neoqst.desktop
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
            )
        endif()
    elseif(NEOQST_REQUIRE_WX_GUI)
        message(FATAL_ERROR
            "wxWidgets was not found, but NEOQST_REQUIRE_WX_GUI is ON. "
            "On Windows, install the pinned wxWidgets 3.3.3 overlay from NeoShared and configure with the vcpkg toolchain. "
            "On Debian or Ubuntu, install libwxgtk3.2-dev.")
    else()
        message(WARNING "wxWidgets was not found. Core and CLI targets will still build; install wxWidgets to build NeoQST.")
    endif()
endif()

install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/NeoQST)

neo_force_static_msvc_runtime_for_directory_targets()
neo_apply_minimal_release_to_directory_targets()

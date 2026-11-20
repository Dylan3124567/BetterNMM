cmake_minimum_required(VERSION 3.25) # Required for features like `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`

project(Launcher LANGUAGES C CXX)

string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_BUILD_DIR}" IS_IN_SOURCE_BUILD)
if(IS_IN_SOURCE_BUILD)
    message(FATAL_ERROR "You are building the Launcher in-source. Please separate the build tree from the source tree.")
endif()

##################################### Set CMake options #####################################
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOGEN_ORIGIN_DEPENDS OFF)
set(CMAKE_GLOBAL_AUTOGEN_TARGET ON)
set(CMAKE_GLOBAL_AUTORCC_TARGET ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/")

# Output all executables and shared libs in the main build folder, not in subfolders.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

######## Set compiler flags ########
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_C_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 11)
include(GenerateExportHeader)

add_compile_definitions($<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG>)
add_compile_definitions(QT_WARN_DEPRECATED_UP_TO=0x060800)
add_compile_definitions(QT_DISABLE_DEPRECATED_UP_TO=0x060800)
add_compile_definitions(QT_NO_CONTEXTLESS_CONNECT)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(
        # /GS Adds buffer security checks, default on but included anyway to mirror gcc's fstack-protector flag
        "$<$<COMPILE_LANGUAGE:C,CXX>:/GS>"

        # /Gw helps reduce binary size
        # /Gy allows the compiler to package individual functions
        # /guard:cf enables control flow guard
        "$<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<COMPILE_LANGUAGE:C,CXX>>:/Gw;/Gy;/guard:cf>"
    )

    add_link_options(
        # /LTCG allows for linking wholy optimizated programs
        $<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<COMPILE_LANGUAGE:C,CXX>>:/LTCG>
    )

    # /GL enables whole program optimizations
    # NOTE: With Clang, this is implemented as regular LTO and only used during linking
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options("$<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<COMPILE_LANGUAGE:C,CXX>>:/GL>")
    endif()

    # See https://github.com/ccache/ccache/issues/1040
    # TODO(@getchoo): Is sccache affected by this? Would be nice to use `ProgramDatabase`....
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

    # Ensure our runtime library matches our vcpkg dependencies
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

    # TODO(@getchoo): Do we actually need this???
    if(CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL")
        set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release "")
        set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release "")
    endif()
else()
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-fstack-protector-strong;--param=ssp-buffer-size=4>")

    # Avoid re-defining _FORTIFY_SOURCE, as it can cause redefinition errors in setups that use it by default (i.e., package builds)
    if(NOT (CMAKE_C_FLAGS MATCHES "-D_FORTIFY_SOURCE" OR CMAKE_CXX_FLAGS MATCHES "-D_FORTIFY_SOURCE"))
        # NOTE: _FORTIFY_SOURCE requires optimizations in most newer versions of glibc
        add_compile_options("$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release,RelWithDebInfo>>:-D_FORTIFY_SOURCE=2>")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        add_link_options("$<$<COMPILE_LANGUAGE:C,CXX>:-Wl,--stack,8388608>")

        # -ffunction-sections and -fdata-sections help reduce binary size
        # TODO: Look into -gc-sections to further reduce binary size
        add_compile_options("$<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<COMPILE_LANGUAGE:C,CXX>>:-ffunction-sections;-fdata-sections>")
        # -mguard=cf enables Control Flow Guard; MinGW GCC rejects the flag, only Clang has it
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            add_compile_options("$<$<AND:$<CONFIG:Release,RelWithDebInfo>,$<COMPILE_LANGUAGE:C,CXX>>:-mguard=cf>")
        endif()
    endif()
endif()

# Export compile commands for debug builds if we can (useful in LSPs like clangd)
# https://cmake.org/cmake/help/v3.31/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html
if(CMAKE_GENERATOR STREQUAL "Unix Makefiles" OR CMAKE_GENERATOR MATCHES "^Ninja")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

option(USE_CLANG_TIDY "Enable the use of clang-tidy during compilation" OFF)

if(USE_CLANG_TIDY)
    find_program(CLANG_TIDY clang-tidy OPTIONAL)
    if(CLANG_TIDY)
        message(STATUS "Using clang-tidy during compilation")
        set(CLANG_TIDY_COMMAND "${CLANG_TIDY}" "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy")
        set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_COMMAND})
    else()
        message(WARNING "Unable to find `clang-tidy`. Not using during compilation")
    endif()
endif()

option(DEBUG_ADDRESS_SANITIZER "Enable Address Sanitizer in Debug builds" OFF)

# If this is a Debug build turn on address sanitiser
if (DEBUG_ADDRESS_SANITIZER)
    message(STATUS "Address Sanitizer enabled for Debug builds, Turn it off with -DDEBUG_ADDRESS_SANITIZER=off")

    set(USE_ASAN_COMPILE_OPTIONS $<AND:$<CONFIG:Debug,RelWithDebInfo>,$<COMPILE_LANGUAGE:C,CXX>>)
    if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        message(STATUS "Using Address Sanitizer compile options for MSVC frontend")
        add_compile_options(
                $<${USE_ASAN_COMPILE_OPTIONS}:/fsanitize=address>
                $<${USE_ASAN_COMPILE_OPTIONS}:/Oy->
        )
        # NOTE: /INCREMENTAL emits warnings when used on asan'd objects
        add_link_options($<${USE_ASAN_COMPILE_OPTIONS}:/INCREMENTAL:NO>)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
        message(STATUS "Using Address Sanitizer compile options for GCC/Clang")
        add_compile_options(
                $<${USE_ASAN_COMPILE_OPTIONS}:-fsanitize=address,undefined>
                $<${USE_ASAN_COMPILE_OPTIONS}:-fno-omit-frame-pointer>
                $<${USE_ASAN_COMPILE_OPTIONS}:-fno-sanitize-recover=null>
        )
        add_link_options($<${USE_ASAN_COMPILE_OPTIONS}:-fsanitize=address,undefined>)
    else()
        message(STATUS "Address Sanitizer not available on compiler ${CMAKE_CXX_COMPILER_ID}")
    endif()
endif()


option(ENABLE_LTO "Enable Link Time Optimization" off)

if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        if(CMAKE_BUILD_TYPE)
            if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
                message(STATUS "IPO / LTO enabled")
            else()
                message(STATUS "Not enabling IPO / LTO on debug builds")
            endif()
        else()
            message(STATUS "IPO / LTO will only be enabled for release builds")
        endif()
    else()
        message(STATUS "IPO / LTO not supported: <${ipo_error}>")
    endif()
endif()

option(BUILD_TESTING "Build the testing tree." ON)

find_package(ECM NO_MODULE REQUIRED)
set(CMAKE_MODULE_PATH "${ECM_MODULE_PATH};${CMAKE_MODULE_PATH}")

include(CTest)

##################################### Set Application options #####################################

######## Set URLs ########
set(Launcher_HELP_URL "" CACHE STRING "URL (with arg %1 to be substituted with page-id) that gets opened when the user requests help in a dialog window")
set(Launcher_BUG_TRACKER_URL "" CACHE STRING "URL for the bug tracker.")
set(Launcher_NEXUS_API_URL "https://api.nexusmods.com/v1/" CACHE STRING "Nexus Mods REST API base URL")
set(Launcher_NEXUS_GRAPHQL_URL "https://api.nexusmods.com/v2/graphql" CACHE STRING "Nexus Mods GraphQL API URL")
set(Launcher_NEXUS_SITE_URL "https://www.nexusmods.com/" CACHE STRING "Nexus Mods website")
set(Launcher_RELEASES_API_URL "https://api.github.com/repos/Dylan3124567/BetterNMM/releases/latest" CACHE STRING "GitHub API URL of the newest release, for updating; empty turns updating off")

######## Set version numbers ########
set(Launcher_VERSION_MAJOR 1)
set(Launcher_VERSION_MINOR 1)
set(Launcher_VERSION_PATCH 0)

set(Launcher_VERSION_NAME "${Launcher_VERSION_MAJOR}.${Launcher_VERSION_MINOR}.${Launcher_VERSION_PATCH}")
set(Launcher_VERSION_NAME4 "${Launcher_VERSION_MAJOR}.${Launcher_VERSION_MINOR}.${Launcher_VERSION_PATCH}.0")
set(Launcher_VERSION_NAME4_COMMA "${Launcher_VERSION_MAJOR},${Launcher_VERSION_MINOR},${Launcher_VERSION_PATCH},0")

# Build platform.
set(Launcher_BUILD_PLATFORM "${CMAKE_SYSTEM_NAME}" CACHE STRING "A short string identifying the platform that this build was built for. Only used to display in the about dialog.")

# Builds
set(Launcher_QT_VERSION_MAJOR "6" CACHE STRING "Major Qt version to build against")

option(Launcher_USE_PCH "Use precompiled headers where possible" ON)

option(Launcher_WARNING_AS_ERROR "Treat compiler/linker warnings as errors (-Werror)")

if(Launcher_WARNING_AS_ERROR)
    message(STATUS "Treating all warnings as errors for launcher targets")
endif()

set(Launcher_COMPILER_NAME ${CMAKE_CXX_COMPILER_ID})
set(Launcher_COMPILER_VERSION ${CMAKE_CXX_COMPILER_VERSION})
set(Launcher_COMPILER_TARGET_SYSTEM ${CMAKE_SYSTEM_NAME})
set(Launcher_COMPILER_TARGET_SYSTEM_VERSION ${CMAKE_SYSTEM_VERSION})
set(Launcher_COMPILER_TARGET_PROCESSOR ${CMAKE_SYSTEM_PROCESSOR})

#### Check the current Git commit and branch
# a checkout without commits (or no checkout at all) builds as a plain release
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE Launcher_GIT_HEAD_RESULT OUTPUT_QUIET ERROR_QUIET)
endif()
if(GIT_FOUND AND Launcher_GIT_HEAD_RESULT EQUAL 0)
    include(GetGitRevisionDescription)
    git_get_exact_tag(Launcher_GIT_TAG)
    get_git_head_revision(Launcher_GIT_REFSPEC Launcher_GIT_COMMIT)
else()
    set(Launcher_GIT_TAG "GIT-NOTFOUND")
    set(Launcher_GIT_REFSPEC "")
    set(Launcher_GIT_COMMIT "")
endif()

message(STATUS "Git commit: ${Launcher_GIT_COMMIT}")
message(STATUS "Git tag: ${Launcher_GIT_TAG}")
message(STATUS "Git refspec: ${Launcher_GIT_REFSPEC}")

string(TIMESTAMP TODAY "%Y-%m-%d")
set(Launcher_BUILD_TIMESTAMP "${TODAY}")

################################ 3rd Party Libs ################################

# Find the required Qt parts
if(Launcher_QT_VERSION_MAJOR EQUAL 6)
    set(QT_VERSION_MAJOR 6)
    find_package(
        Qt6 6.8
        REQUIRED
        COMPONENTS
            Concurrent
            Core
            CoreTools
            Network
            Test
            Widgets
            Xml
        HINTS
            C:/Qt/*/*/lib/cmake
            /opt/qt/*/*/lib/cmake
            $ENV{MINGW_PREFIX}/opt/qt/*/*/lib/cmake
    )
else()
    message(FATAL_ERROR "Qt version ${Launcher_QT_VERSION_MAJOR} is not supported")
endif()

if(Launcher_QT_VERSION_MAJOR EQUAL 6)
    set(QT_PLUGINS_DIR  ${QT${QT_VERSION_MAJOR}_INSTALL_PREFIX}/${QT${QT_VERSION_MAJOR}_INSTALL_PLUGINS})
    set(QT_LIBS_DIR     ${QT${QT_VERSION_MAJOR}_INSTALL_PREFIX}/${QT${QT_VERSION_MAJOR}_INSTALL_LIBS})
    set(QT_LIBEXECS_DIR ${QT${QT_VERSION_MAJOR}_INSTALL_PREFIX}/${QT${QT_VERSION_MAJOR}_INSTALL_LIBEXECS})
endif()

# Find libarchive through CMake packages, mainly for vcpkg
find_package(LibArchive)
# CMake packages aren't available in most distributions of libarchive, so fallback to pkg-config
if(NOT LibArchive_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(libarchive REQUIRED IMPORTED_TARGET libarchive)
endif()



include(ECMQtDeclareLoggingCategory)

####################################### Program Info #######################################

set(Launcher_APP_BINARY_NAME "betternmm" CACHE STRING "Name of the Launcher binary")
add_subdirectory(program_info)

####################################### Install layout #######################################

if(NOT WIN32)
    message(FATAL_ERROR "BetterNMM manages Windows game installations and only builds for Windows")
endif()

# Install "portable.txt" if selected component is "portable"
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${Launcher_Portable_File}" DESTINATION "." COMPONENT portable EXCLUDE_FROM_ALL)

set(BINARY_DEST_DIR ".")
set(LIBRARY_DEST_DIR ".")
set(PLUGIN_DEST_DIR ".")
set(RESOURCES_DEST_DIR ".")

################################ Included Libs ################################

include(ExternalProject)
set_directory_properties(PROPERTIES EP_BASE External)

add_subdirectory(libraries/rainbow) # Qt extension for colors
add_subdirectory(libraries/LocalPeer) # fork of a library from Qt solutions

############################### Built Artifacts ###############################

add_subdirectory(buildconfig)

if(WIN32)
    add_subdirectory(browser) # Nexus Mods download window (Electron)
    add_subdirectory(installer) # setup and uninstaller (target "setup")
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
# NOTE: this must always be last to appease the CMake deity of quirky install command evaluation order.
add_subdirectory(launcher)

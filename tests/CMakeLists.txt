project(tests)
include(ECMMarkAsTest)
include(ECMMarkNonGuiExecutable)

set(TEST_SOURCES
    FileSystem_test.cpp
    Task_test.cpp
    INIFile_test.cpp
    Version_test.cpp
    CatPack_test.cpp
    Commandline_test.cpp
    GameFiles_test.cpp
    Steam_test.cpp
    ModInstall_test.cpp
    LoadOrder_test.cpp
    Nexus_test.cpp
    Isolation_test.cpp
    IniEditor_test.cpp
    CrashReport_test.cpp
    IniSettings_test.cpp
    AutoSort_test.cpp
    NexusDownloadQueue_test.cpp
    VortexImport_test.cpp
    FomodPatches_test.cpp
)

# NOTE(@getchoo): Very funny way of aliasing the Qt generated `main` test functions to respective filenames
# This is required for any test file passed to `create_test_sourcelist()`
foreach(src ${TEST_SOURCES})
    cmake_path(GET src STEM funcName)
    set_source_files_properties(${src} PROPERTIES COMPILE_DEFINITIONS main=${funcName})
    list(APPEND TEST_NAMES ${funcName})
endforeach()

# stands in for a game: reports the folders Windows gives it
add_executable(known_folders helpers/known_folders.cpp)
target_link_options(known_folders PRIVATE -municode)
target_link_libraries(known_folders PRIVATE shell32 ole32 uuid)
set_property(SOURCE Isolation_test.cpp APPEND PROPERTY COMPILE_DEFINITIONS "KNOWN_FOLDERS_EXE=\"$<TARGET_FILE:known_folders>\"")

# stands in for the Nexus Mods download window (betternmm-browser.exe)
add_executable(fake_browser helpers/fake_browser.cpp)
set_property(SOURCE NexusDownloadQueue_test.cpp APPEND PROPERTY COMPILE_DEFINITIONS "FAKE_BROWSER_EXE=\"$<TARGET_FILE:fake_browser>\"")
if(TARGET BetterNMM_browser)
    # the real download window, started against a local stand-in for Nexus Mods
    set_property(SOURCE NexusDownloadQueue_test.cpp APPEND PROPERTY COMPILE_DEFINITIONS "BROWSER_EXE=\"${CMAKE_BINARY_DIR}/browser/betternmm-browser.exe\"")
endif()

create_test_sourcelist(DRIVER_SOURCES LauncherTests.cpp ${TEST_SOURCES})
add_executable(LauncherTests ${DRIVER_SOURCES})
target_link_libraries(LauncherTests PRIVATE Launcher_logic Qt${QT_VERSION_MAJOR}::Test)
add_dependencies(LauncherTests known_folders fake_browser)
if(TARGET BetterNMM_browser)
    add_dependencies(LauncherTests BetterNMM_browser)
endif()

# Add back some stuff ECMAddTests would (https://api.kde.org/ecm/module/ECMAddTests.html)
ecm_mark_as_test(LauncherTests)
# NOTE(@getchoo): This is only for parity with our previous configurations, which all had no GUI
# In the future this might change....and I don't know how to handle it right now. Have fun!
ecm_mark_nongui_executable(LauncherTests)
# "The tests will be build with -DQT_FORCE_ASSERTS to enable assertions in the test executable even for release builds"
target_compile_definitions(LauncherTests PRIVATE QT_FORCE_ASSERTS)

foreach(test ${TEST_NAMES})
    string(REPLACE "_test" "" display_name ${test})
    add_test(NAME ${display_name} COMMAND LauncherTests ${test})
endforeach()

#[[
# TODO(@getchoo): Use this when we can use CMake 4.4 as our minimum version....one day
discover_tests(
    COMMAND LauncherTests
    DISCOVERY_ARGS -N
    DISCOVERY_MATCH "^(.+)$"
    TEST_NAME "\\1"
    TEST_ARGS "\\1"
)
]]#

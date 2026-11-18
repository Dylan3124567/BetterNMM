# BetterNMM's setup: a small Win32 program with no DLLs of its own, and the tool that packs the installed files onto it.
#   cmake --build <build> --target setup   ->   <build>/BetterNMM-Setup-<version>.exe

set(SETUP_ICON "${CMAKE_SOURCE_DIR}/program_info/betternmm.ico")
set(SETUP_LOGO "${CMAKE_CURRENT_SOURCE_DIR}/logo.png")
set(SETUP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/setup.manifest")
configure_file(setup.rc.in "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" @ONLY)

set(SETUP_COMMON Files.cpp Files.h Payload.cpp Payload.h)
set(SETUP_DEFINITIONS UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX)
# everything linked in: setup runs before anything of BetterNMM is on the PC
set(SETUP_LINK_OPTIONS -municode -static -static-libgcc -static-libstdc++ -Wl,--gc-sections $<$<CONFIG:Release,MinSizeRel>:-s>)

# the Vortex reader is the app's own, plain C++: the wizard lists the Vortex profiles BetterNMM can bring over
set(SETUP_VORTEX "${CMAKE_SOURCE_DIR}/launcher/vortex/VortexState.cpp" "${CMAKE_SOURCE_DIR}/launcher/vortex/VortexState.h")
add_executable(betternmm-setup-stub WIN32 Setup.cpp Install.cpp Install.h ${SETUP_COMMON} ${SETUP_VORTEX} "${CMAKE_CURRENT_BINARY_DIR}/setup.rc")
target_include_directories(betternmm-setup-stub PRIVATE "${CMAKE_SOURCE_DIR}/launcher/vortex")
target_compile_definitions(betternmm-setup-stub PRIVATE ${SETUP_DEFINITIONS})
target_compile_options(betternmm-setup-stub PRIVATE -municode $<$<CONFIG:Release,MinSizeRel>:-Os>)
target_link_options(betternmm-setup-stub PRIVATE ${SETUP_LINK_OPTIONS})
target_link_libraries(betternmm-setup-stub PRIVATE gdiplus cabinet comctl32 dwmapi uxtheme shlwapi shell32 ole32 oleaut32 uuid runtimeobject)
set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/setup.rc" APPEND PROPERTY OBJECT_DEPENDS "${SETUP_ICON}" "${SETUP_LOGO}" "${SETUP_MANIFEST}")

add_executable(bnmm-pack Pack.cpp ${SETUP_COMMON})
target_compile_definitions(bnmm-pack PRIVATE ${SETUP_DEFINITIONS})
target_compile_options(bnmm-pack PRIVATE -municode)
target_link_options(bnmm-pack PRIVATE ${SETUP_LINK_OPTIONS})
target_link_libraries(bnmm-pack PRIVATE cabinet)

set(SETUP_STAGE "${CMAKE_BINARY_DIR}/setup-stage")
set(SETUP_OUT "${CMAKE_BINARY_DIR}/BetterNMM-Setup-${Launcher_VERSION_NAME}.exe")
set(SETUP_DEPENDS betternmm-setup-stub bnmm-pack BetterNMM)
if(TARGET BetterNMM_browser)
    list(APPEND SETUP_DEPENDS BetterNMM_browser)
endif()
add_custom_target(setup
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${SETUP_STAGE}"
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --prefix "${SETUP_STAGE}"
    COMMAND "$<TARGET_FILE:bnmm-pack>" "$<TARGET_FILE:betternmm-setup-stub>" "${SETUP_STAGE}" "${SETUP_OUT}" "${Launcher_VERSION_NAME}"
    DEPENDS ${SETUP_DEPENDS}
    COMMENT "Packing BetterNMM-Setup-${Launcher_VERSION_NAME}.exe"
    VERBATIM)

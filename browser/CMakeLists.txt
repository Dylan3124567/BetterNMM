# BetterNMM's Nexus Mods download window: Electron (Chromium) running BetterNMM's own app from app/.
# The prebuilt Electron runtime and Electron's FFmpeg without patented codecs are downloaded once into
# ELECTRON_CACHE_DIR (shared by every build tree) and checked against their SHA-256. Only the files the window needs are
# staged in <build>/browser and installed into <prefix>/browser.

set(ELECTRON_VERSION "45.3.3")
set(ELECTRON_SHA256 "49dd2d3875944f08de03d34595cd30093c588b9edcabb41a0ac481028a3a5a8a")
set(ELECTRON_FFMPEG_SHA256 "9d1cf2881a823921da686b1796d3be7689c4d11b534a715dac954fb61fac2c52")
set(ELECTRON_CACHE_DIR "${CMAKE_SOURCE_DIR}/.cache/electron" CACHE PATH "Where the downloaded Electron runtime is kept")

# An archive already in the cache folder (downloaded by hand when this computer cannot download it) is used as it is
function(fetch_electron_archive name sha256 description)
    set(dir "${ELECTRON_CACHE_DIR}/${name}")
    if(EXISTS "${dir}/.complete")
        return()
    endif()
    set(archive "${ELECTRON_CACHE_DIR}/${name}.zip")
    set(url "https://github.com/electron/electron/releases/download/v${ELECTRON_VERSION}/${name}.zip")
    set(actual "")
    if(EXISTS "${archive}")
        file(SHA256 "${archive}" actual)
    endif()
    if(NOT actual STREQUAL sha256)
        message(STATUS "Downloading ${description}; this happens once")
        file(DOWNLOAD "${url}" "${archive}" STATUS download_status)
        list(GET download_status 1 download_error)
        if(EXISTS "${archive}")
            file(SHA256 "${archive}" actual)
        endif()
        if(NOT actual STREQUAL sha256)
            file(REMOVE "${archive}")
            message(FATAL_ERROR "Could not download ${url} (${download_error}).\n"
                                "Download it by hand into ${ELECTRON_CACHE_DIR} and configure again; its SHA-256 must be ${sha256}.")
        endif()
    endif()
    file(REMOVE_RECURSE "${dir}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dir}")
    file(REMOVE "${archive}")
    file(TOUCH "${dir}/.complete")
endfunction()

fetch_electron_archive("electron-v${ELECTRON_VERSION}-win32-x64" ${ELECTRON_SHA256} "Electron ${ELECTRON_VERSION} (Chromium, about 160 MB)")
fetch_electron_archive("ffmpeg-v${ELECTRON_VERSION}-win32-x64" ${ELECTRON_FFMPEG_SHA256} "Electron's FFmpeg without patented codecs (1 MB)")
set(ELECTRON_ROOT "${ELECTRON_CACHE_DIR}/electron-v${ELECTRON_VERSION}-win32-x64")
set(ELECTRON_FFMPEG "${ELECTRON_CACHE_DIR}/ffmpeg-v${ELECTRON_VERSION}-win32-x64/ffmpeg.dll")

# Sets the fuses of the staged program (see fuses.cpp)
add_executable(electron_fuses fuses.cpp)
set_target_properties(electron_fuses PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_options(electron_fuses PRIVATE -static)

# What Electron needs at run time. Left out: all locales but en-US, Electron's sample app (default_app.asar), its
# version file and its FFmpeg with patented codecs, replaced by the one without: H.264 and HEVC video are then decoded
# by the graphics card, and AAC audio not at all.
set(ELECTRON_RUNTIME_FILES
    "${ELECTRON_ROOT}/chrome_100_percent.pak"
    "${ELECTRON_ROOT}/chrome_200_percent.pak"
    "${ELECTRON_ROOT}/d3dcompiler_47.dll"
    "${ELECTRON_ROOT}/dxcompiler.dll"
    "${ELECTRON_ROOT}/electron_wer.dll"
    "${ELECTRON_ROOT}/icudtl.dat"
    "${ELECTRON_ROOT}/resources.pak"
    "${ELECTRON_ROOT}/snapshot_blob.bin"
    "${ELECTRON_ROOT}/v8_context_snapshot.bin"
    "${ELECTRON_ROOT}/vk_swiftshader.dll"
    "${ELECTRON_ROOT}/vk_swiftshader_icd.json"
    "${ELECTRON_ROOT}/vulkan-1.dll"
    "${ELECTRON_ROOT}/LICENSE"
    "${ELECTRON_ROOT}/LICENSES.chromium.html"
    "${ELECTRON_FFMPEG}")
set(BROWSER_APP_FILES app/package.json app/main.js app/page.js app/header.html app/header.js)
# Fuses: 0 RunAsNode and 2 NODE_OPTIONS, 3 --inspect off (the program cannot be made to run other scripts, and a
# NODE_OPTIONS set for development elsewhere on the computer does not reach it); 1 cookie encryption on (the Nexus Mods
# login is stored encrypted for this Windows user)
set(ELECTRON_FUSES 0=0 1=1 2=0 3=0)
# What the CEF-based window of earlier versions left behind
set(BROWSER_OBSOLETE_FILES betternmm-browser.dll libcef.dll chrome_elf.dll LICENSE.txt CREDITS.html)

set(stage "${CMAKE_BINARY_DIR}/browser")
set(staged_app)
foreach(file IN LISTS BROWSER_APP_FILES)
    get_filename_component(name "${file}" NAME)
    list(APPEND staged_app "${stage}/resources/app/${name}")
endforeach()
add_custom_command(OUTPUT "${stage}/betternmm-browser.exe"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${stage}/locales"
    COMMAND ${CMAKE_COMMAND} -E chdir "${stage}" ${CMAKE_COMMAND} -E rm -f ${BROWSER_OBSOLETE_FILES}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ELECTRON_RUNTIME_FILES} "${stage}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ELECTRON_ROOT}/locales/en-US.pak" "${stage}/locales"
    COMMAND ${CMAKE_COMMAND} -E copy "${ELECTRON_ROOT}/electron.exe" "${stage}/betternmm-browser.exe.new"
    COMMAND electron_fuses "${stage}/betternmm-browser.exe.new" ${ELECTRON_FUSES}
    COMMAND ${CMAKE_COMMAND} -E rename "${stage}/betternmm-browser.exe.new" "${stage}/betternmm-browser.exe"
    DEPENDS electron_fuses ${ELECTRON_RUNTIME_FILES} "${ELECTRON_ROOT}/electron.exe"
    COMMENT "Staging the Nexus Mods download window (Electron ${ELECTRON_VERSION})"
    VERBATIM)
add_custom_command(OUTPUT ${staged_app}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${stage}/resources/app"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BROWSER_APP_FILES} "${stage}/resources/app"
    DEPENDS ${BROWSER_APP_FILES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM)
add_custom_target(BetterNMM_browser ALL DEPENDS "${stage}/betternmm-browser.exe" ${staged_app})

install(CODE "
    foreach(name IN ITEMS ${BROWSER_OBSOLETE_FILES})
        file(REMOVE \"\${CMAKE_INSTALL_PREFIX}/browser/\${name}\")
    endforeach()
")
install(FILES ${ELECTRON_RUNTIME_FILES} DESTINATION browser)
install(FILES "${ELECTRON_ROOT}/locales/en-US.pak" DESTINATION browser/locales)
install(FILES ${BROWSER_APP_FILES} DESTINATION browser/resources/app)
install(PROGRAMS "${stage}/betternmm-browser.exe" DESTINATION browser)

# Compressed on disk the way Windows compresses its own system files (CompactOS, LZX): the window's first start takes a
# little longer while Windows unpacks what it reads. Drives without NTFS skip it.
install(CODE [[
    file(TO_NATIVE_PATH "${CMAKE_INSTALL_PREFIX}/browser" browser_dir)
    execute_process(COMMAND compact /c /s:${browser_dir} /exe:lzx /q /i OUTPUT_QUIET ERROR_QUIET)
]])

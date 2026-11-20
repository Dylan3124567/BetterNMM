project(application)

################################ FILES ################################

set(LAUNCHER_SOURCES
    # core
    Application.cpp
    Application.h
    ApplicationMessage.cpp
    ApplicationMessage.h
    AssertHelpers.h
    BaseInstance.cpp
    BaseInstance.h
    Commandline.cpp
    Commandline.h
    DesktopServices.cpp
    DesktopServices.h
    EnumWrapper.h
    ExponentialSeries.h
    FastFileIconProvider.cpp
    FastFileIconProvider.h
    FileIgnoreProxy.cpp
    FileIgnoreProxy.h
    FileSystem.cpp
    FileSystem.h
    Filter.h
    Handle.h
    HardwareInfo.cpp
    HardwareInfo.h
    InstanceCopyPrefs.h
    InstanceCopyTask.cpp
    InstanceCopyTask.h
    InstanceDirUpdate.cpp
    InstanceDirUpdate.h
    InstanceImportTask.cpp
    InstanceImportTask.h
    InstanceList.cpp
    InstanceList.h
    InstancePageProvider.h
    InstanceTask.cpp
    InstanceTask.h
    Json.cpp
    Json.h
    KonamiCode.cpp
    KonamiCode.h
    LaunchController.cpp
    LaunchController.h
    LoggedProcess.cpp
    LoggedProcess.h
    MMCTime.cpp
    MMCTime.h
    MMCZip.cpp
    MMCZip.h
    MTPixmapCache.h
    Markdown.cpp
    Markdown.h
    MessageLevel.cpp
    MessageLevel.h
    NullInstance.h
    PSaveFile.h
    ProblemProvider.h
    QObjectPtr.h
    RWStorage.h
    Result.h
    SeparatorPrefixTree.h
    ShortcutUtils.cpp
    ShortcutUtils.h
    StringUtils.cpp
    StringUtils.h
    Usable.h
    Version.cpp
    Version.h
    WatchLock.h
    # archive
    archive/ArchiveReader.cpp
    archive/ArchiveReader.h
    archive/ArchiveWriter.cpp
    archive/ArchiveWriter.h
    archive/ExportToZipTask.cpp
    archive/ExportToZipTask.h
    archive/ExtractZipTask.cpp
    archive/ExtractZipTask.h
    # console
    console/Console.h
    console/WindowsConsole.cpp
    console/WindowsConsole.h
    # game
    game/AutoSort.cpp
    game/AutoSort.h
    game/CrashReport.cpp
    game/CrashReport.h
    game/HealthCheck.cpp
    game/HealthCheck.h
    game/Deployer.cpp
    game/Deployer.h
    game/Fomod.cpp
    game/FomodPatches.cpp
    game/Nemesis.cpp
    game/Nemesis.h
    game/FomodPatches.h
    game/Fomod.h
    game/GameDefinition.cpp
    game/GameDefinition.h
    game/GameFiles.cpp
    game/GameFiles.h
    game/GameInstance.cpp
    game/GameInstance.h
    game/GameInstanceCreationTask.cpp
    game/GameInstanceCreationTask.h
    game/Ini.cpp
    game/Ini.h
    game/IniSettings.cpp
    game/IniSettings.h
    game/loot/Condition.cpp
    game/loot/Condition.h
    game/loot/Masterlist.cpp
    game/loot/Masterlist.h
    game/loot/Plugin.cpp
    game/loot/Plugin.h
    game/loot/Sorter.cpp
    game/loot/Sorter.h
    game/loot/Yaml.cpp
    game/loot/Yaml.h
    game/ModInstaller.cpp
    game/ModInstaller.h
    game/ModList.cpp
    game/ModList.h
    game/ModListExport.cpp
    game/ModListExport.h
    game/PatternMatcher.h
    game/PluginList.cpp
    game/PluginList.h
    game/Steam.cpp
    game/Steam.h
    game/VersionCheck.cpp
    game/VersionCheck.h
    # icons
    icons/IconList.cpp
    icons/IconList.h
    icons/IconUtils.cpp
    icons/IconUtils.h
    icons/MMCIcon.cpp
    icons/MMCIcon.h
    # launch
    launch/LaunchStep.cpp
    launch/LaunchStep.h
    launch/LaunchTask.cpp
    launch/LaunchTask.h
    launch/LogModel.cpp
    launch/LogModel.h
    launch/TaskStepWrapper.cpp
    launch/TaskStepWrapper.h
    # launch/steps
    launch/steps/LaunchCommand.cpp
    launch/steps/LaunchCommand.h
    launch/steps/LaunchGameProcess.cpp
    launch/steps/LaunchGameProcess.h
    launch/steps/PrepareGame.cpp
    launch/steps/PrepareGame.h
    launch/steps/QuitAfterGameStop.cpp
    launch/steps/QuitAfterGameStop.h
    launch/steps/TextPrint.cpp
    launch/steps/TextPrint.h
    # net
    net/ApiHeaderProxy.h
    net/ApiRequest.h
    net/ByteArraySink.h
    net/ChecksumValidator.h
    net/DummySink.h
    net/FileSink.cpp
    net/FileSink.h
    net/HeaderProxy.h
    net/HttpMetaCache.cpp
    net/HttpMetaCache.h
    net/Logging.cpp
    net/Logging.h
    net/MetaCacheSink.cpp
    net/MetaCacheSink.h
    net/Mode.h
    net/DownloadManager.cpp
    net/DownloadManager.h
    net/NetJob.cpp
    net/NetJob.h
    net/RPCSink.h
    net/RawHeaderProxy.h
    net/Request.cpp
    net/Request.h
    net/Sink.h
    net/Validator.h
    # nexus
    nexus/NexusAccount.cpp
    nexus/NexusAccount.h
    nexus/NexusApi.cpp
    nexus/NexusApi.h
    nexus/NxmLink.cpp
    nexus/NxmLink.h
    # vortex
    vortex/VortexImport.cpp
    vortex/VortexImport.h
    vortex/VortexState.cpp
    vortex/VortexState.h
    # settings
    settings/INIFile.cpp
    settings/INIFile.h
    settings/INISettingsObject.cpp
    settings/INISettingsObject.h
    settings/OverrideSetting.cpp
    settings/OverrideSetting.h
    settings/PassthroughSetting.cpp
    settings/PassthroughSetting.h
    settings/Setting.cpp
    settings/Setting.h
    settings/SettingsObject.cpp
    settings/SettingsObject.h
    # tasks
    tasks/ConcurrentTask.cpp
    tasks/ConcurrentTask.h
    tasks/MultipleOptionsTask.cpp
    tasks/MultipleOptionsTask.h
    tasks/SequentialTask.cpp
    tasks/SequentialTask.h
    tasks/Task.cpp
    tasks/Task.h
    # ui
    ui/GuiUtil.cpp
    ui/GuiUtil.h
    ui/InstanceWindow.cpp
    ui/InstanceWindow.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/SelfUpdate.cpp
    ui/SelfUpdate.h
    ui/MainWindow.ui
    ui/MultiDecorationItemDelegate.cpp
    ui/MultiDecorationItemDelegate.h
    ui/ToolTipFilter.cpp
    ui/ToolTipFilter.h
    ui/ViewLogWindow.cpp
    ui/ViewLogWindow.h
    # ui/dialogs
    ui/dialogs/AboutDialog.cpp
    ui/dialogs/AboutDialog.h
    ui/dialogs/AboutDialog.ui
    ui/dialogs/CrashReportDialog.cpp
    ui/dialogs/CrashReportDialog.h
    ui/dialogs/CopyInstanceDialog.cpp
    ui/dialogs/CopyInstanceDialog.h
    ui/dialogs/CopyInstanceDialog.ui
    ui/dialogs/CreateShortcutDialog.cpp
    ui/dialogs/CreateShortcutDialog.h
    ui/dialogs/CreateShortcutDialog.ui
    ui/dialogs/CustomMessageBox.cpp
    ui/dialogs/CustomMessageBox.h
    ui/dialogs/ExportInstanceDialog.cpp
    ui/dialogs/ExportInstanceDialog.h
    ui/dialogs/ExportInstanceDialog.ui
    ui/dialogs/FomodDialog.cpp
    ui/dialogs/FomodDialog.h
    ui/dialogs/IconPickerDialog.cpp
    ui/dialogs/IconPickerDialog.h
    ui/dialogs/IconPickerDialog.ui
    ui/dialogs/ModInstallFlow.cpp
    ui/dialogs/ModInstallFlow.h
    ui/dialogs/NetworkJobFailedDialog.cpp
    ui/dialogs/NetworkJobFailedDialog.h
    ui/dialogs/NetworkJobFailedDialog.ui
    ui/dialogs/NewInstanceDialog.cpp
    ui/dialogs/VortexImportDialog.cpp
    ui/dialogs/VortexImportDialog.h
    ui/dialogs/NewInstanceDialog.h
    ui/dialogs/NewInstanceDialog.ui
    ui/dialogs/NexusBrowseDialog.cpp
    ui/dialogs/DownloadsDialog.cpp
    ui/dialogs/DownloadsDialog.h
    ui/dialogs/ConflictsDialog.cpp
    ui/dialogs/ConflictsDialog.h
    ui/dialogs/NexusDownloadQueue.cpp
    ui/dialogs/NexusDownloadQueue.h
    ui/dialogs/NexusBrowseDialog.h
    ui/dialogs/NexusLoginDialog.cpp
    ui/dialogs/NexusLoginDialog.h
    ui/dialogs/ProgressDialog.cpp
    ui/dialogs/ProgressDialog.h
    ui/dialogs/ProgressDialog.ui
    ui/dialogs/ScrollMessageBox.cpp
    ui/dialogs/ScrollMessageBox.h
    ui/dialogs/ScrollMessageBox.ui
    # ui/instanceview
    ui/instanceview/AccessibleInstanceView.cpp
    ui/instanceview/AccessibleInstanceView.h
    ui/instanceview/AccessibleInstanceView_p.h
    ui/instanceview/InstanceDelegate.cpp
    ui/instanceview/InstanceDelegate.h
    ui/instanceview/InstanceProxyModel.cpp
    ui/instanceview/InstanceProxyModel.h
    ui/instanceview/InstanceView.cpp
    ui/instanceview/InstanceView.h
    ui/instanceview/VisualGroup.cpp
    ui/instanceview/VisualGroup.h
    # ui/pagedialog
    ui/pagedialog/PageDialog.cpp
    ui/pagedialog/PageDialog.h
    # ui/pages
    ui/pages/BasePage.h
    ui/pages/BasePageContainer.h
    ui/pages/BasePageProvider.h
    # ui/pages/global
    ui/pages/global/AccountListPage.cpp
    ui/pages/global/AccountListPage.h
    ui/pages/global/AppearancePage.h
    ui/pages/global/GamesPage.h
    ui/pages/global/LauncherPage.cpp
    ui/pages/global/LauncherPage.h
    ui/pages/global/LauncherPage.ui
    ui/pages/global/ProxyPage.cpp
    ui/pages/global/ProxyPage.h
    ui/pages/global/ProxyPage.ui
    # ui/pages/instance
    ui/pages/instance/GameFilesPage.cpp
    ui/pages/instance/GameFilesPage.h
    ui/pages/instance/GameSettingsPage.h
    ui/pages/instance/ListPage.cpp
    ui/pages/instance/ListPage.h
    ui/pages/instance/LogPage.cpp
    ui/pages/instance/LogPage.h
    ui/pages/instance/LogPage.ui
    ui/pages/instance/ModsPage.cpp
    ui/pages/instance/ModsPage.h
    ui/pages/instance/NotesPage.cpp
    ui/pages/instance/NotesPage.h
    ui/pages/instance/NotesPage.ui
    ui/pages/instance/OtherLogsPage.cpp
    ui/pages/instance/OtherLogsPage.h
    ui/pages/instance/OtherLogsPage.ui
    ui/pages/instance/PluginsPage.cpp
    ui/pages/instance/PluginsPage.h
    ui/pages/instance/HealthPage.cpp
    ui/pages/instance/HealthPage.h
    ui/pages/instance/SavesPage.cpp
    ui/pages/instance/SavesPage.h
    # ui/pages/newinstance
    ui/pages/newinstance/GamePage.cpp
    ui/pages/newinstance/GamePage.h
    ui/pages/newinstance/ImportPage.cpp
    ui/pages/newinstance/ImportPage.h
    # ui/setupwizard
    ui/setupwizard/BaseWizardPage.h
    ui/setupwizard/LoginWizardPage.cpp
    ui/setupwizard/LoginWizardPage.h
    ui/setupwizard/LoginWizardPage.ui
    ui/setupwizard/SetupWizard.cpp
    ui/setupwizard/SetupWizard.h
    ui/setupwizard/ThemeWizardPage.h
    # ui/themes
    ui/themes/BrightTheme.cpp
    ui/themes/BrightTheme.h
    ui/themes/CatPack.cpp
    ui/themes/CatPack.h
    ui/themes/CatPainter.cpp
    ui/themes/CatPainter.h
    ui/themes/CustomTheme.cpp
    ui/themes/CustomTheme.h
    ui/themes/DarkTheme.cpp
    ui/themes/DarkTheme.h
    ui/themes/FusionTheme.cpp
    ui/themes/FusionTheme.h
    ui/themes/HintOverrideProxyStyle.cpp
    ui/themes/HintOverrideProxyStyle.h
    ui/themes/ITheme.cpp
    ui/themes/ITheme.h
    ui/themes/IconTheme.cpp
    ui/themes/IconTheme.h
    ui/themes/SystemTheme.cpp
    ui/themes/SystemTheme.h
    ui/themes/ThemeManager.cpp
    ui/themes/ThemeManager.h
    # ui/widgets
    ui/widgets/AppearanceWidget.cpp
    ui/widgets/AppearanceWidget.h
    ui/widgets/AppearanceWidget.ui
    ui/widgets/Common.cpp
    ui/widgets/Common.h
    ui/widgets/CustomCommands.cpp
    ui/widgets/CustomCommands.h
    ui/widgets/CustomCommands.ui
    ui/widgets/EditableLabel.cpp
    ui/widgets/EditableLabel.h
    ui/widgets/EnvironmentVariables.cpp
    ui/widgets/EnvironmentVariables.h
    ui/widgets/EnvironmentVariables.ui
    ui/widgets/GameSettingsWidget.cpp
    ui/widgets/GameSettingsWidget.h
    ui/widgets/IconLabel.cpp
    ui/widgets/IconLabel.h
    ui/widgets/LogView.cpp
    ui/widgets/LogView.h
    ui/widgets/PageContainer.cpp
    ui/widgets/PageContainer.h
    ui/widgets/PageContainer_p.h
    ui/widgets/ProgressWidget.cpp
    ui/widgets/ProgressWidget.h
    ui/widgets/ProjectItem.cpp
    ui/widgets/ProjectItem.h
    ui/widgets/IniEditor.cpp
    ui/widgets/IniEditor.h
    ui/widgets/IniSettingsView.cpp
    ui/widgets/IniSettingsView.h
    ui/widgets/ProjectDescriptionPage.cpp
    ui/widgets/ProjectDescriptionPage.h
    ui/widgets/VariableSizedImageObject.cpp
    ui/widgets/VariableSizedImageObject.h
    ui/widgets/SubTaskProgressBar.cpp
    ui/widgets/SubTaskProgressBar.h
    ui/widgets/SubTaskProgressBar.ui
    ui/widgets/WideBar.cpp
    ui/widgets/WideBar.h
)

######## Logging categories ########

ecm_qt_export_logging_category(
    IDENTIFIER taskLogC
    CATEGORY_NAME "launcher.task"
    DEFAULT_SEVERITY Debug
    DESCRIPTION "Task actions"
    EXPORT "${Launcher_Name}"
)

ecm_qt_export_logging_category(
    IDENTIFIER taskNetLogC
    CATEGORY_NAME "launcher.task.net"
    DEFAULT_SEVERITY Debug
    DESCRIPTION "Task network action"
    EXPORT "${Launcher_Name}"
)

ecm_qt_export_logging_category(
    IDENTIFIER taskDownloadLogC
    CATEGORY_NAME "launcher.task.net.download"
    DEFAULT_SEVERITY Debug
    DESCRIPTION "Task network download actions"
    EXPORT "${Launcher_Name}"
)

ecm_qt_export_logging_category(
    IDENTIFIER taskUploadLogC
    CATEGORY_NAME "launcher.task.net.upload"
    DEFAULT_SEVERITY Debug
    DESCRIPTION "Task network upload actions"
    EXPORT "${Launcher_Name}"
)

ecm_qt_export_logging_category(
    IDENTIFIER taskMetaCacheLogC
    CATEGORY_NAME "launcher.task.net.metacache"
    DEFAULT_SEVERITY Debug
    DESCRIPTION "task network meta-cache actions"
    EXPORT "${Launcher_Name}"
)

ecm_qt_export_logging_category(
    IDENTIFIER taskHttpMetaCacheLogC
    CATEGORY_NAME "launcher.task.net.metacache.http"
    DEFAULT_SEVERITY Debug
    DESCRIPTION "task network http meta-cache actions"
    EXPORT "${Launcher_Name}"
)

qt_add_resources(LAUNCHER_RESOURCES
    resources/backgrounds/backgrounds.qrc
    resources/breeze_dark/breeze_dark.qrc
    resources/breeze_light/breeze_light.qrc
    resources/documents/documents.qrc
    resources/flat/flat.qrc
    resources/games/games.qrc
    resources/flat_white/flat_white.qrc
    resources/legacy/legacy.qrc
    resources/pe_blue/pe_blue.qrc
    resources/pe_colored/pe_colored.qrc
    resources/pe_dark/pe_dark.qrc
    resources/pe_light/pe_light.qrc
    resources/style/style.qrc
    "${CMAKE_CURRENT_BINARY_DIR}/../${Launcher_Branding_LogoQRC}"
)

######## Windows resource files ########
set(LAUNCHER_RCS ${CMAKE_CURRENT_BINARY_DIR}/../${Launcher_Branding_WindowsRC})
# the resource compiler reads the icon itself, so the build only notices a new icon when told
set_source_files_properties(${LAUNCHER_RCS} PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/../${Launcher_Branding_ICO})

######## Precompiled Headers ###########

if(${Launcher_USE_PCH})
    message(STATUS "Using precompiled headers for applicable launcher targets")
    set(PRECOMPILED_HEADERS
        include/base.pch.hpp
        include/qtcore.pch.hpp
        include/qtgui.pch.hpp
    )
endif()

####### Targets ########

add_library(Launcher_logic STATIC ${LAUNCHER_SOURCES} ${LAUNCHER_RESOURCES})
target_include_directories(Launcher_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(Launcher_logic PUBLIC LAUNCHER_APPLICATION)

if(${Launcher_USE_PCH})
    target_precompile_headers(Launcher_logic PRIVATE ${PRECOMPILED_HEADERS})
endif()

target_link_libraries(Launcher_logic
    BuildConfig
    LocalPeer
    Launcher_rainbow
    version  # GetFileVersionInfo: which script extender build fits the game
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Xml
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
)
if(TARGET PkgConfig::libarchive)
    target_link_libraries(Launcher_logic PkgConfig::libarchive)
else()
    target_link_libraries(Launcher_logic LibArchive::LibArchive)
endif()

add_executable(${Launcher_Name} WIN32 main.cpp ${LAUNCHER_RCS})
if(TARGET BetterNMM_browser)
    add_dependencies(${Launcher_Name} BetterNMM_browser) # the download window ships with every build
endif()
target_link_options(${Launcher_Name} PRIVATE
    # /MANIFEST:NO disables generating a manifest file, we instead provide our own
    $<$<STREQUAL:${CMAKE_CXX_COMPILER_FRONTEND_VARIANT},MSVC>:/MANIFEST:NO>
)

target_link_libraries(${Launcher_Name} Launcher_logic)

if(DEFINED Launcher_APP_BINARY_NAME)
    set_target_properties(${Launcher_Name} PROPERTIES OUTPUT_NAME "${Launcher_APP_BINARY_NAME}")
endif()

install(TARGETS ${Launcher_Name}
    RUNTIME_DEPENDENCY_SET LAUNCHER_DEPENDENCY_SET
    RUNTIME DESTINATION ${BINARY_DEST_DIR} COMPONENT Runtime
)

# Deploy PDBs
if(CMAKE_CXX_LINKER_SUPPORTS_PDB)
    install(FILES $<TARGET_PDB_FILE:${Launcher_Name}> DESTINATION ${BINARY_DEST_DIR} OPTIONAL)
endif()

# Set basic compiler warning/error flags for all targets
get_directory_property(Launcher_TARGETS BUILDSYSTEM_TARGETS)
foreach(target ${Launcher_TARGETS})
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /wd4100
            $<$<STREQUAL:${CMAKE_CXX_COMPILER_ID},Clang>:-Wno-missing-field-initializers -Wno-missing-designated-field-initializers>
        )
        target_link_options(${target} PRIVATE /STACK:8388608)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers
        )
    endif()
endforeach()

set_target_properties(${Launcher_TARGETS} PROPERTIES COMPILE_WARNING_AS_ERROR ${Launcher_WARNING_AS_ERROR})

#### Bundle: the Qt runtime and every linked DLL next to the executable ####
set(QT_DEPLOY_TOOL_OPTIONS "--no-opengl-sw --no-quick-import --no-system-d3d-compiler --no-system-dxc-compiler --skip-plugin-types generic,networkinformation")

qt_generate_deploy_script(
    TARGET ${Launcher_Name}
    OUTPUT_SCRIPT QT_DEPLOY_SCRIPT
    CONTENT "
    qt_deploy_runtime_dependencies(
        EXECUTABLE ${BINARY_DEST_DIR}/$<TARGET_FILE_NAME:${Launcher_Name}>
        BIN_DIR ${BINARY_DEST_DIR}
        LIBEXEC_DIR ${LIBRARY_DEST_DIR}
        LIB_DIR ${LIBRARY_DEST_DIR}
        PLUGINS_DIR ${PLUGIN_DEST_DIR}
        NO_OVERWRITE
        NO_TRANSLATIONS
        NO_COMPILER_RUNTIME
        DEPLOY_TOOL_OPTIONS ${QT_DEPLOY_TOOL_OPTIONS}
    )"
)

install(
    RUNTIME_DEPENDENCY_SET LAUNCHER_DEPENDENCY_SET
    COMPONENT bundle
    DIRECTORIES
        $ENV{PATH}
        ${CMAKE_SYSTEM_LIBRARY_PATH}
        ${QT_LIBS_DIR}
        ${QT_LIBEXECS_DIR}
    PRE_EXCLUDE_REGEXES
        "^(api-ms-win|ext-ms)-.*\\.dll$"
        "^azure.*\\.dll$"
        "^vcruntime.*\\.dll$"
    POST_EXCLUDE_REGEXES
        "system32"
    LIBRARY DESTINATION ${LIBRARY_DEST_DIR}
    RUNTIME DESTINATION ${BINARY_DEST_DIR}
)
install(
    SCRIPT ${QT_DEPLOY_SCRIPT}
    COMPONENT bundle
)
# windeployqt copies Qt's plugins but not the libraries they load (libjpeg for JPEG, libwebp for WebP...), which then only
# load on a PC with MSYS2 on PATH; they come from the same folders as the executable's own libraries
file(TO_CMAKE_PATH "$ENV{PATH}" LAUNCHER_PATH_DIRS)
set(LAUNCHER_PLUGIN_LIBRARY_DIRS ${LAUNCHER_PATH_DIRS} ${CMAKE_SYSTEM_LIBRARY_PATH}
    ${QT${QT_VERSION_MAJOR}_INSTALL_PREFIX}/${QT${QT_VERSION_MAJOR}_INSTALL_BINS})
install(
    CODE "
        set(_dirs \"${LAUNCHER_PLUGIN_LIBRARY_DIRS}\")
        file(GLOB _plugins \"\${CMAKE_INSTALL_PREFIX}/${PLUGIN_DEST_DIR}/*/*.dll\")
        list(FILTER _plugins EXCLUDE REGEX \"/browser/\")
        file(GET_RUNTIME_DEPENDENCIES
            LIBRARIES \${_plugins}
            RESOLVED_DEPENDENCIES_VAR _libraries
            UNRESOLVED_DEPENDENCIES_VAR _missing
            DIRECTORIES \${_dirs}
            PRE_EXCLUDE_REGEXES \"^(api-ms-win|ext-ms)-\" \"^vcruntime\"
            POST_EXCLUDE_REGEXES \"[Ss]ystem32\")
        if(_missing)
            message(FATAL_ERROR \"Libraries Qt's plugins need were not found: \${_missing}\")
        endif()
        file(INSTALL \${_libraries} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${BINARY_DEST_DIR}\")
    "
    COMPONENT bundle
)
install(
    CODE "file(WRITE \"\${CMAKE_INSTALL_PREFIX}/${RESOURCES_DEST_DIR}/qt.conf\" \" \")"
    COMPONENT bundle
)
install(
    FILES "qtlogging.ini"
    # relative, so it follows the prefix given at install time (cmake --install --prefix) like everything else
    DESTINATION ${RESOURCES_DEST_DIR}
    COMPONENT bundle
)

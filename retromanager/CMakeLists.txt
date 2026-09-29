cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Build switches (see CMakePresets.json for the supported combinations)
#   RM_BUILD_APP    Borealis application (needs -DPLATFORM_DESKTOP=ON or -DPLATFORM_SWITCH=ON)
#   RM_BUILD_TESTS  GoogleTest suite, desktop only; can be built without the app
# ---------------------------------------------------------------------------
option(RM_BUILD_APP "Build the Borealis application" ON)
option(RM_BUILD_TESTS "Build the unit tests (desktop only)" OFF)
option(RM_WITH_CURL "Build the libcurl-based network clients (FtpClient)" ON)
option(RM_WITH_SMB "Build the SMB2/3 client (SmbClient, libsmb2)" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (RM_BUILD_APP)
    include(FetchBorealis)  # sets up the toolchain: must run before project()
endif ()

project(RetroManager VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Keep GNU extensions (gnu++17): with strict -std=c++17 newlib hides POSIX
# functions (fileno, fdopen) that Borealis' bundled fmt needs on Switch.
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (PLATFORM_SWITCH AND RM_BUILD_TESTS)
    message(FATAL_ERROR "Unit tests run on the host: configure them with the 'tests' or 'desktop' preset.")
endif ()

include(Dependencies)

set(RM_WARNINGS -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# retromanager_core: platform-agnostic code (core, abstractions, and later
# parsers / network / services). No Borealis, no libnx: always testable on PC.
# ---------------------------------------------------------------------------
add_library(retromanager_core STATIC
    src/core/AppContext.cpp
    src/core/Crc32.cpp
    src/core/EventBus.cpp
    src/core/FileName.cpp
    src/core/Format.cpp
    src/core/Md5.cpp
    src/core/Result.cpp
    src/core/Url.cpp
    src/core/WorkerThread.cpp
    src/forwarder/Aes128.cpp
    src/forwarder/IconMaker.cpp
    src/forwarder/Sha256.cpp
    src/forwarder/nsp/Containers.cpp
    src/forwarder/nsp/Metadata.cpp
    src/forwarder/nsp/Nca.cpp
    src/forwarder/StbImage.cpp
    src/fs/FileInstall.cpp
    src/fs/RomStore.cpp
    src/models/Bios.cpp
    src/models/Systems.cpp
    src/network/MockRemoteSource.cpp
    src/network/SourceRouter.cpp
    src/parsers/CfgDocument.cpp
    src/parsers/ConfigParser.cpp
    src/parsers/EntryJson.cpp
    src/parsers/IniDocument.cpp
    src/parsers/PlaylistDocument.cpp
    src/parsers/RepoIndexParser.cpp
    src/platform/common/BufferedWriteStream.cpp
    src/platform/common/IFileSystem.cpp
    src/platform/common/ISystem.cpp
    src/platform/common/LocalFileSystem.cpp
    src/platform/common/VirtualPath.cpp
    src/services/AppManager.cpp
    src/services/BiosManager.cpp
    src/services/CheatManager.cpp
    src/services/CloudSyncService.cpp
    src/services/ConfigManager.cpp
    src/services/DownloadQueueManager.cpp
    src/services/ForwarderBuilder.cpp
    src/services/EmulatorConfigurator.cpp
    src/services/PlaylistManager.cpp
    src/services/QueueStore.cpp
    src/services/RetroArchPaths.cpp
    src/services/SysClkConfigurator.cpp
    src/services/ThumbnailManager.cpp
    src/services/ShopService.cpp
    src/services/SyncPlanner.cpp
)
target_include_directories(retromanager_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
find_package(Threads REQUIRED)
target_link_libraries(retromanager_core PUBLIC nlohmann_json::nlohmann_json Threads::Threads PRIVATE rm_stb)
# Third-party code: built as is, without our warning set. stb's JPEG writer
# left-shifts negative values on purpose (well-defined since C++20, UB for
# UBSan in C++17): that single check is off for this file, ASan and the
# other UBSan checks stay on (a no-op without -fsanitize).
set_source_files_properties(src/forwarder/StbImage.cpp PROPERTIES COMPILE_OPTIONS "-w;-fno-sanitize=shift-base")
target_compile_definitions(retromanager_core PUBLIC RM_VERSION="${PROJECT_VERSION}")
target_compile_options(retromanager_core PRIVATE ${RM_WARNINGS})

# ---------------------------------------------------------------------------
# retromanager_smb: SMB2/3 client backed by libsmb2 (independent of curl).
# ---------------------------------------------------------------------------
if (RM_WITH_SMB)
    add_library(retromanager_smb STATIC src/network/SmbClient.cpp)
    target_link_libraries(retromanager_smb PUBLIC retromanager_core PRIVATE smb2)
    target_compile_definitions(retromanager_smb PUBLIC RM_WITH_SMB)
    target_compile_options(retromanager_smb PRIVATE ${RM_WARNINGS})
endif ()

# ---------------------------------------------------------------------------
# retromanager_curl: network clients backed by libcurl. Kept out of the core
# so the core stays dependency-free.
# ---------------------------------------------------------------------------
if (RM_WITH_CURL)
    include(Curl)
    add_library(retromanager_curl STATIC
        src/network/CurlCommon.cpp
        src/network/FtpClient.cpp
        src/network/HttpClient.cpp
        src/network/SourceCatalog.cpp
        src/network/SourceFactory.cpp)
    target_link_libraries(retromanager_curl PUBLIC retromanager_core PRIVATE CURL::libcurl)
    if (RM_WITH_SMB)
        target_link_libraries(retromanager_curl PUBLIC retromanager_smb)  # SourceFactory creates SmbClient too
    endif ()
    target_compile_options(retromanager_curl PRIVATE ${RM_WARNINGS})
endif ()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
if (RM_BUILD_APP)
    set(RM_RESOURCES_OUT ${CMAKE_BINARY_DIR}/resources)

    if (PLATFORM_DESKTOP)
        # Bake the absolute resources path so the binary runs from any cwd.
        set(BRLS_RESOURCES_DIR ${CMAKE_BINARY_DIR})
    endif ()
    add_subdirectory(${BOREALIS_LIBRARY} ${CMAKE_BINARY_DIR}/borealis EXCLUDE_FROM_ALL)

    # Exactly one platform implementation is linked into the app.
    if (PLATFORM_SWITCH)
        set(RM_PLATFORM_SRC src/platform/switch/SwitchPlatform.cpp)
    else ()
        set(RM_PLATFORM_SRC src/platform/desktop/DesktopPlatform.cpp)
    endif ()

    set(RM_APP_SRC
        src/main.cpp
        src/ui/AppsListActivity.cpp
        src/ui/BiosActivity.cpp
        src/ui/DownloadNotifier.cpp
        src/ui/DownloadsActivity.cpp
        src/ui/DownloadSummary.cpp
        src/ui/ForwarderActivity.cpp
        src/ui/GamesListActivity.cpp
        src/ui/HomeActivity.cpp
        src/ui/SyncActivity.cpp
        ${RM_PLATFORM_SRC}
    )
    if (PLATFORM_SWITCH)
        list(APPEND RM_APP_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
    endif ()

    if (RM_WITH_CURL)
        list(APPEND RM_APP_SRC src/ui/SourcesActivity.cpp src/ui/SourceFormActivity.cpp)  # edits sources through the curl clients
    endif ()
    program_target(${PROJECT_NAME} "${RM_APP_SRC}")
    target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
    target_link_libraries(${PROJECT_NAME} PRIVATE retromanager_core borealis ${APP_PLATFORM_LIB})
    if (RM_WITH_CURL)
        target_link_libraries(${PROJECT_NAME} PRIVATE retromanager_curl)
        target_compile_definitions(${PROJECT_NAME} PRIVATE RM_WITH_CURL)
    endif ()
    if (RM_WITH_SMB)
        target_link_libraries(${PROJECT_NAME} PRIVATE retromanager_smb)
    endif ()
    if (PLATFORM_DESKTOP)
        target_compile_definitions(${PROJECT_NAME} PRIVATE
            RM_DESKTOP_DEFAULT_SD_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/sdmc")
    endif ()

    # Runtime resources = the Borealis base set + ours (ours win on conflict).
    add_custom_target(${PROJECT_NAME}_resources
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${RM_RESOURCES_OUT}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/font ${RM_RESOURCES_OUT}/font
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/material ${RM_RESOURCES_OUT}/material
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/img/sys ${RM_RESOURCES_OUT}/img/sys
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_RESOURCES}/i18n ${RM_RESOURCES_OUT}/i18n
        COMMAND ${CMAKE_COMMAND} -E rm -f ${RM_RESOURCES_OUT}/i18n/en-US/demo.json ${RM_RESOURCES_OUT}/i18n/fr/demo.json
                ${RM_RESOURCES_OUT}/i18n/zh-Hans/demo.json
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${RM_RESOURCES_OUT}
        COMMENT "Assembling resources"
        VERBATIM
    )
    add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_resources)

    if (PLATFORM_SWITCH)
        # Fonts come from the console's shared font; strip them from romfs.
        add_custom_target(${PROJECT_NAME}.nro ALL DEPENDS ${PROJECT_NAME}
            COMMAND ${NX_NACPTOOL_EXE} --create "RetroManager" "RetroManager contributors" "${PROJECT_VERSION}"
                    ${PROJECT_NAME}.nacp
            COMMAND ${CMAKE_COMMAND} -E remove_directory ${RM_RESOURCES_OUT}/font
            COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
                    --icon=${CMAKE_CURRENT_SOURCE_DIR}/resources/img/icon.jpg
                    --nacp=${PROJECT_NAME}.nacp --romfsdir=${RM_RESOURCES_OUT}
            VERBATIM
        )
    endif ()
endif ()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if (RM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()

cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Build switches (see CMakePresets.json for the supported combinations)
#   RM_BUILD_APP    Borealis application (needs -DPLATFORM_DESKTOP=ON or -DPLATFORM_SWITCH=ON)
#   RM_BUILD_TESTS  GoogleTest suite, desktop only; can be built without the app
# ---------------------------------------------------------------------------
option(RM_BUILD_APP "Build the Borealis application" ON)
option(RM_BUILD_TESTS "Build the unit tests (desktop only)" OFF)

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
    src/core/Result.cpp
    src/platform/common/IFileSystem.cpp
    src/platform/common/LocalFileSystem.cpp
    src/platform/common/VirtualPath.cpp
)
target_include_directories(retromanager_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(retromanager_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(retromanager_core PUBLIC RM_VERSION="${PROJECT_VERSION}")
target_compile_options(retromanager_core PRIVATE ${RM_WARNINGS})

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
        src/ui/HomeActivity.cpp
        ${RM_PLATFORM_SRC}
    )
    if (PLATFORM_SWITCH)
        list(APPEND RM_APP_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
    endif ()

    program_target(${PROJECT_NAME} "${RM_APP_SRC}")
    target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
    target_link_libraries(${PROJECT_NAME} PRIVATE retromanager_core borealis ${APP_PLATFORM_LIB})
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

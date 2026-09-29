# Third-party libraries shared by every target (included after project()).

include(FetchContent)

# nlohmann/json: header-only, used by the parsers (repository indexes,
# app config). Available to core now so the dependency is wired on both
# platforms before Phase 2 needs it.
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# stb (header-only, no CMake project): stb_image decodes the box art,
# stb_image_resize2 scales it and stb_image_write saves the forwarder icon as
# JPEG. Pinned to a commit: stb has no release tags. The implementations are
# compiled once, as static functions (src/forwarder/StbImage.cpp), so they
# never clash with the stb_image copy inside Borealis' nanovg.
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20
)
FetchContent_MakeAvailable(stb)
add_library(rm_stb INTERFACE)
target_include_directories(rm_stb SYSTEM INTERFACE ${stb_SOURCE_DIR})

# libsmb2: SMB2/3 client (LGPL-2.1, static). libcurl only speaks SMB1,
# which Samba (NAS such as ZimaOS, Synology...) disables by default. Built
# without Kerberos/GSSAPI (NTLM user/password is what home NAS use) and
# without the DCE/RPC library. Supports the Switch through the devkitPro
# toolchain. Pinned to a commit (the project tags rarely).
if (RM_WITH_SMB)
    set(ENABLE_LIBKRB5 OFF CACHE BOOL "" FORCE)
    set(ENABLE_GSSAPI OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBDCERPC OFF CACHE BOOL "" FORCE)
    set(ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libsmb2
        GIT_REPOSITORY https://github.com/sahlberg/libsmb2.git
        GIT_TAG c796ab389328d597ea2135717092d3802015f91f
    )
    FetchContent_MakeAvailable(libsmb2)
    if (PLATFORM_SWITCH)
        # Its Switch code paths (compat.c/.h, portable-endian.h) key on
        # __SWITCH__, which Borealis only defines for its own targets.
        target_compile_definitions(smb2 PRIVATE __SWITCH__)
        # Its Switch compat.h only includes <sys/types.h>, yet aes.h and
        # others use uint8_t: provide <stdint.h> to every libsmb2 source.
        target_compile_options(smb2 PRIVATE -include stdint.h)
    endif ()
endif ()

if (RM_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # MSVC
    FetchContent_MakeAvailable(googletest)
endif ()

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

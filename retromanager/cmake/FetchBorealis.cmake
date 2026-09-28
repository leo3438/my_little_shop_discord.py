# Fetches Borealis and applies its platform toolchain.
#
# Must be included BEFORE project(): on Switch, Borealis' toolchain.cmake
# loads devkitPro's Switch.cmake, which selects the aarch64 compilers.
#
# Override the checkout with -DFETCHCONTENT_SOURCE_DIR_BOREALIS=/path/to/borealis
# to hack on Borealis locally.

include(FetchContent)

# FetchContent_Populate() is the only way to get the sources without
# add_subdirectory()'ing Borealis' root (which builds its demo app).
if (POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif ()

set(RM_BOREALIS_REPOSITORY "https://github.com/xfangfang/borealis.git" CACHE STRING "Borealis git repository")
# Pinned for reproducible builds. Bump deliberately, then rebuild both targets.
set(RM_BOREALIS_TAG "5f08b286f3df737f3321d2247a6fe633fcead03c" CACHE STRING "Borealis commit")

if (PLATFORM_DESKTOP)
    # Desktop builds GLFW from Borealis' submodule; skip the (huge) SDL one.
    FetchContent_Declare(borealis
        GIT_REPOSITORY ${RM_BOREALIS_REPOSITORY}
        GIT_TAG ${RM_BOREALIS_TAG}
        GIT_SUBMODULES library/lib/extern/glfw
    )
else ()
    # Switch uses GLFW from devkitPro portlibs: no submodule needed.
    FetchContent_Declare(borealis
        GIT_REPOSITORY ${RM_BOREALIS_REPOSITORY}
        GIT_TAG ${RM_BOREALIS_TAG}
        GIT_SUBMODULES ""
    )
endif ()

FetchContent_GetProperties(borealis)
if (NOT borealis_POPULATED)
    message(STATUS "Fetching Borealis ${RM_BOREALIS_TAG}")
    FetchContent_Populate(borealis)
endif ()

set(BOREALIS_LIBRARY ${borealis_SOURCE_DIR}/library)
set(BOREALIS_RESOURCES ${borealis_SOURCE_DIR}/resources)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)

include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# Test doubles: in-memory filesystem and the fixture SD card loader.
add_library(retromanager_mocks STATIC
    mocks/MemoryFileSystem.cpp
    mocks/MockSdCard.cpp
)
target_include_directories(retromanager_mocks PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
target_link_libraries(retromanager_mocks PUBLIC retromanager_core)
target_compile_definitions(retromanager_mocks PUBLIC
    RM_FIXTURE_SD_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sd_card")
target_compile_options(retromanager_mocks PRIVATE ${RM_WARNINGS})

# One binary for all unit tests; ctest sees each test case individually.
file(GLOB_RECURSE RM_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp
)
if (NOT RM_WITH_CURL)  # everything that needs the libcurl clients
    list(FILTER RM_TEST_SOURCES EXCLUDE REGEX
         "/(Ftp[A-Za-z]*|Http[A-Za-z]*|CloudSyncIntegration|SourceFactory|SourceCatalog)Test\\.cpp$")
endif ()
if (NOT RM_WITH_SMB)
    list(FILTER RM_TEST_SOURCES EXCLUDE REGEX "/Smb[A-Za-z]*Test\\.cpp$")
endif ()
add_executable(retromanager_tests ${RM_TEST_SOURCES})
target_link_libraries(retromanager_tests PRIVATE retromanager_mocks GTest::gtest_main GTest::gmock)
if (RM_WITH_CURL)
    target_link_libraries(retromanager_tests PRIVATE retromanager_curl)
endif ()
if (RM_WITH_SMB)
    target_link_libraries(retromanager_tests PRIVATE retromanager_smb)
endif ()
target_compile_options(retromanager_tests PRIVATE ${RM_WARNINGS})

include(GoogleTest)
gtest_discover_tests(retromanager_tests DISCOVERY_TIMEOUT 30)

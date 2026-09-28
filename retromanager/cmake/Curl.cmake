# Provides the CURL::libcurl target.
#
# - Switch: libcurl from devkitPro portlibs (switch-curl), via pkg-config.
# - Desktop: the system libcurl when available (libcurl4-openssl-dev,
#   Homebrew, vcpkg...), otherwise a minimal static libcurl built from source
#   with FetchContent (FTP/FTPS/HTTP/HTTPS only).

if (TARGET CURL::libcurl)
    return()
endif ()

if (PLATFORM_SWITCH)
    # Portlibs are static: link the Libs.private closure (mbedtls, zlib...),
    # which IMPORTED_TARGET would drop. libnx comes last because libcurl's
    # socket calls resolve into it.
    add_library(rm_curl_switch INTERFACE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RM_CURL QUIET libcurl)
    if (RM_CURL_FOUND)
        target_include_directories(rm_curl_switch INTERFACE ${RM_CURL_STATIC_INCLUDE_DIRS})
        target_link_directories(rm_curl_switch INTERFACE ${RM_CURL_STATIC_LIBRARY_DIRS})
        target_link_libraries(rm_curl_switch INTERFACE ${RM_CURL_STATIC_LIBRARIES} nx)
        message(STATUS "libcurl: devkitPro portlib ${RM_CURL_VERSION} (${RM_CURL_STATIC_LIBRARIES})")
    else ()
        find_library(RM_CURL_LIBRARY curl PATHS ${DEVKITPRO}/portlibs/switch/lib NO_DEFAULT_PATH)
        if (NOT RM_CURL_LIBRARY)
            message(FATAL_ERROR "libcurl not found: install it with 'dkp-pacman -S switch-curl'")
        endif ()
        target_include_directories(rm_curl_switch INTERFACE ${DEVKITPRO}/portlibs/switch/include)
        target_link_libraries(rm_curl_switch INTERFACE ${RM_CURL_LIBRARY} mbedtls mbedx509 mbedcrypto z nx)
        message(STATUS "libcurl: devkitPro portlib (no pkg-config) ${RM_CURL_LIBRARY}")
    endif ()
    add_library(CURL::libcurl ALIAS rm_curl_switch)
    return()
endif ()

option(RM_FETCH_CURL "Always build libcurl from source instead of using the system one" OFF)

if (NOT RM_FETCH_CURL)
    find_package(CURL QUIET)
endif ()

if (CURL_FOUND AND TARGET CURL::libcurl)
    message(STATUS "libcurl: system ${CURL_VERSION_STRING}")
    return()
endif ()

message(STATUS "libcurl: not found on the system, building it from source")
include(FetchContent)

find_package(OpenSSL QUIET)
if (OPENSSL_FOUND)
    set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
else ()
    message(WARNING "OpenSSL not found: libcurl is built without TLS (no FTPS/HTTPS)")
    set(CURL_ENABLE_SSL OFF CACHE BOOL "" FORCE)
endif ()

# Keep the build small and dependency-free: we only need FTP(S) and HTTP(S).
foreach (option
        BUILD_CURL_EXE BUILD_SHARED_LIBS BUILD_TESTING BUILD_LIBCURL_DOCS BUILD_MISC_DOCS ENABLE_CURL_MANUAL
        CURL_USE_LIBPSL CURL_USE_LIBSSH2 CURL_USE_LIBSSH CURL_USE_GSSAPI CURL_BROTLI CURL_ZSTD USE_NGHTTP2
        USE_LIBIDN2 USE_LIBRTMP ENABLE_ARES CURL_DISABLE_INSTALL_PLACEHOLDER)
    set(${option} OFF CACHE BOOL "" FORCE)
endforeach ()
foreach (protocol DICT FILE GOPHER IMAP LDAP LDAPS MQTT POP3 RTSP SMB SMTP TELNET TFTP)
    set(CURL_DISABLE_${protocol} ON CACHE BOOL "" FORCE)
endforeach ()
set(CURL_ZLIB OFF CACHE STRING "" FORCE)

FetchContent_Declare(curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_10_1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(curl)

if (NOT TARGET CURL::libcurl)
    add_library(CURL::libcurl ALIAS libcurl_static)
endif ()

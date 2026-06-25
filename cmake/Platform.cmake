set(PLATFORM_LIBS "")
set(FMWRAPPER_LIB "")
set(OPENSSL_STATIC_LIBS "")   # populated per-platform below
set(OPENSSL_INCLUDE "")

set(SDK_LIBS    "${CMAKE_SOURCE_DIR}/sdk/Libraries")
set(VENDOR_ROOT "${CMAKE_SOURCE_DIR}/third_party")

if(APPLE)
    # Universal binary (x86_64 + arm64) is required for distribution but needs
    # a universal OpenSSL. Pass -DUNIVERSAL=ON only when you have one available
    # (e.g. built via `brew install openssl` on an Intel Mac, or via lipo).
    # Default: native architecture only (fast development builds).
    if(UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)

    find_library(CORE_FOUNDATION CoreFoundation REQUIRED)
    list(APPEND PLATFORM_LIBS ${CORE_FOUNDATION})

    # Weak-link FMWrapper so DYLD doesn't abort if it can't find it via rpath —
    # symbols are resolved from the already-loaded FMWrapper inside FileMaker.
    # Use a cache variable so CMakeLists.txt can call target_link_options with it.
    set(FMWRAPPER_SEARCH_PATH "${SDK_LIBS}/Mac" CACHE INTERNAL "")
    set(FMWRAPPER_LIB "")   # handled via target_link_options in CMakeLists.txt

    # Bundled static OpenSSL (universal arm64+x86_64, built by build-openssl-mac.sh)
    set(_OPENSSL_MAC "${VENDOR_ROOT}/openssl/mac")
    if(EXISTS "${_OPENSSL_MAC}/lib/libssl.a")
        set(OPENSSL_INCLUDE    "${_OPENSSL_MAC}/include")
        set(OPENSSL_STATIC_LIBS
            "${_OPENSSL_MAC}/lib/libssl.a"
            "${_OPENSSL_MAC}/lib/libcrypto.a"
        )
        # Tell rabbitmq-c's find_package(OpenSSL) to use our static copies
        set(OPENSSL_ROOT_DIR        "${_OPENSSL_MAC}"              CACHE PATH "" FORCE)
        set(OPENSSL_INCLUDE_DIR     "${_OPENSSL_MAC}/include"      CACHE PATH "" FORCE)
        set(OPENSSL_SSL_LIBRARY     "${_OPENSSL_MAC}/lib/libssl.a" CACHE FILEPATH "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY  "${_OPENSSL_MAC}/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
        set(OPENSSL_USE_STATIC_LIBS TRUE                           CACHE BOOL "" FORCE)
        message(STATUS "OpenSSL: bundled static (${_OPENSSL_MAC})")
    else()
        message(STATUS "OpenSSL: not found in ${_OPENSSL_MAC} — SSL will be disabled. Run build-openssl-mac.sh to enable it.")
    endif()

elseif(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)   # Windows 10+
    # advapi32 is needed by static OpenSSL on Windows
    list(APPEND PLATFORM_LIBS ws2_32 crypt32 advapi32)

    set(FMWRAPPER_LIB "${SDK_LIBS}/Win/x64/FMWrapper.lib")

    set(_OPENSSL_WIN "${VENDOR_ROOT}/openssl/win/x64")
    if(EXISTS "${_OPENSSL_WIN}/lib/libssl.lib")
        set(OPENSSL_INCLUDE   "${_OPENSSL_WIN}/include")
        set(OPENSSL_STATIC_LIBS
            "${_OPENSSL_WIN}/lib/libssl.lib"
            "${_OPENSSL_WIN}/lib/libcrypto.lib"
        )
        set(OPENSSL_ROOT_DIR       "${_OPENSSL_WIN}"              CACHE PATH "" FORCE)
        set(OPENSSL_INCLUDE_DIR    "${_OPENSSL_WIN}/include"      CACHE PATH "" FORCE)
        set(OPENSSL_SSL_LIBRARY    "${_OPENSSL_WIN}/lib/libssl.lib" CACHE FILEPATH "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_WIN}/lib/libcrypto.lib" CACHE FILEPATH "" FORCE)
        set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
        message(STATUS "OpenSSL: bundled static (${_OPENSSL_WIN})")
    else()
        message(STATUS "OpenSSL: not found in ${_OPENSSL_WIN} — SSL will be disabled.")
    endif()

elseif(UNIX)
    find_package(Threads REQUIRED)
    list(APPEND PLATFORM_LIBS Threads::Threads dl)  # dl needed for dladdr in CACert.cpp

    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(_LINUX_ARCH "arm64")
    else()
        set(_LINUX_ARCH "x64")
    endif()

    # Detect Ubuntu version
    execute_process(
        COMMAND lsb_release -r
        OUTPUT_VARIABLE _LSB_RELEASE_TMP
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REGEX MATCH "([0-9]+)\\.[0-9]+" _ "${_LSB_RELEASE_TMP}")
    set(_UBUNTU_MAJOR "${CMAKE_MATCH_1}")
    if(_UBUNTU_MAJOR STREQUAL "24")
        set(_U_PLATFORM "U24")
    else()
        set(_U_PLATFORM "U22")
    endif()

    set(_OPENSSL_LINUX "${VENDOR_ROOT}/openssl/linux/${_U_PLATFORM}/${_LINUX_ARCH}")
    if(EXISTS "${_OPENSSL_LINUX}/lib/libssl.a")
        set(OPENSSL_INCLUDE   "${_OPENSSL_LINUX}/include")
        set(OPENSSL_STATIC_LIBS
            "${_OPENSSL_LINUX}/lib/libssl.a"
            "${_OPENSSL_LINUX}/lib/libcrypto.a"
        )
        set(OPENSSL_ROOT_DIR       "${_OPENSSL_LINUX}"              CACHE PATH "" FORCE)
        set(OPENSSL_INCLUDE_DIR    "${_OPENSSL_LINUX}/include"      CACHE PATH "" FORCE)
        set(OPENSSL_SSL_LIBRARY    "${_OPENSSL_LINUX}/lib/libssl.a" CACHE FILEPATH "" FORCE)
        set(OPENSSL_CRYPTO_LIBRARY "${_OPENSSL_LINUX}/lib/libcrypto.a" CACHE FILEPATH "" FORCE)
        set(OPENSSL_USE_STATIC_LIBS TRUE CACHE BOOL "" FORCE)
        message(STATUS "OpenSSL: bundled static (${_OPENSSL_LINUX})")
    else()
        message(STATUS "OpenSSL: not found in ${_OPENSSL_LINUX} — SSL will be disabled.")
    endif()

    set(FMWRAPPER_LIB "${SDK_LIBS}/Linux/${_U_PLATFORM}/${_LINUX_ARCH}/libFMWrapper.so")
    message(STATUS "FMWrapper: ${FMWRAPPER_LIB}")
endif()

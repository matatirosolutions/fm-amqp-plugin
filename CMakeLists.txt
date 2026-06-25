cmake_minimum_required(VERSION 3.21)
project(AMQPFMPlugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Platform detection & FMWrapper SDK ──────────────────────────────────────
include(cmake/Platform.cmake)

# ── Dependencies (rabbitmq-c + SimpleAmqpClient) ────────────────────────────
include(cmake/FetchDependencies.cmake)

# ── Plugin target ───────────────────────────────────────────────────────────
set(PLUGIN_SOURCES
    src/Plugin.cpp
    src/ConnectionManager.cpp
    src/Functions.cpp
    src/TextUtil.cpp
    src/CACert.cpp
)

set(CACERT_FILE "${CMAKE_SOURCE_DIR}/third_party/cacert/cacert.pem")

add_library(AMQPFMPlugin MODULE ${PLUGIN_SOURCES})

target_include_directories(AMQPFMPlugin PRIVATE
    src
    sdk/FMWrapper   # sdk/FMWrapper/FMWrapper/*.h — include as "FMWrapper/FMXExtern.h"
    ${rabbitmq-c_SOURCE_DIR}/include
    ${rabbitmq-c_BINARY_DIR}/include       # generated export.h
    ${rabbitmq-c_BINARY_DIR}/librabbitmq   # generated amqp_framing.h
)

if(OPENSSL_INCLUDE)
    target_include_directories(AMQPFMPlugin PRIVATE ${OPENSSL_INCLUDE})
endif()

target_link_libraries(AMQPFMPlugin PRIVATE
    rabbitmq-static
    ${FMWRAPPER_LIB}
    ${PLATFORM_LIBS}
)

if(OPENSSL_STATIC_LIBS)
    target_link_libraries(AMQPFMPlugin PRIVATE ${OPENSSL_STATIC_LIBS})
    target_compile_definitions(AMQPFMPlugin PRIVATE AMQP_HAS_SSL)
endif()

target_compile_definitions(AMQPFMPlugin PRIVATE
    PLUGIN_VERSION_STRING="${PROJECT_VERSION}"
)

if(WIN32)
    # Feed version numbers into the .rc file as comma-separated and string forms
    string(REPLACE "." "," _VERSION_COMMA "${PROJECT_VERSION}.0")
    target_compile_definitions(AMQPFMPlugin PRIVATE
        PLUGIN_VERSION_COMMA=${_VERSION_COMMA}
        PLUGIN_VERSION_STR="${PROJECT_VERSION}.0\\0"
    )
endif()

# ── Platform-specific output naming ─────────────────────────────────────────
if(APPLE)
    set_target_properties(AMQPFMPlugin PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "fmplugin"
        OUTPUT_NAME "AMQPFMPlugin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/mac/Info.plist"
        BUILD_RPATH "@loader_path/../Frameworks"
        INSTALL_RPATH "@loader_path/../Frameworks"
    )
    # Weak-link FMWrapper: DYLD won't abort if it can't find it via rpath;
    # symbols resolve from the already-loaded FMWrapper inside FileMaker.
    target_link_options(AMQPFMPlugin PRIVATE
        "-F${FMWRAPPER_SEARCH_PATH}"
        "-weak_framework" "FMWrapper"
    )
    # PkgInfo is required for FileMaker to recognise the bundle type (FMXT + plugin ID)
    add_custom_command(TARGET AMQPFMPlugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/resources/mac/PkgInfo"
            "$<TARGET_BUNDLE_DIR:AMQPFMPlugin>/Contents/PkgInfo"
        COMMENT "Copying PkgInfo into bundle"
    )
    # Bundle the Mozilla CA bundle into Contents/Resources/
    if(EXISTS "${CACERT_FILE}")
        add_custom_command(TARGET AMQPFMPlugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:AMQPFMPlugin>/Contents/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CACERT_FILE}"
                "$<TARGET_BUNDLE_DIR:AMQPFMPlugin>/Contents/Resources/cacert.pem"
            COMMENT "Bundling cacert.pem into bundle resources"
        )
    endif()
    # Sign the bundle. CODESIGN_IDENTITY can be overridden at cmake time:
    #   -DCODESIGN_IDENTITY="Developer ID Application: ..."  (distribution)
    #   -DCODESIGN_IDENTITY="-"                              (ad-hoc, default)
    if(NOT DEFINED CODESIGN_IDENTITY)
        set(CODESIGN_IDENTITY "-")
    endif()
    add_custom_command(TARGET AMQPFMPlugin POST_BUILD
        COMMAND codesign --deep --force --options runtime
            --sign "${CODESIGN_IDENTITY}"
            "$<TARGET_BUNDLE_DIR:AMQPFMPlugin>"
        COMMENT "Signing bundle (${CODESIGN_IDENTITY})"
        VERBATIM
    )
elseif(WIN32)
    set_target_properties(AMQPFMPlugin PROPERTIES
        OUTPUT_NAME "AMQPFMPlugin"
        SUFFIX ".fmx64"
        PREFIX ""
    )
    target_sources(AMQPFMPlugin PRIVATE resources/win/Plugin.rc)
    # Copy cacert.pem next to the .fmx64 so it can be found at runtime
    if(EXISTS "${CACERT_FILE}")
        add_custom_command(TARGET AMQPFMPlugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CACERT_FILE}"
                "$<TARGET_FILE_DIR:AMQPFMPlugin>/cacert.pem"
            COMMENT "Copying cacert.pem next to plugin"
        )
    endif()
elseif(UNIX)
    # FMWrapper is already loaded by FileMaker Server when the plugin is dlopen'd,
    # so the dynamic linker finds it without a hard rpath. We list the known FMS
    # install paths as a fallback for any edge cases (e.g. running outside FMS).
    set_target_properties(AMQPFMPlugin PROPERTIES
        OUTPUT_NAME "AMQPFMPlugin"
        SUFFIX ".fmx"
        PREFIX ""
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH
            "$ORIGIN:\
/opt/FileMaker/FileMaker Server/Database Server/:\
/opt/FileMaker/FileMaker Server/Web Publishing/:\
/opt/FileMaker/lib"
    )
    # Copy cacert.pem next to the .fmx so it can be found at runtime
    if(EXISTS "${CACERT_FILE}")
        add_custom_command(TARGET AMQPFMPlugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CACERT_FILE}"
                "$<TARGET_FILE_DIR:AMQPFMPlugin>/cacert.pem"
            COMMENT "Copying cacert.pem next to plugin"
        )
    endif()
endif()

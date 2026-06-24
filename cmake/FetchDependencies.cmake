include(FetchContent)

# ── rabbitmq-c ───────────────────────────────────────────────────────────────
# Used directly (no SimpleAmqpClient wrapper) to avoid a Boost dependency.
FetchContent_Declare(rabbitmq-c
    GIT_REPOSITORY https://github.com/alanxz/rabbitmq-c.git
    GIT_TAG        v0.14.0
    GIT_SHALLOW    TRUE
)

set(BUILD_SHARED_LIBS              OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON  CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES        OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS           OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS           OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS_DOCS      OFF CACHE BOOL "" FORCE)
# Enable SSL only when bundled static OpenSSL libs are present (set by Platform.cmake).
if(OPENSSL_STATIC_LIBS)
    set(ENABLE_SSL_SUPPORT ON  CACHE BOOL "" FORCE)
else()
    set(ENABLE_SSL_SUPPORT OFF CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(rabbitmq-c)

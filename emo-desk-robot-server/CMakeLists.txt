cmake_minimum_required(VERSION 3.10)
project(emo-desk-robot-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(DISABLE_WERROR ON CACHE BOOL "Do not treat libwebsockets warnings as errors" FORCE)
set(LWS_WITH_STATIC ON CACHE BOOL "Build libwebsockets static library" FORCE)
set(LWS_WITH_SHARED OFF CACHE BOOL "Do not build libwebsockets shared library" FORCE)
set(LWS_WITH_SSL OFF CACHE BOOL "Build libwebsockets without SSL dependency" FORCE)
set(LWS_WITH_TLS OFF CACHE BOOL "Build libwebsockets without TLS support" FORCE)
set(LWS_WITH_SCHANNEL OFF CACHE BOOL "Do not use Windows SChannel in libwebsockets" FORCE)
set(LWS_WITH_TLS_SESSIONS OFF CACHE BOOL "Build libwebsockets without TLS sessions" FORCE)
set(LWS_WITH_HTTP2 OFF CACHE BOOL "Build libwebsockets without HTTP/2 support" FORCE)
set(LWS_ROLE_H2 OFF CACHE BOOL "Build libwebsockets without HTTP/2 role" FORCE)
set(LWS_ROLE_H3 OFF CACHE BOOL "Build libwebsockets without HTTP/3 role" FORCE)
set(LWS_ROLE_QUIC OFF CACHE BOOL "Build libwebsockets without QUIC role" FORCE)
set(LWS_WITH_MINIMAL_EXAMPLES OFF CACHE BOOL "Do not build libwebsockets examples" FORCE)
set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "Do not build libwebsockets test apps" FORCE)
set(LWS_WITHOUT_TEST_SERVER ON CACHE BOOL "Do not build libwebsockets test server" FORCE)
set(LWS_WITHOUT_TEST_CLIENT ON CACHE BOOL "Do not build libwebsockets test client" FORCE)
set(LWS_WITHOUT_TEST_PING ON CACHE BOOL "Do not build libwebsockets ping test" FORCE)
set(LWS_WITH_SECURE_STREAMS OFF CACHE BOOL "Build libwebsockets without secure streams" FORCE)
set(LWS_WITH_SYS_STATE OFF CACHE BOOL "Build libwebsockets without system state" FORCE)
set(LWS_WITH_SYS_SMD OFF CACHE BOOL "Build libwebsockets without system message distribution" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "Do not build opus tests" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "Do not build opus tools" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "Do not install opus pkg-config module" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "Do not install opus CMake config module" FORCE)

add_subdirectory(libwebsockets)
add_subdirectory(opus)

add_executable(${PROJECT_NAME}
    main.cpp
    audio_codec.cpp
    config.cpp
    llm_client.cpp
    mimo_voice_client.cpp
    server.cpp
    ws_voice_server.cpp
    cJSON/cJSON.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
    .
    cJSON/include
    dr_libs
    opus/include
    spdlog/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE websockets opus)

if(WIN32)
    # MinGW/MSVC on Windows
    target_link_libraries(${PROJECT_NAME} PRIVATE winhttp ws2_32)
else()
    # Linux/macOS
    find_package(OpenSSL REQUIRED)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        pthread
        OpenSSL::SSL
        OpenSSL::Crypto
    )
endif()

cmake_minimum_required(VERSION 3.16)
project(zasca-guard VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(zasca-guard
    src/main.cpp
    src/logger.cpp
    src/utils.cpp
    src/security.cpp
    src/process.cpp
    src/http_client.cpp
    src/json_parser.cpp
    src/uv_installer.cpp
    src/updater.cpp
    src/win_service.cpp
    src/initializer.cpp
    resources/resource.rc
)

target_include_directories(zasca-guard PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/resources
)

target_link_libraries(zasca-guard PRIVATE
    advapi32
    user32
    shell32
    ole32
    oleaut32
    winhttp
)

if(MSVC)
    target_compile_options(zasca-guard PRIVATE /EHsc /W3)
    target_compile_definitions(zasca-guard PRIVATE
        _CRT_SECURE_NO_WARNINGS
        $<$<CONFIG:Release>:NDEBUG>
    )
endif()

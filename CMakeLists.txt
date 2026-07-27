cmake_minimum_required(VERSION 3.21.1)

project(Flick VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Widgets)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XCB QUIET IMPORTED_TARGET xcb)
    endif()
endif()

qt_standard_project_setup()

qt_add_executable(flick
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_services.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_services.h
)

target_link_libraries(flick PRIVATE Qt6::Concurrent Qt6::Widgets)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(flick PRIVATE Qt6::DBus)
endif()
if(TARGET PkgConfig::XCB)
    target_link_libraries(flick PRIVATE PkgConfig::XCB)
    target_compile_definitions(flick PRIVATE FLICK_HAVE_XCB_COLOR_PROFILE)
endif()
target_compile_options(flick PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    target_compile_definitions(flick PRIVATE FLICK_ENABLE_TEST_HARNESS)
endif()

install(TARGETS flick RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

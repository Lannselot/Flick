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

function(add_flick_executable target)
    qt_add_executable(${target}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_services.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_services.h
    )
    target_link_libraries(${target} PRIVATE Qt6::Concurrent Qt6::Widgets)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${target} PRIVATE Qt6::DBus)
    endif()
    if(TARGET PkgConfig::XCB)
        target_link_libraries(${target} PRIVATE PkgConfig::XCB)
        target_compile_definitions(${target} PRIVATE FLICK_HAVE_XCB_COLOR_PROFILE)
    endif()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_flick_executable(flick)
if(BUILD_TESTING)
    add_flick_executable(flick_test_driver)
    target_compile_definitions(flick_test_driver PRIVATE FLICK_ENABLE_TEST_HARNESS)
endif()

install(TARGETS flick RUNTIME DESTINATION bin)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(
        FILES packaging/linux/org.flick.Flick.desktop
        DESTINATION share/applications
    )
    install(
        FILES packaging/linux/org.flick.Flick.metainfo.xml
        DESTINATION share/metainfo
    )
    install(
        FILES packaging/linux/org.flick.Flick.svg
        DESTINATION share/icons/hicolor/scalable/apps
    )
    install(
        FILES LICENSE
        DESTINATION share/licenses/Flick
    )
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

cmake_minimum_required(VERSION 3.21.1)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum supported macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS release architectures")
    endif()
endif()

project(Flick VERSION 0.1.2 LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

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
    set(options)
    if(APPLE AND target STREQUAL "flick")
        list(APPEND options MACOSX_BUNDLE)
    endif()
    qt_add_executable(${target} ${options}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewer_window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewer_window.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/flick_application.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/flick_application.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/browsing_sequence.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/browsing_sequence.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/image_loading.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/image_loading.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_services.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_services.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewing_surface.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/viewing_surface.h
    )
    target_link_libraries(${target} PRIVATE Qt6::Concurrent Qt6::Widgets)
    target_compile_definitions(${target} PRIVATE FLICK_VERSION="${PROJECT_VERSION}")
    if(APPLE)
        target_sources(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/platform_services_macos.mm
        )
        target_link_libraries(${target} PRIVATE "-framework AppKit")
    endif()
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
if(APPLE)
    set_source_files_properties(packaging/macos/Flick.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    target_sources(flick PRIVATE packaging/macos/Flick.icns)
    set_target_properties(flick PROPERTIES
        OUTPUT_NAME Flick
        MACOSX_BUNDLE_BUNDLE_NAME Flick
        MACOSX_BUNDLE_GUI_IDENTIFIER org.flick.Flick
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
    )
endif()
if(BUILD_TESTING)
    add_flick_executable(flick_test_driver)
    target_compile_definitions(flick_test_driver PRIVATE FLICK_ENABLE_TEST_HARNESS)
endif()

if(APPLE)
    install(TARGETS flick BUNDLE DESTINATION .)
    qt_generate_deploy_app_script(
        TARGET flick
        OUTPUT_SCRIPT flick_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${flick_deploy_script})
else()
    install(TARGETS flick RUNTIME DESTINATION bin)
endif()
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

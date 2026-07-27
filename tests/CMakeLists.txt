find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

qt_add_executable(flick_application_test
    ${CMAKE_CURRENT_SOURCE_DIR}/flick_application_test.cpp
)

target_link_libraries(flick_application_test PRIVATE Qt6::Gui Qt6::Test)
target_compile_options(flick_application_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(flick_application_test PRIVATE
    FLICK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME flick.application COMMAND flick_application_test)
set_tests_properties(flick.application PROPERTIES
    ENVIRONMENT "FLICK_EXECUTABLE=$<TARGET_FILE:flick_test_driver>;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)

qt_add_executable(flick_performance
    ${CMAKE_CURRENT_SOURCE_DIR}/flick_performance.cpp
)
target_link_libraries(flick_performance PRIVATE Qt6::Gui)
target_compile_options(flick_performance PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME flick.performance-smoke COMMAND flick_performance --smoke)
set_tests_properties(flick.performance-smoke PROPERTIES
    ENVIRONMENT "FLICK_EXECUTABLE=$<TARGET_FILE:flick_test_driver>;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
)

add_test(
    NAME flick.github-workflows
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/github_workflows_test.py
        ${PROJECT_SOURCE_DIR}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(BASH_EXECUTABLE bash REQUIRED)
    add_test(
        NAME flick.network-trace-filter
        COMMAND ${BASH_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/network_trace_filter_test.sh
            ${PROJECT_SOURCE_DIR}
    )
    add_test(
        NAME flick.no-network-dependencies
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DBINARY=$<TARGET_FILE:flick>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/no_network_dependencies_test.cmake
    )
    add_test(
        NAME flick.linux-desktop-metadata
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/linux_desktop_metadata_test.cmake
    )
    add_test(
        NAME flick.linux-release-layout
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DBINARY_DIR=${PROJECT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/linux_release_layout_test.cmake
    )
endif()

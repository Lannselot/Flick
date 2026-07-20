find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Test)

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
    ENVIRONMENT "FLICK_EXECUTABLE=$<TARGET_FILE:flick>;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 20
)

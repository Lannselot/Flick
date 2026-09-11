if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/docs/architecture.md" architecture_documentation)
foreach(required_owner IN ITEMS
        "ImageInformation::Dialog"
        "Settings::Editor"
        "ProcessTestAdapter"
        "ViewerWindowTestControl")
    if(NOT architecture_documentation MATCHES "${required_owner}")
        message(FATAL_ERROR "Architecture documentation omits final owner: ${required_owner}")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/src/viewer_window_test_control.h" test_control)
if(test_control MATCHES "QByteArray")
    message(FATAL_ERROR "Window test seam exposes protocol-serialized values")
endif()
if(test_control MATCHES "QWidget[ \t]*\\*|QAction[ \t]*\\*|QAbstractButton[ \t]*\\*")
    message(FATAL_ERROR "Window test seam exposes widget or control pointers")
endif()

file(READ "${SOURCE_DIR}/src/settings_editor.h" settings_editor)
if(settings_editor MATCHES "parseTest|describeTest|dialogStructure|dialogFocusOrder")
    message(FATAL_ERROR "Settings owns test protocol parsing or serialization")
endif()

file(READ "${SOURCE_DIR}/src/process_test_adapter.cpp" adapter)
foreach(protocol_owner IN ITEMS "startsWith" "fprintf" "parseSettings" "describeSettings"
                                "describePresentation" "describeActions")
    if(NOT adapter MATCHES "${protocol_owner}")
        message(FATAL_ERROR "Process adapter does not own protocol operation: ${protocol_owner}")
    endif()
endforeach()

file(GLOB production_sources "${SOURCE_DIR}/src/*.cpp" "${SOURCE_DIR}/src/*.h")
foreach(source IN LISTS production_sources)
    if(source MATCHES "process_test_adapter\\.(cpp|h)$")
        continue()
    endif()
    file(READ "${source}" contents)
    if(contents MATCHES "QByteArray[ \t\n]+[A-Za-z0-9_]*(State|Description|Structure|FocusOrder|MotionContract|Transition)\\(")
        message(FATAL_ERROR "Protocol-shaped QByteArray description outside adapter: ${source}")
    endif()
    if(contents MATCHES "(triggerAction|focusDetails|focusSkip|toggleDetails)\\(")
        message(FATAL_ERROR "Widget-named test operation outside adapter: ${source}")
    endif()
    if(NOT source MATCHES "test_driver_main\\.cpp$" AND contents MATCHES "FLICK_TEST_")
        message(FATAL_ERROR "Deterministic environment parsing outside test-driver wiring: ${source}")
    endif()
endforeach()

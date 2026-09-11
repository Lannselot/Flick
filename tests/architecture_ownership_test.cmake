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

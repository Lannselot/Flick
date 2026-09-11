if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/.scratch/flick-ux/issues/06-cross-platform-ux-validation.md"
     ux_validation_ticket)
if(NOT ux_validation_ticket MATCHES "\\*\\*Status:\\*\\* ready-for-human")
    message(FATAL_ERROR "Native UX validation ticket must remain open for human validation")
endif()

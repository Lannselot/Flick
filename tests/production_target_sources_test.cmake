if(NOT DEFINED PRODUCTION_SOURCES OR NOT DEFINED DRIVER_SOURCES OR
   NOT DEFINED TEST_ADAPTER_SOURCE)
    message(FATAL_ERROR "Production, driver, and test adapter sources are required")
endif()

list(FIND PRODUCTION_SOURCES "${TEST_ADAPTER_SOURCE}" adapter_index)
if(NOT adapter_index EQUAL -1)
    message(FATAL_ERROR "Production target includes the process test adapter")
endif()

list(FIND DRIVER_SOURCES "${TEST_ADAPTER_SOURCE}" adapter_index)
if(adapter_index EQUAL -1)
    message(FATAL_ERROR "Test driver does not include the process test adapter")
endif()

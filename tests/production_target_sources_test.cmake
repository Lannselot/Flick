if(NOT DEFINED PRODUCTION_SOURCES OR NOT DEFINED DRIVER_SOURCES OR
   NOT DEFINED TEST_ADAPTER_SOURCE OR NOT DEFINED PRODUCTION_BINARY OR
   NOT DEFINED NM_EXECUTABLE)
    message(FATAL_ERROR "Production sources, binary, driver, and test adapter are required")
endif()

list(FIND PRODUCTION_SOURCES "${TEST_ADAPTER_SOURCE}" adapter_index)
if(NOT adapter_index EQUAL -1)
    message(FATAL_ERROR "Production target includes the process test adapter")
endif()

list(FIND DRIVER_SOURCES "${TEST_ADAPTER_SOURCE}" adapter_index)
if(adapter_index EQUAL -1)
    message(FATAL_ERROR "Test driver does not include the process test adapter")
endif()

execute_process(
    COMMAND "${NM_EXECUTABLE}" -C "${PRODUCTION_BINARY}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE production_symbols
    ERROR_VARIABLE nm_error
)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect production symbols: ${nm_error}")
endif()
if(production_symbols MATCHES "installProcessTestAdapter|ViewerWindowTestControl")
    message(FATAL_ERROR "Production binary links test-adapter symbols")
endif()

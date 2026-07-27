if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY)
    message(FATAL_ERROR "SOURCE_DIR and BINARY are required")
endif()

file(GLOB_RECURSE sources
    "${SOURCE_DIR}/src/*"
    "${SOURCE_DIR}/CMakeLists.txt")
foreach(source IN LISTS sources)
    file(READ "${source}" contents)
    if(contents MATCHES
       "(QNetwork|QTcpSocket|QUdpSocket|QWebSocket|libcurl|curl_easy|https?://)")
        message(FATAL_ERROR "Network API or remote URL found in ${source}")
    endif()
endforeach()

execute_process(COMMAND ldd "${BINARY}" OUTPUT_VARIABLE dependencies
                RESULT_VARIABLE ldd_result)
if(NOT ldd_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect release dependencies")
endif()
if(dependencies MATCHES "(libcurl|libQt6Network|libssl|libcrypto)")
    message(FATAL_ERROR "Network-capable release dependency found: ${dependencies}")
endif()

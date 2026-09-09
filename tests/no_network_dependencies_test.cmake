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

find_program(READELF_EXECUTABLE readelf REQUIRED)
execute_process(
    COMMAND "${READELF_EXECUTABLE}" --dynamic "${BINARY}"
    OUTPUT_VARIABLE dynamic_section
    RESULT_VARIABLE readelf_result
)
if(NOT readelf_result EQUAL 0)
    message(FATAL_ERROR "Could not inspect direct release dependencies")
endif()
if(dynamic_section MATCHES
   "Shared library: \\[(libcurl|libQt6Network|libssl|libcrypto)[^]]*\\]")
    message(FATAL_ERROR
        "Direct network-capable release dependency found: ${dynamic_section}")
endif()

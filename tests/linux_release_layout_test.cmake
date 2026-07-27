if(NOT DEFINED SOURCE_DIR OR NOT DEFINED BINARY_DIR)
    message(FATAL_ERROR "SOURCE_DIR and BINARY_DIR are required")
endif()

set(stage "${BINARY_DIR}/release-layout-test")
file(REMOVE_RECURSE "${stage}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${stage}/usr"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Could not stage the development archive")
endif()

foreach(required
        "usr/bin/flick"
        "usr/share/applications/org.flick.Flick.desktop"
        "usr/share/metainfo/org.flick.Flick.metainfo.xml"
        "usr/share/icons/hicolor/scalable/apps/org.flick.Flick.svg"
        "usr/share/licenses/Flick/LICENSE")
    if(NOT EXISTS "${stage}/${required}")
        message(FATAL_ERROR "Release layout is missing ${required}")
    endif()
endforeach()

execute_process(
    COMMAND file "${stage}/usr/bin/flick"
    OUTPUT_VARIABLE binary_description
    RESULT_VARIABLE file_result
)
if(NOT file_result EQUAL 0 OR
   NOT binary_description MATCHES "(x86-64|x86_64)")
    message(FATAL_ERROR "Release binary is not x86_64: ${binary_description}")
endif()

execute_process(
    COMMAND "${SOURCE_DIR}/packaging/linux/build-release.sh"
            --archive-only "${BINARY_DIR}" "${stage}/artifacts"
    RESULT_VARIABLE archive_result
    ERROR_VARIABLE archive_error
)
file(READ "${SOURCE_DIR}/CMakeLists.txt" project_file)
string(REGEX MATCH "project\\(Flick VERSION ([^ ]+)" project_match "${project_file}")
set(version "${CMAKE_MATCH_1}")
if(NOT archive_result EQUAL 0 OR NOT EXISTS
   "${stage}/artifacts/Flick-${version}-linux-x86_64.tar.gz")
    message(FATAL_ERROR "Development archive was not produced: ${archive_error}")
endif()

file(READ "${stage}/usr/share/licenses/Flick/LICENSE" license)
if(NOT license MATCHES "GNU GENERAL PUBLIC LICENSE" OR
   NOT license MATCHES "either version 3 of the License, or.*any later version")
    message(FATAL_ERROR "Distributed license is not GPL-3.0-or-later")
endif()

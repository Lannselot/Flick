if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/packaging/linux/org.flick.Flick.desktop" desktop_entry)
foreach(required
        "Type=Application"
        "Exec=flick %f"
        "Icon=image-x-generic"
        "MimeType=image/jpeg;image/png;image/webp;image/gif;image/bmp;"
        "Categories=Graphics;Viewer;")
    string(FIND "${desktop_entry}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Desktop entry is missing: ${required}")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/packaging/linux/org.flick.Flick.metainfo.xml" metainfo)
foreach(required
        "<id>org.flick.Flick</id>"
        "<launchable type=\"desktop-id\">org.flick.Flick.desktop</launchable>"
        "<mediatype>image/jpeg</mediatype>"
        "<mediatype>image/png</mediatype>"
        "<mediatype>image/webp</mediatype>"
        "<mediatype>image/gif</mediatype>"
        "<mediatype>image/bmp</mediatype>")
    string(FIND "${metainfo}" "${required}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "AppStream metadata is missing: ${required}")
    endif()
endforeach()

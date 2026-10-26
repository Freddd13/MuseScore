# Qt 5.15.2 software-renderer backport. The SDK and its other modules stay shared.
set(KUMO_QTQUICK_PATCH_DIR "${PROJECT_BINARY_DIR}/personal/qtquick-fixed/bin"
    CACHE PATH "Qt Quick runtime with the QTBUG-80415 fix")

function(kumo_select_qtquick_runtime variable)
    set(patched "${KUMO_QTQUICK_PATCH_DIR}/Qt5Quick.dll")
    if(NOT EXISTS "${patched}")
        return()
    endif()
    if(NOT Qt5Core_VERSION VERSION_EQUAL "5.15.2")
        message(FATAL_ERROR "The Qt Quick backport is for Qt 5.15.2")
    endif()
    file(STRINGS "${KUMO_QTQUICK_PATCH_DIR}/Qt5Quick.sha256" expected LIMIT_COUNT 1)
    file(SHA256 "${patched}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "The Qt Quick backport checksum differs")
    endif()
    file(STRINGS "${KUMO_QTQUICK_PATCH_DIR}/Qt5Quick.original.sha256" original LIMIT_COUNT 1)
    set(selected)
    foreach(path IN LISTS ${variable})
        get_filename_component(name "${path}" NAME)
        if(name STREQUAL "Qt5Quick.dll")
            file(SHA256 "${path}" sdk)
            if(NOT sdk STREQUAL original)
                message(FATAL_ERROR "The Qt Quick backport belongs to another SDK build")
            endif()
            list(APPEND selected "${patched}")
        else()
            list(APPEND selected "${path}")
        endif()
    endforeach()
    set(${variable} "${selected}" PARENT_SCOPE)
endfunction()

if(WIN32 AND EXISTS "${KUMO_QTQUICK_PATCH_DIR}/Qt5Quick.fix.json")
    install(FILES
        "${KUMO_QTQUICK_PATCH_DIR}/Qt5Quick.fix.json"
        "${KUMO_QTQUICK_PATCH_DIR}/Qt5Quick.sha256"
        "${KUMO_QTQUICK_PATCH_DIR}/Qt5Quick.original.sha256"
        DESTINATION personal/thirdparty/qtquick)
    install(FILES
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty/qtquick/README.md"
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty/qtquick/QTBUG-80415.patch"
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty/qtquick/LICENSE.LGPL3"
        "${CMAKE_CURRENT_LIST_DIR}/thirdparty/qtquick/LICENSE.GPL3"
        DESTINATION personal/thirdparty/qtquick)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/tools/build_qtquick_fix.py"
        DESTINATION personal/tools)
endif()

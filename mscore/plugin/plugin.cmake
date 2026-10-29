
set (PLUGIN_API_SRC
    ${PROJECT_SOURCE_DIR}/mscore/workflow/paneldock.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/paneldock.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/referencefactory.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/referencefactory.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/referenceworkflow.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/regionalpanel.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/regionalpanel.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/regionalcontrols.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/regionalcomparison.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/regionalcomparison.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/regionalcontrols.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/midicapture.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/midicapture.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/midiinput.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/midiinput.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/scoreworkflow.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/workflowexpression.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/analysisidentity.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/arrangementimages.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/harmonylabel.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/chordeditor.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/positionhint.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/positionhint.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/playbackpreview.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/playbackpreview.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/workflowvariants.cpp
    ${PROJECT_SOURCE_DIR}/mscore/workflow/tapwrite.h
    ${PROJECT_SOURCE_DIR}/mscore/workflow/tapwrite.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/cursor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/cursor.h
    ${CMAKE_CURRENT_LIST_DIR}/api/elements.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/elements.h
    ${CMAKE_CURRENT_LIST_DIR}/api/enums.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/enums.h
    ${CMAKE_CURRENT_LIST_DIR}/api/excerpt.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/excerpt.h
    ${CMAKE_CURRENT_LIST_DIR}/api/fraction.h
    ${CMAKE_CURRENT_LIST_DIR}/api/instrument.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/instrument.h
    ${CMAKE_CURRENT_LIST_DIR}/api/part.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/part.h
    ${CMAKE_CURRENT_LIST_DIR}/api/playevent.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/playevent.h
    ${CMAKE_CURRENT_LIST_DIR}/api/qmlpluginapi.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/qmlpluginapi.h
    ${CMAKE_CURRENT_LIST_DIR}/api/readonlyanalysisjob.h
    ${CMAKE_CURRENT_LIST_DIR}/api/readonlyanalysisjob.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/pianohandanalysis.h
    ${CMAKE_CURRENT_LIST_DIR}/api/pianohandanalysis.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/analysisplayback.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/arrangementsnapshot.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/scoreobserver.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/scoreobserver.h
    ${CMAKE_CURRENT_LIST_DIR}/api/score.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/scoreelement.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/scoreelement.h
    ${CMAKE_CURRENT_LIST_DIR}/api/score.h
    ${CMAKE_CURRENT_LIST_DIR}/api/selection.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/selection.h
    ${CMAKE_CURRENT_LIST_DIR}/api/style.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/style.h
    ${CMAKE_CURRENT_LIST_DIR}/api/tie.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/tie.h
    ${CMAKE_CURRENT_LIST_DIR}/api/util.cpp
    ${CMAKE_CURRENT_LIST_DIR}/api/util.h
    )

list(APPEND PLUGIN_API_SRC ${PROJECT_SOURCE_DIR}/mscore/workflow/workflowdocument.cpp)

list(APPEND PLUGIN_API_SRC ${PROJECT_SOURCE_DIR}/mscore/workflow/referencepanel.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/referencepanel.h)

list(APPEND PLUGIN_API_SRC ${PROJECT_SOURCE_DIR}/mscore/workflow/builtinpanels.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/builtinpanels.h)

list(APPEND PLUGIN_API_SRC ${PROJECT_SOURCE_DIR}/mscore/workflow/arrangementreader.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/arrangementoptions.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/suggestionpanel.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/arrangement.h)

list(APPEND PLUGIN_API_SRC ${PROJECT_SOURCE_DIR}/mscore/workflow/localsuggestions.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/localsuggestions.h)

list(APPEND PLUGIN_API_SRC ${PROJECT_SOURCE_DIR}/mscore/performanceeditor/rhythmpane.cpp ${PROJECT_SOURCE_DIR}/mscore/performanceeditor/rhythmpane.h)

set (PLUGIN_SRC
    ${PLUGIN_API_SRC}
    ${CMAKE_CURRENT_LIST_DIR}/mscorePlugins.cpp
    ${CMAKE_CURRENT_LIST_DIR}/pluginCreator.cpp
    ${CMAKE_CURRENT_LIST_DIR}/pluginCreator.h
    ${CMAKE_CURRENT_LIST_DIR}/pluginManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/pluginManager.h
    ${CMAKE_CURRENT_LIST_DIR}/qmledit.cpp
    ${CMAKE_CURRENT_LIST_DIR}/qmledit.h
    ${CMAKE_CURRENT_LIST_DIR}/qmliconview.cpp
    ${CMAKE_CURRENT_LIST_DIR}/qmliconview.h
    ${CMAKE_CURRENT_LIST_DIR}/qmlplugin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/qmlpluginengine.cpp
    ${CMAKE_CURRENT_LIST_DIR}/qmlpluginengine.h
    ${CMAKE_CURRENT_LIST_DIR}/qmlplugin.h
    )

set (PLUGIN_UI
    ${CMAKE_CURRENT_LIST_DIR}/pluginManager.ui
    ${CMAKE_CURRENT_LIST_DIR}/pluginCreator.ui
    )


# The local MCP bridge loads QtWebSockets through QML, so deployment tools do
# not discover this DLL from the executable's import table.
if(WIN32)
    get_filename_component(KUMO_QT_PREFIX "${Qt5Core_DIR}/../../.." ABSOLUTE)
    if(EXISTS "${KUMO_QT_PREFIX}/bin/Qt5WebSockets.dll")
        install(FILES "${KUMO_QT_PREFIX}/bin/Qt5WebSockets.dll" DESTINATION bin)
    endif()
endif()

list(APPEND PLUGIN_API_SRC ${PROJECT_SOURCE_DIR}/mscore/workflow/aiconnection.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/aiconnection.h)
list(APPEND PLUGIN_SRC ${PROJECT_SOURCE_DIR}/mscore/workflow/aiconnection.cpp ${PROJECT_SOURCE_DIR}/mscore/workflow/aiconnection.h)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/share/workflow/ DESTINATION ${Mscore_SHARE_NAME}${Mscore_INSTALL_NAME}workflow)

install(CODE "
if(NOT EXISTS \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/kumo.ini\")
  file(INSTALL DESTINATION \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin\" TYPE FILE FILES \"${PROJECT_SOURCE_DIR}/personal/kumo.ini\")
endif()
")

set(KUMO_MCP_PACKAGE_DIR "" CACHE PATH "Directory containing the self-contained MuseScoreMCP executable")
if(EXISTS "${KUMO_MCP_PACKAGE_DIR}/MuseScoreMCP.exe")
  install(DIRECTORY "${KUMO_MCP_PACKAGE_DIR}/" DESTINATION bin/tools/mcp)
endif()

# The optional Windows host is independent of score and notation classes.
set(KUMO_VST_SDK ${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/kumo-vst3)
add_library(kumo_vst STATIC plugin.cpp plugin.h rack.cpp rack.h proxy.cpp bridge.h fixedchanges.h vst3plugin.cpp vst3editor.cpp vst3editor.h vst2plugin.cpp vst2editor.cpp vst2editor.h
 ${KUMO_VST_SDK}/pluginterfaces/base/coreiids.cpp
 ${KUMO_VST_SDK}/pluginterfaces/base/funknown.cpp
 ${KUMO_VST_SDK}/pluginterfaces/base/ustring.cpp
 ${KUMO_VST_SDK}/public.sdk/source/common/commoniids.cpp
 ${KUMO_VST_SDK}/public.sdk/source/common/memorystream.cpp
 ${KUMO_VST_SDK}/public.sdk/source/common/commonstringconvert.cpp
 ${KUMO_VST_SDK}/public.sdk/source/common/threadchecker_win32.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/vstinitiids.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/utility/stringconvert.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/module.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/module_win32.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/plugprovider.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/connectionproxy.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/eventlist.cpp
 ${KUMO_VST_SDK}/public.sdk/source/vst/hosting/processdata.cpp)
set_target_properties(kumo_vst PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(kumo_vst PRIVATE ${KUMO_VST_SDK} ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_definitions(kumo_vst PRIVATE RELEASE=1 NOMINMAX QT_NO_KEYWORDS)
if(MSVC)
 target_compile_options(kumo_vst PRIVATE /source-charset:utf-8)
endif()
target_link_libraries(kumo_vst PUBLIC Qt5::Core Qt5::Widgets Qt5::Network PRIVATE ole32 shell32 uuid)

add_executable(MuseScoreVstHost hostmain.cpp)
set_target_properties(MuseScoreVstHost PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(MuseScoreVstHost PRIVATE NOMINMAX QT_NO_KEYWORDS)
if(MSVC)
 target_compile_options(MuseScoreVstHost PRIVATE /source-charset:utf-8)
endif()
target_link_libraries(MuseScoreVstHost PRIVATE kumo_vst)

install(TARGETS MuseScoreVstHost RUNTIME DESTINATION bin)
install(FILES ${KUMO_VST_SDK}/LICENSE.txt ${KUMO_VST_SDK}/source.json DESTINATION ${Mscore_SHARE_NAME}${Mscore_INSTALL_NAME}/licenses/kumo-vst3)
install(FILES ${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/kumo-vestige/COPYING ${CMAKE_CURRENT_LIST_DIR}/../../thirdparty/kumo-vestige/source.json DESTINATION ${Mscore_SHARE_NAME}${Mscore_INSTALL_NAME}/licenses/kumo-vestige)

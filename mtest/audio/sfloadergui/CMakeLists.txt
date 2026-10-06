set(TARGET tst_sfloadergui)
set(MTEST_LINK_MSCOREAPP TRUE)
include(${PROJECT_SOURCE_DIR}/mtest/CreateMtestTarget.cmake)
target_include_directories(${TARGET} PRIVATE ${CMAKE_BINARY_DIR}/audio)

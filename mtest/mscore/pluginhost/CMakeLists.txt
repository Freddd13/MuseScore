set(TARGET tst_pluginhost)
set(MTEST_LINK_MSCOREAPP TRUE)
include(${PROJECT_SOURCE_DIR}/mtest/CreateMtestTarget.cmake)
target_include_directories(${TARGET} PRIVATE ${PROJECT_BINARY_DIR}/mscore ${PROJECT_SOURCE_DIR}/mscore)

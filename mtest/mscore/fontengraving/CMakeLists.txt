set(TARGET tst_fontengraving)
set(MTEST_LINK_MSCOREAPP TRUE)
include(${PROJECT_SOURCE_DIR}/mtest/CreateMtestTarget.cmake)
target_include_directories(${TARGET} PRIVATE ${PROJECT_BINARY_DIR}/mscore ${PROJECT_SOURCE_DIR}/mscore)
# This small harness includes the common prefix normally, avoiding stale shared
# MSVC precompiled headers when the font interface changes.
set_property(TARGET ${TARGET} PROPERTY DISABLE_PRECOMPILE_HEADERS ON)

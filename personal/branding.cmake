# Keep personal UI identity separate from the upstream application/file version.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/personal/VERSION")
file(STRINGS "${PROJECT_SOURCE_DIR}/personal/VERSION" KUMO_PERSONAL_VERSION LIMIT_COUNT 1)
if(NOT KUMO_PERSONAL_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
      message(FATAL_ERROR "personal/VERSION must contain major.minor.patch")
endif()
configure_file("${PROJECT_SOURCE_DIR}/personal/branding.h.in" "${PROJECT_BINARY_DIR}/personalbranding.h" @ONLY)

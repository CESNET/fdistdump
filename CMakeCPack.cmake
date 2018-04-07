##### CPack variables
set(CPACK_PACKAGE_VENDOR "CESNET")
set(CPACK_PACKAGE_CONTACT "wrona@cesnet.cz")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_CHECKSUM SHA256)

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

set(CPACK_GENERATOR "TGZ;TXZ")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM}")

set(CPACK_SOURCE_GENERATOR "TGZ;TXZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-source")
# ignore (don't pack) certaing files
set(CPACK_SOURCE_IGNORE_FILES
    # Git
    "/\\\\.git/"
    "/\\\\.gitignore$"
    # temporary files
    "~$"
    "\\\\.swo$"
    "\\\\.swp$"
    )

##### CMake variables for CPack
# Always use relative path as a DESTINATION, otherwise CMAKE_INSTALL_PREFIX
# won't have any effect.
set(CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION TRUE)

##### Include CPack module once all variables are set.
# This generates CPackConfig.cmake and CPackSourceConfig.cmake files. It also
# generates a new target called "package" in the build system, which invokes
# CPack by specifying the generated configuration files.
include(CPack)

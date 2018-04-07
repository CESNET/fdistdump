# generate a man page file from a template
string(TIMESTAMP DATE "%Y-%m-%d")
string(TOUPPER "${PROJECT_NAME}" PROJECT_NAME_UPPER)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/man/${PROJECT_NAME}.1.in"
               "${CMAKE_CURRENT_BINARY_DIR}/man/${PROJECT_NAME}.1"
               @ONLY)

# generate instalation rules for the documentation
install(FILES "${PROJECT_BINARY_DIR}/doc/man/${PROJECT_NAME}.1"
        DESTINATION "${CMAKE_INSTALL_MANDIR}"
        COMPONENT "Documentation")

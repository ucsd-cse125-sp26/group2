# cmake/Doxygen.cmake
# Finds Doxygen (and optionally Graphviz dot) and registers a `docs` build target.
# Include from CMakeLists.txt after the project() call.

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")

    # Enable diagram generation only when Graphviz dot is available.
    if(DOXYGEN_DOT_FOUND)
        set(DOXYGEN_HAVE_DOT "YES")
        set(DOXYGEN_DOT_PATH "${DOXYGEN_DOT_EXECUTABLE}")
        message(STATUS "Graphviz dot found — call graphs and diagrams enabled")
    else()
        set(DOXYGEN_HAVE_DOT "NO")
        set(DOXYGEN_DOT_PATH "")
        message(STATUS "Graphviz dot not found — diagrams disabled. Install with: "
                       "sudo apt-get install graphviz  OR  brew install graphviz")
    endif()

    configure_file(
        "${CMAKE_SOURCE_DIR}/Doxyfile.in"
        "${CMAKE_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    message(STATUS "Doxygen ${DOXYGEN_VERSION} found — run 'cmake --build . --target docs' to generate docs")
else()
    message(STATUS "Doxygen not found — 'docs' target unavailable. Install with: "
                   "sudo apt-get install doxygen  OR  brew install doxygen")
endif()

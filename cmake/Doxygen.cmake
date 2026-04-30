# cmake/Doxygen.cmake
# Finds Doxygen (and optionally Graphviz dot) and registers a `docs` build target.
# Include from CMakeLists.txt after the project() call.

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")

    # Enable diagram generation only when Graphviz dot is available.
    # find_package(Doxygen OPTIONAL_COMPONENTS dot) doesn't always detect dot
    # (e.g. when dot is in /usr/bin but not in Doxygen's own directory), so we
    # fall back to find_program() when the component check misses it.
    if(DOXYGEN_DOT_FOUND)
        set(DOXYGEN_HAVE_DOT "YES")
        set(DOXYGEN_DOT_PATH "${DOXYGEN_DOT_EXECUTABLE}")
        message(STATUS "Graphviz dot found (via Doxygen component) — diagrams enabled")
    else()
        find_program(_DOT_FALLBACK dot)
        if(_DOT_FALLBACK)
            set(DOXYGEN_HAVE_DOT "YES")
            get_filename_component(DOXYGEN_DOT_PATH "${_DOT_FALLBACK}" DIRECTORY)
            message(STATUS "Graphviz dot found (via find_program: ${_DOT_FALLBACK}) — diagrams enabled")
        else()
            set(DOXYGEN_HAVE_DOT "NO")
            set(DOXYGEN_DOT_PATH "")
            message(STATUS "Graphviz dot not found — diagrams disabled. Install with: "
                           "sudo apt-get install graphviz  OR  brew install graphviz")
        endif()
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

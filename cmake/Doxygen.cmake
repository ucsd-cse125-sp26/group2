# cmake/Doxygen.cmake
# Finds Doxygen and registers a `docs` build target.
# Include from CMakeLists.txt after the project() call.

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")

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
    message(STATUS "Doxygen not found — 'docs' target unavailable. Install with: sudo apt-get install doxygen  OR  brew install doxygen")
endif()

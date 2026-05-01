/// @file VHACDImpl.cpp
/// @brief Single translation unit that compiles the V-HACD implementation.
///
/// V-HACD is a header-only library (`VHACD.h`).  Its header gates the actual
/// algorithm behind the `ENABLE_VHACD_IMPLEMENTATION` macro — exactly like
/// `stb_image` and `dr_libs` style single-headers.  Defining the macro in
/// *one* compilation unit (this file) compiles the implementation; every
/// other site just `#include <VHACD.h>` for the public API.

// V-HACD uses a few constructs that fire warnings under our strict warning
// configuration (-Wconversion, -Wshadow, etc.).  Suppress them around the
// implementation include so we don't need to fix upstream code.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wcast-align"
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4305 4267 4456 4458 4459 4505 4189 4100)
#endif

#define ENABLE_VHACD_IMPLEMENTATION 1
#include <VHACD.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

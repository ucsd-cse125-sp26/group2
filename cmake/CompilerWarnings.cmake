function(set_compiler_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4           # High warning level
            /permissive-  # Strict conformance
            /w14242       # Possible loss of data (int → char)
            /w14254       # Larger bit field assigned to smaller
            /w14263       # Member function doesn't override base
            /w14265       # Non-virtual destructor in class with virtuals
            /w14287       # Unsigned/negative constant mismatch
            /we4289       # Loop control variable used outside loop
            /w14296       # Expression always true/false
            /w14311       # Pointer truncation
            /w14545       # Expression before comma evaluates to function with no args
            /w14546       # Function call before comma missing argument list
            /w14547       # Operator before comma has no effect
            /w14549       # Operator before comma has no effect (redundant)
            /w14555       # Expression has no effect
            /w14619       # Pragma warning: unknown warning number
            /w14826       # Conversion from type1 to type2 is sign-extended
            /w14905       # Wide string literal cast to LPSTR
            /w14906       # String literal cast to LPWSTR
            /w14928       # Illegal copy-initialization
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wcast-align
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wmisleading-indentation
            # Silence warnings from third-party headers pulled in via FetchContent
            -Wno-error
        )
    endif()
endfunction()

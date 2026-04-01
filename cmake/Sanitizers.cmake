function(enable_sanitizers target)
    if(MSVC)
        # MSVC supports ASan since VS 2019 16.9
        if(ENABLE_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address)
            target_compile_definitions(${target} PRIVATE _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
        endif()
        # MSVC does not support UBSan or TSan
        return()
    endif()

    set(sanitizers "")

    if(ENABLE_ASAN)
        list(APPEND sanitizers "address")
    endif()

    if(ENABLE_UBSAN)
        list(APPEND sanitizers "undefined")
    endif()

    if(ENABLE_TSAN)
        if(ENABLE_ASAN)
            message(WARNING "TSan is incompatible with ASan — TSan will be ignored.")
        else()
            list(APPEND sanitizers "thread")
        endif()
    endif()

    if(sanitizers)
        list(JOIN sanitizers "," sanitizer_flags)
        target_compile_options(${target} PRIVATE
            -fsanitize=${sanitizer_flags}
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
        )
        target_link_options(${target} PRIVATE
            -fsanitize=${sanitizer_flags}
        )
        message(STATUS "[${target}] Sanitizers enabled: ${sanitizer_flags}")
    endif()
endfunction()

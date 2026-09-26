# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Applies the project warning policy to a target. C++ sources only: the
# options would reach other languages too (windres rejects them for the
# Windows resource script, src/app/openshape.rc.in).
function(openshape_set_warnings target)
    set(cxx "$<COMPILE_LANGUAGE:CXX>")
    if(MSVC)
        target_compile_options(${target} PRIVATE $<${cxx}:/W4> $<${cxx}:/permissive->)
        if(OPENSHAPE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE $<${cxx}:/WX>)
        endif()
    else()
        target_compile_options(${target} PRIVATE $<${cxx}:-Wall> $<${cxx}:-Wextra> $<${cxx}:-Wpedantic> $<${cxx}:-Wnon-virtual-dtor>)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE $<${cxx}:-Wshadow=local>) # GCC only (Clang rejects it)
        endif()
        if(OPENSHAPE_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE $<${cxx}:-Werror>)
        endif()
    endif()
endfunction()

# ASan/UBSan smoke is part of the commit gate (G0.4) and rides along with the
# debug preset. MSVC has no equivalent spelling here; the CI sanitizer job is
# clang on Linux, so that is where this is exercised.
option(GMAX_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

function(gmax_set_sanitizers target)
  if(NOT GMAX_SANITIZE)
    return()
  endif()
  if(MSVC)
    message(WARNING "GMAX_SANITIZE is ignored with MSVC; use the CI sanitizer job.")
    return()
  endif()
  target_compile_options(${target} PRIVATE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer)
  target_link_options(${target} PRIVATE -fsanitize=address,undefined)
endfunction()

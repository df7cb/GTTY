# cmake -DMOD=<cmake/GttyVersion.cmake> -P version_detect_test.cmake
# Checks how a "git describe" string becomes the three version forms. The describe string
# is fed in through GTTY_VERSION_OVERRIDE, so the test does not depend on the checkout.
include("${MOD}")
set(failures 0)
function(expect describe display numeric package)
  set(GTTY_VERSION_OVERRIDE "${describe}")
  gtty_detect_version("${CMAKE_CURRENT_LIST_DIR}/no-such-directory")
  if(NOT GTTY_VERSION STREQUAL display OR NOT GTTY_VERSION_NUMERIC STREQUAL numeric OR NOT GTTY_VERSION_PACKAGE STREQUAL package)
    message(SEND_ERROR "${describe}: got ${GTTY_VERSION} / ${GTTY_VERSION_NUMERIC} / ${GTTY_VERSION_PACKAGE}, "
                       "expected ${display} / ${numeric} / ${package}")
  endif()
endfunction()
expect("v0.2.8"                  "0.2.8"                  "0.2.8"  "0.2.8")
expect("0.2.8"                   "0.2.8"                  "0.2.8"  "0.2.8")
expect("v0.2.8-3-g1a2b3c4"       "0.2.8-3-g1a2b3c4"       "0.2.8"  "0.2.8+3.g1a2b3c4")
expect("v0.2.8-dirty"            "0.2.8-dirty"            "0.2.8"  "0.2.8+dirty")
expect("v0.2.8-3-g1a2b3c4-dirty" "0.2.8-3-g1a2b3c4-dirty" "0.2.8"  "0.2.8+3.g1a2b3c4.dirty")
expect("v10.20.30"               "10.20.30"               "10.20.30" "10.20.30")
expect("1a2b3c4"                 "1a2b3c4"                "0.0.0"  "0.0.0+g1a2b3c4")
expect("1a2b3c4-dirty"           "1a2b3c4-dirty"          "0.0.0"  "0.0.0+g1a2b3c4.dirty")
expect("2026.11-custom"          "2026.11-custom"         "0.0.0"  "2026.11-custom")

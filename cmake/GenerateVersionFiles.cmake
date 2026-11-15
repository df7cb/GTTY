# Run on every build (cmake -P) by the gtty_version target: writes the files that carry
# the version into OUT_DIR. configure_file() leaves a file alone when its content would
# not change, so nothing is rebuilt unless the tag or commit did change.
#   -DSOURCE_DIR=<top of the sources> -DOUT_DIR=<build dir>/generated [-DGTTY_VERSION_OVERRIDE=...]
include("${CMAKE_CURRENT_LIST_DIR}/GttyVersion.cmake")
gtty_detect_version("${SOURCE_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/gtty_version.h.in" "${OUT_DIR}/gtty_version.h" @ONLY)
configure_file("${SOURCE_DIR}/packaging/gtty.1.in" "${OUT_DIR}/gtty.1" @ONLY)
configure_file("${SOURCE_DIR}/packaging/gtty-cli.1.in" "${OUT_DIR}/gtty-cli.1" @ONLY)
configure_file("${SOURCE_DIR}/packaging/changelog.in" "${OUT_DIR}/changelog" @ONLY)
configure_file("${SOURCE_DIR}/data/io.github.gtty.metainfo.xml.in" "${OUT_DIR}/io.github.gtty.metainfo.xml" @ONLY)

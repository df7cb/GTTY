# Works out gtty's version from the source tree it is built from, so that a build always
# says which tag or commit it is. No version number is written anywhere in the sources.
#
#   gtty_detect_version(<source dir>)
#
# sets, in the caller's scope:
#   GTTY_VERSION           what the program shows: "0.2.8" on the tag v0.2.8,
#                          "0.2.8-3-g1a2b3c4" three commits after it, "1a2b3c4" when no
#                          tag can be seen (a shallow clone); "-dirty" is appended when
#                          the working tree has uncommitted changes
#   GTTY_VERSION_NUMERIC   "0.2.8": for project(VERSION) and the macOS bundle
#   GTTY_VERSION_PACKAGE   "0.2.8" or "0.2.8+3.g1a2b3c4": a valid Debian / AppStream version
#   GTTY_VERSION_DATE      date of the commit, "2026-11-14"
#   GTTY_VERSION_DATE_RFC  the same as "Sat, 14 Nov 2026 15:50:51 -0700" (Debian changelog)
#   GTTY_VERSION_MAN_DATE  the same as "November 2026" (man page header)
#
# Sources, first that answers:
#   1. GTTY_VERSION_OVERRIDE (a -D option or an environment variable), for packagers
#   2. git describe, when the source directory is a git checkout
#   3. .git_archival.txt, which git fills in for "git archive" and GitHub source tarballs
#   4. GITHUB_REF_NAME / GITHUB_SHA, for CI jobs that got the sources without git
function(gtty_detect_version source_dir)
  set(describe "")
  set(date_iso "")
  set(date_rfc "")

  if(GTTY_VERSION_OVERRIDE)
    set(describe "${GTTY_VERSION_OVERRIDE}")
  elseif(NOT "$ENV{GTTY_VERSION_OVERRIDE}" STREQUAL "")
    set(describe "$ENV{GTTY_VERSION_OVERRIDE}")
  endif()

  find_program(GTTY_GIT_EXECUTABLE git)
  mark_as_advanced(GTTY_GIT_EXECUTABLE)
  if(GTTY_GIT_EXECUTABLE AND EXISTS "${source_dir}/.git")
    # safe.directory: packaging containers build a checkout owned by another user.
    set(git ${GTTY_GIT_EXECUTABLE} -c safe.directory=* -C "${source_dir}")
    if(describe STREQUAL "")
      execute_process(COMMAND ${git} describe --tags --match "v[0-9]*" --always --dirty --abbrev=7
        OUTPUT_VARIABLE out RESULT_VARIABLE rc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(rc EQUAL 0)
        set(describe "${out}")
      endif()
    endif()
    execute_process(COMMAND ${git} log -1 --format=%cs
      OUTPUT_VARIABLE date_iso RESULT_VARIABLE rc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${git} log -1 --format=%cD
      OUTPUT_VARIABLE date_rfc RESULT_VARIABLE rc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  elseif(EXISTS "${source_dir}/.git_archival.txt")
    file(STRINGS "${source_dir}/.git_archival.txt" lines)
    foreach(line IN LISTS lines)
      if(line MATCHES "\\$Format")   # not an archive: git did not fill the file in
        continue()
      endif()
      if(line MATCHES "^describe: (.+)$" AND describe STREQUAL "")
        set(describe "${CMAKE_MATCH_1}")
      elseif(line MATCHES "^commit: (.+)$")
        set(archive_commit "${CMAKE_MATCH_1}")
      elseif(line MATCHES "^date: (.+)$")
        set(date_iso "${CMAKE_MATCH_1}")
      elseif(line MATCHES "^date-rfc: (.+)$")
        set(date_rfc "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    if(describe STREQUAL "" AND archive_commit)
      set(describe "${archive_commit}")
    endif()
  endif()

  if(describe STREQUAL "")
    if("$ENV{GITHUB_REF_TYPE}" STREQUAL "tag" AND NOT "$ENV{GITHUB_REF_NAME}" STREQUAL "")
      set(describe "$ENV{GITHUB_REF_NAME}")
    elseif(NOT "$ENV{GITHUB_SHA}" STREQUAL "")
      string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 describe)
    endif()
  endif()

  set(numeric "0.0.0")
  if(describe MATCHES "^v?([0-9]+\\.[0-9]+\\.[0-9]+)(-([0-9]+)-g([0-9a-f]+))?(-dirty)?$")
    set(numeric "${CMAKE_MATCH_1}")
    set(dirty "${CMAKE_MATCH_5}")
    if(CMAKE_MATCH_3)
      set(display "${numeric}-${CMAKE_MATCH_3}-g${CMAKE_MATCH_4}")
      set(package "${numeric}+${CMAKE_MATCH_3}.g${CMAKE_MATCH_4}")
    else()
      set(display "${numeric}")
      set(package "${numeric}")
    endif()
  elseif(describe MATCHES "^([0-9a-f]+)(-dirty)?$")
    set(dirty "${CMAKE_MATCH_2}")
    set(display "${CMAKE_MATCH_1}")
    set(package "0.0.0+g${CMAKE_MATCH_1}")
  elseif(NOT describe STREQUAL "")   # an override in a form of the packager's own
    set(dirty "")
    set(display "${describe}")
    set(package "${describe}")
  else()
    set(dirty "")
    set(display "unknown")
    set(package "0.0.0")
  endif()
  if(dirty)
    string(APPEND display "-dirty")
    if(package MATCHES "\\+")
      string(APPEND package ".dirty")
    else()
      string(APPEND package "+dirty")
    endif()
  endif()

  if(date_iso STREQUAL "")
    string(TIMESTAMP date_iso "%Y-%m-%d" UTC)
  endif()
  if(date_rfc STREQUAL "")
    string(TIMESTAMP date_rfc "%a, %d %b %Y %H:%M:%S +0000" UTC)
  endif()
  set(months January February March April May June July August September October November December)
  set(man_date "${date_iso}")
  if(date_iso MATCHES "^([0-9][0-9][0-9][0-9])-([0-9][0-9])-")
    set(year "${CMAKE_MATCH_1}")
    math(EXPR month_index "${CMAKE_MATCH_2} - 1")   # decimal: "08" and "09" are not octal here
    list(GET months ${month_index} month)
    set(man_date "${month} ${year}")
  endif()

  set(GTTY_VERSION "${display}" PARENT_SCOPE)
  set(GTTY_VERSION_NUMERIC "${numeric}" PARENT_SCOPE)
  set(GTTY_VERSION_PACKAGE "${package}" PARENT_SCOPE)
  set(GTTY_VERSION_DATE "${date_iso}" PARENT_SCOPE)
  set(GTTY_VERSION_DATE_RFC "${date_rfc}" PARENT_SCOPE)
  set(GTTY_VERSION_MAN_DATE "${man_date}" PARENT_SCOPE)
endfunction()

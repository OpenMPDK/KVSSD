#.rst:
# FindLibSubunit
# --------
#
# Find the native realtime includes and library.
#
# IMPORTED Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines :prop_tgt:`IMPORTED` target ``LIBSUBUNIT::LIBSUBUNIT``, if
# LIBSUBUNIT has been found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables:
#
# ::
#
#   LIBSUBUNIT_INCLUDE_DIRS  - where to find subunit/child.h
#   LIBSUBUNIT_LIBRARIES     - List of libraries when using libsubunit.
#   LIBSUBUNIT_FOUND         - True if subunit library found.
#
# Hints
# ^^^^^
#
# A user may set ``LIBSUBUNIT_ROOT`` to a subunit library installation root to tell this
# module where to look.

find_path(LIBSUBUNIT_INCLUDE_DIRS
  NAMES subunit/child.h
  PATHS ${LIBSUBUNIT_ROOT}/include/
)
find_library(LIBSUBUNIT_LIBRARIES subunit)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibSubunit DEFAULT_MSG LIBSUBUNIT_LIBRARIES LIBSUBUNIT_INCLUDE_DIRS)
mark_as_advanced(LIBSUBUNIT_INCLUDE_DIRS LIBSUBUNIT_LIBRARIES)

if(LIBSUBUNIT_FOUND)
    if(NOT TARGET LIBSUBUNIT::LIBSUBUNIT)
      add_library(LIBSUBUNIT::LIBSUBUNIT UNKNOWN IMPORTED)
      set_target_properties(LIBSUBUNIT::LIBSUBUNIT PROPERTIES
        IMPORTED_LOCATION "${LIBSUBUNIT_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBSUBUNIT_INCLUDE_DIRS}")
    endif()
endif()

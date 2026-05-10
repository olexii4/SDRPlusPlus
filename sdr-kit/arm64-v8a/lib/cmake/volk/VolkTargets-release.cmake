#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "Volk::volk" for configuration "Release"
set_property(TARGET Volk::volk APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(Volk::volk PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libvolk.so"
  IMPORTED_SONAME_RELEASE "libvolk.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS Volk::volk )
list(APPEND _IMPORT_CHECK_FILES_FOR_Volk::volk "${_IMPORT_PREFIX}/lib/libvolk.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)

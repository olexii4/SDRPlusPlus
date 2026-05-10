cmake_minimum_required(VERSION 3.13)
project(mirisdr_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

# --- libusb (static, from source on Android; pkg-config on desktop) ---
if (ANDROID)
    set(LIBUSB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libusb")
    file(GLOB_RECURSE LIBUSB_SRC "${LIBUSB_DIR}/libusb/*.c")
    add_library(libusb_mirisdr STATIC ${LIBUSB_SRC})
    set_target_properties(libusb_mirisdr PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(libusb_mirisdr PRIVATE "${LIBUSB_DIR}/android")
    target_include_directories(libusb_mirisdr PUBLIC "${LIBUSB_DIR}/libusb")
    find_library(LOG_LIB log)
    target_link_libraries(libusb_mirisdr ${LOG_LIB})
endif ()

# --- libmirisdr (static) ---
# libmirisdr.c #includes all other .c files, so only compile that one
set(MIRISDR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libmirisdr")

add_library(mirisdr STATIC "${MIRISDR_DIR}/src/libmirisdr.c")
target_include_directories(mirisdr PUBLIC "${MIRISDR_DIR}/include")
target_include_directories(mirisdr PRIVATE "${MIRISDR_DIR}/src")
target_compile_definitions(mirisdr PUBLIC mirisdr_STATIC)

if (ANDROID)
    find_library(LOG_LIB2 log)
    target_link_libraries(mirisdr PRIVATE libusb_mirisdr ${LOG_LIB2})
else ()
    find_package(PkgConfig)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    target_include_directories(mirisdr PRIVATE ${LIBUSB_INCLUDE_DIRS})
    target_link_directories(mirisdr PUBLIC ${LIBUSB_LIBRARY_DIRS})
    target_link_libraries(mirisdr PUBLIC ${LIBUSB_LIBRARIES})
endif ()

# --- mirisdr_source module ---
target_include_directories(mirisdr_source PRIVATE "${MIRISDR_DIR}/include")
target_link_libraries(mirisdr_source PRIVATE mirisdr)
if (NOT ANDROID)
    target_link_directories(mirisdr_source PRIVATE ${LIBUSB_LIBRARY_DIRS})
    target_link_libraries(mirisdr_source PRIVATE ${LIBUSB_LIBRARIES})
endif ()

cmake_minimum_required(VERSION 3.20)
project(displayManager VERSION 1.1.0 LANGUAGES CXX OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "DisplayManager requires macOS")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 23)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

add_library(display_core STATIC src/display_manager.cc)
target_include_directories(display_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(display_core PUBLIC "-framework ApplicationServices" "-framework CoreFoundation")
target_compile_options(display_core PRIVATE -Wall -Wextra -Wpedantic)

# Keep the existing dm CLI available.
add_executable(displayManager src/main.cc)
set_target_properties(displayManager PROPERTIES OUTPUT_NAME "dm")
target_link_libraries(displayManager PRIVATE display_core)

add_executable(DisplayManagerMenuBar MACOSX_BUNDLE src/menu_bar.mm)
set_target_properties(DisplayManagerMenuBar PROPERTIES
    OUTPUT_NAME "DisplayManager"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "DisplayManager"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.timeee.displaymanager"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)
target_compile_options(DisplayManagerMenuBar PRIVATE -fobjc-arc -Wall -Wextra -Wpedantic)
target_link_libraries(DisplayManagerMenuBar PRIVATE display_core "-framework AppKit")
install(TARGETS displayManager RUNTIME DESTINATION bin)
install(TARGETS DisplayManagerMenuBar BUNDLE DESTINATION Applications)

include(CTest)
if(BUILD_TESTING)
    add_executable(display_record_test tests/display_record_test.cc)
    target_include_directories(display_record_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME display_record_test COMMAND display_record_test)
    add_executable(display_core_smoke tests/core_smoke.cc)
    target_link_libraries(display_core_smoke PRIVATE display_core)
    add_test(NAME display_core_smoke COMMAND display_core_smoke)
endif()

cmake_minimum_required(VERSION 3.20)

set(APP_NAME "DisplayManager")

project(DisplayManager
        VERSION 1.0
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
     "src/*.cc" "src/*.c"
     "include/*.h"
)

add_executable(${APP_NAME} ${SOURCES})

target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${APP_NAME} PRIVATE
    "-framework ApplicationServices"
)
cmake_minimum_required(VERSION 3.5.1)

# ==============================================================================
#   Calculate Project Version
# ==============================================================================
set(MAJOR 0)
set(MAJOR_OFFSET 0)

set(MINOR 0)
set(MINOR_OFFSET 0)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
  execute_process(
    COMMAND git rev-list --all --count
    OUTPUT_VARIABLE REVISION
    )
  math(EXPR PATCH "${REVISION} - ${MAJOR_OFFSET} - ${MINOR_OFFSET}")
else()
  set(PATCH 0)
endif()

# ==============================================================================
#   Project Settings
# ==============================================================================
project(vt10x VERSION ${MAJOR}.${MINOR}.${PATCH} LANGUAGES CXX)

set(CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -Og")
set(CMAKE_CXX_FLAGS_RELEASE "-s -O3 -mtune=native -march=native -DNDEBUG")
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_CXX_STANDARD 17) # CMake <= 3.8.2

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Boost REQUIRED)

set(${PROJECT_NAME}_CONFIGURE ${CMAKE_CURRENT_SOURCE_DIR}/configure)
set(${PROJECT_NAME}_INCLUDE   ${CMAKE_CURRENT_SOURCE_DIR}/include)

include_directories(
  ${${PROJECT_NAME}_INCLUDE}
  ${Boost_INCLUDE_DIRS}
  )

link_directories(
  ${Boost_LIBRARY_DIRS}
  )

# ==============================================================================
#   Configure README
# ==============================================================================
if(EXISTS ${${PROJECT_NAME}_CONFIGURE}/README.md.cmake)
  configure_file(
    ${${PROJECT_NAME}_CONFIGURE}/README.md.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    )
endif()

# ==============================================================================
#   Configure Header
# ==============================================================================
string(TIMESTAMP ${PROJECT_NAME}_BUILD_DATE "%Y/%m/%d %H:%M:%S")

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
  execute_process(
    COMMAND git rev-parse HEAD
    COMMAND tr -d "\n"
    OUTPUT_VARIABLE ${PROJECT_NAME}_BUILD_HASH
    )
else()
  set(${PROJECT_NAME}_BUILD_HASH "")
endif()

# ==============================================================================
#   Executable
# ==============================================================================
file(GLOB ${PROJECT_NAME}_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SOURCES})

target_link_libraries(${PROJECT_NAME}
  cairo
  xcb
  )

# ==============================================================================
#   Installation
# ==============================================================================
install(
  TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION bin
  )


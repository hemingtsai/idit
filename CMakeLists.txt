cmake_minimum_required(VERSION 3.14)
project(idit VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Curses REQUIRED)

add_executable(idit
    src/main.cpp
    src/log_reader.cpp
    src/tui.cpp
    src/theme.cpp
)

target_include_directories(idit PRIVATE src ${CURSES_INCLUDE_DIRS})
target_link_libraries(idit PRIVATE ${CURSES_LIBRARIES})

# Test binary
add_executable(test_reader src/test_reader.cpp src/log_reader.cpp)
target_include_directories(test_reader PRIVATE src)

# Install
install(TARGETS idit DESTINATION bin)
install(DIRECTORY themes/ DESTINATION share/idit/themes)

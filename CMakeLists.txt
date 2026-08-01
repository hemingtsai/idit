cmake_minimum_required(VERSION 3.14)
project(idit VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ============================================================================
# Common source files (shared library for both frontends)
# ============================================================================
set(CORE_SOURCES
    src/log_reader.cpp
    src/log_viewer.cpp
)

set(CORE_HEADERS
    src/log_reader.hpp
    src/log_viewer.hpp
)

# ============================================================================
# TUI target (ncurses)
# ============================================================================
find_package(Curses REQUIRED)

add_executable(idit
    src/main.cpp
    src/tui.cpp
    src/theme.cpp
    ${CORE_SOURCES}
)

target_include_directories(idit PRIVATE src ${CURSES_INCLUDE_DIRS})
target_link_libraries(idit PRIVATE ${CURSES_LIBRARIES})

# ============================================================================
# GUI target (ImGui + GLFW + OpenGL)
# ============================================================================

# --- GLFW ---
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "GLFW not found — downloading via FetchContent")
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)
endif()

# --- ImGui ---
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# --- OpenGL ---
find_package(OpenGL REQUIRED)

add_executable(idit-gui
    src/gui_main.cpp
    src/gui.cpp
    ${CORE_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(idit-gui PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(idit-gui PRIVATE
    glfw
    OpenGL::GL
    ${CMAKE_DL_LIBS}
)

# macOS: link additional frameworks needed by GLFW
if(APPLE)
    target_link_libraries(idit-gui PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()

# ============================================================================
# Test binary
# ============================================================================
add_executable(test_reader src/test_reader.cpp src/log_reader.cpp)
target_include_directories(test_reader PRIVATE src)

# ============================================================================
# Install
# ============================================================================
install(TARGETS idit DESTINATION bin)
install(DIRECTORY themes/ DESTINATION share/idit/themes)

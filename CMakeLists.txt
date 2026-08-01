cmake_minimum_required(VERSION 3.14)
project(idit VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ============================================================================
# Lua 5.4 (for config/theme scripting)
# ============================================================================
find_package(Lua 5.4 QUIET)
if(NOT Lua_FOUND)
    message(STATUS "Lua not found — building from source")
    FetchContent_Declare(lua
        GIT_REPOSITORY https://github.com/lua/lua.git
        GIT_TAG        v5.4.7
    )
    FetchContent_MakeAvailable(lua)

    set(LUA_SOURCES
        ${lua_SOURCE_DIR}/lapi.c
        ${lua_SOURCE_DIR}/lauxlib.c
        ${lua_SOURCE_DIR}/lbaselib.c
        ${lua_SOURCE_DIR}/lcode.c
        ${lua_SOURCE_DIR}/lcorolib.c
        ${lua_SOURCE_DIR}/lctype.c
        ${lua_SOURCE_DIR}/ldblib.c
        ${lua_SOURCE_DIR}/ldebug.c
        ${lua_SOURCE_DIR}/ldo.c
        ${lua_SOURCE_DIR}/ldump.c
        ${lua_SOURCE_DIR}/lfunc.c
        ${lua_SOURCE_DIR}/lgc.c
        ${lua_SOURCE_DIR}/linit.c
        ${lua_SOURCE_DIR}/liolib.c
        ${lua_SOURCE_DIR}/llex.c
        ${lua_SOURCE_DIR}/lmathlib.c
        ${lua_SOURCE_DIR}/lmem.c
        ${lua_SOURCE_DIR}/loadlib.c
        ${lua_SOURCE_DIR}/lobject.c
        ${lua_SOURCE_DIR}/lopcodes.c
        ${lua_SOURCE_DIR}/loslib.c
        ${lua_SOURCE_DIR}/lparser.c
        ${lua_SOURCE_DIR}/lstate.c
        ${lua_SOURCE_DIR}/lstring.c
        ${lua_SOURCE_DIR}/lstrlib.c
        ${lua_SOURCE_DIR}/ltable.c
        ${lua_SOURCE_DIR}/ltablib.c
        ${lua_SOURCE_DIR}/ltm.c
        ${lua_SOURCE_DIR}/lundump.c
        ${lua_SOURCE_DIR}/lutf8lib.c
        ${lua_SOURCE_DIR}/lvm.c
        ${lua_SOURCE_DIR}/lzio.c
    )
    set(LUA_INCLUDE_DIR ${lua_SOURCE_DIR})
    set(LUA_LIBRARIES "")  # we compile the sources directly, no separate library
    set(LUA_FOUND TRUE)
endif()

# --- sol2 (header-only Lua C++ wrapper) ---
FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(sol2)

# ============================================================================
# Common source files (shared library for both frontends)
# ============================================================================
set(CORE_SOURCES
    src/log_reader.cpp
    src/log_viewer.cpp
    src/config.cpp
)

set(CORE_HEADERS
    src/log_reader.hpp
    src/log_viewer.hpp
    src/config.hpp
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
    ${LUA_SOURCES}
)

target_include_directories(idit PRIVATE src ${CURSES_INCLUDE_DIRS} ${LUA_INCLUDE_DIR} ${sol2_SOURCE_DIR}/include)
target_link_libraries(idit PRIVATE ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS})

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
    ${LUA_SOURCES}
)

target_include_directories(idit-gui PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${LUA_INCLUDE_DIR}
    ${sol2_SOURCE_DIR}/include
)

target_link_libraries(idit-gui PRIVATE
    glfw
    OpenGL::GL
    ${CMAKE_DL_LIBS}
)

# macOS: suppress OpenGL deprecation warnings
if(APPLE)
    target_compile_definitions(idit-gui PRIVATE GL_SILENCE_DEPRECATION)
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

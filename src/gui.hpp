#pragma once

#include "log_reader.hpp"
#include "log_viewer.hpp"

#include <vector>

struct GLFWwindow;

/// Key event queued from the GLFW callback.
struct KeyEvent {
    int key;
    int mods;
};

/// ImGui + GLFW + OpenGL3 GUI frontend for LogViewer.
class GUI {
public:
    GUI() = default;
    ~GUI();

    GUI(const GUI&) = delete;
    GUI& operator=(const GUI&) = delete;

    bool init();
    void run(LogViewer& viewer, const std::string& filepath,
             const ReadOptions& opts, bool followMode);
    void shutdown();

private:
    // ---- GLFW callbacks ----
    static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
    void onKey(int key, int scancode, int action, int mods);
    void processKeyQueue();

    // ---- Rendering ----
    void render();
    void renderMenuBar();
    void renderToolbar();
    void renderContent();
    void renderSearchBar();
    void renderCommandBar();
    void renderStatusBar();

    // ---- Input bar helpers ----
    void openSearchBar(bool fullSearch);
    void closeSearchBar(bool confirm);
    void openCommandBar();
    void closeCommandBar(bool confirm);

    LogViewer*  viewer_ = nullptr;
    GLFWwindow* window_ = nullptr;

    // Input text buffers
    char searchBuf_[256]  = {};
    char commandBuf_[64]  = {};

    bool showSearchBar_   = false;
    bool showCommandBar_  = false;

    // GLFW key event queue (filled by callback, consumed in main loop)
    std::vector<KeyEvent> keyQueue_;
};

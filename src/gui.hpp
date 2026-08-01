#pragma once

#include "log_reader.hpp"
#include "log_viewer.hpp"

struct GLFWwindow;

/// ImGui + GLFW + OpenGL3 GUI frontend for LogViewer.
class GUI {
public:
    GUI() = default;
    ~GUI();

    GUI(const GUI&) = delete;
    GUI& operator=(const GUI&) = delete;

    /// Create window and initialize ImGui.  Returns true on success.
    bool init();

    /// Run the main loop.
    void run(LogViewer& viewer, const std::string& filepath,
             const ReadOptions& opts, bool followMode);

    /// Tear down ImGui and destroy the window.
    void shutdown();

private:
    // ---- Rendering ----
    void render();
    void renderMenuBar();
    void renderContent();
    void renderSearchBar();
    void renderCommandBar();
    void renderStatusBar();

    // ---- Input ----
    void handleKeyboardShortcuts();
    void activateSearchInput(bool fullSearch);
    void deactivateSearchInput(bool confirm);
    void activateCommandInput();
    void deactivateCommandInput(bool confirm);

    LogViewer*  viewer_ = nullptr;
    GLFWwindow* window_ = nullptr;

    // ImGui InputText buffers
    char searchBuf_[256]  = {};
    char commandBuf_[64]  = {};

    bool showSearchBar_   = false;
    bool showCommandBar_  = false;
};

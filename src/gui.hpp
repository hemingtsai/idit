#pragma once

#include "config.hpp"
#include "log_reader.hpp"
#include "log_viewer.hpp"

#include <vector>

struct GLFWwindow;
struct ImFont;

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

    bool init(Config& config);
    void run(LogViewer& viewer, const std::string& filepath,
             const ReadOptions& opts, Config& config, bool followMode);
    void shutdown();

private:
    // ---- GLFW callbacks ----
    static void keyCallback(GLFWwindow* win, int key, int scancode, int action, int mods);
    void onKey(int key, int scancode, int action, int mods);
    void processKeyQueue();

    // ---- Font loading ----
    void loadFonts(Config& config);

    // ---- Rendering ----
    void render();
    void renderMenuBar();
    void renderToolbar();
    void renderContent();
    void renderSearchBar();
    void renderCommandBar();
    void renderStatusBar();
    void renderSettingsDialog();

    // ---- Input bar helpers ----
    void openSearchBar(bool fullSearch);
    void closeSearchBar(bool confirm);
    void openCommandBar();
    void closeCommandBar(bool confirm);

    LogViewer*  viewer_ = nullptr;
    Config*     config_ = nullptr;
    GLFWwindow* window_ = nullptr;

    // Fonts
    ImFont* uiFont_      = nullptr;
    ImFont* contentFont_  = nullptr;

    // Input text buffers
    char searchBuf_[256]  = {};
    char commandBuf_[64]  = {};

    // Dialog toggles
    bool showSearchBar_   = false;
    bool showCommandBar_  = false;
    bool showSettings_    = false;

    // GLFW key event queue (filled by callback, consumed in main loop)
    std::vector<KeyEvent> keyQueue_;
};

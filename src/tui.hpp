#pragma once

#include "config.hpp"
#include "log_viewer.hpp"
#include "theme.hpp"

#include <cstddef>
#include <string>

/// ncurses-based TUI frontend for LogViewer.
class TUI {
public:
    TUI() = default;
    ~TUI();

    TUI(const TUI&) = delete;
    TUI& operator=(const TUI&) = delete;

    bool init(const Theme& theme);
    void run(LogViewer& viewer, const std::string& filepath,
             const ReadOptions& opts, Config& config, bool followMode);

private:
    // ---- UI helpers ----
    int contentHeight() const;
    int contentWidth()  const;

    // ---- Rendering ----
    void render();
    void drawStatusBar();
    void drawContent();
    void drawSearchBar();
    void drawCommandBar();
    void drawBottomBar();
    void drawLine(int screenRow, int lineIdx);
    void drawLineWithHighlights(int row, int col, const std::string& text,
                                size_t lineIdx, int maxWidth);

    // ---- Settings screen ----
    void drawSettings();
    bool settingsActive_ = false;

    // ---- Input ----
    bool handleInput();
    void handleSearchInput(int ch);
    void handleCommandInput(int ch);

    LogViewer* viewer_ = nullptr;
    Config*    config_ = nullptr;
    Theme theme_;

    int rows_ = 0;
    int cols_ = 0;
};

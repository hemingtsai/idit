#pragma once

#include "log_viewer.hpp"
#include "theme.hpp"

#include <cstddef>
#include <string>

/// ncurses-based TUI frontend for LogViewer.
///
/// Handles terminal I/O and rendering.  All state and logic lives in LogViewer;
/// this class only reads state for drawing and forwards user input as actions.
class TUI {
public:
    TUI() = default;
    ~TUI();

    TUI(const TUI&) = delete;
    TUI& operator=(const TUI&) = delete;

    /// Initialize ncurses and apply the given theme.
    bool init(const Theme& theme);

    /// Run the main loop: open file, poll, render, handle input.
    void run(LogViewer& viewer, const std::string& filepath,
             const ReadOptions& opts, bool followMode);

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

    // ---- Input ----
    bool handleInput();
    void handleSearchInput(int ch);
    void handleCommandInput(int ch);

    LogViewer* viewer_ = nullptr;
    Theme theme_;

    int rows_ = 0;
    int cols_ = 0;
};

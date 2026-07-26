#pragma once

#include <string>

/// Color pair indices used throughout the TUI.
enum ColorPair : short {
    CP_DEFAULT = 1,
    CP_STATUS_BAR,
    CP_LINE_NUMBER,
    CP_HIGHLIGHT,       // Current line highlight
    CP_SEARCH_MATCH,    // Search result highlight
    CP_TRUNCATION,      // Truncation marker '>'
    CP_SEARCH_BAR,      // Search input bar
    CP_SEARCH_BAR_ERROR,// Search bar when no match
};

/// Theme configuration loaded from a file.
struct Theme {
    // Each field is an ncurses color name (e.g., "black", "white", "red", ...)
    std::string background      = "black";
    std::string foreground      = "white";
    std::string status_bar_bg   = "blue";
    std::string status_bar_fg   = "white";
    std::string line_number_fg  = "green";
    std::string highlight_bg    = "yellow";
    std::string highlight_fg    = "black";
    std::string search_match_bg = "red";
    std::string search_match_fg = "white";
    std::string truncation_bg   = "red";
    std::string truncation_fg   = "white";
    std::string search_bar_bg   = "cyan";
    std::string search_bar_fg   = "black";
};

/// Parse a color string (e.g., "red", "bright_red") to an ncurses COLOR_* constant.
/// Returns -1 on failure.
int parse_color(const std::string& name);

/// Load a theme from a file. Returns true on success.
bool load_theme(const std::string& path, Theme& theme);

/// Apply a theme to ncurses (initialize all color pairs).
void apply_theme(const Theme& theme);

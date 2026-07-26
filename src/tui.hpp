#pragma once

#include "log_reader.hpp"
#include "theme.hpp"

#include <cstddef>
#include <string>
#include <vector>

/// Search state for the TUI.
struct SearchState {
    bool   active      = false;
    std::string pattern;
    std::vector<size_t> match_lines;  // Line indices within current chunk
    ssize_t current_match = -1;       // Index into match_lines (-1 = none)

    void clear() {
        active = false;
        pattern.clear();
        match_lines.clear();
        current_match = -1;
    }
};

/// Main TUI class managing the ncurses interface.
class TUI {
public:
    TUI() = default;
    ~TUI();

    TUI(const TUI&) = delete;
    TUI& operator=(const TUI&) = delete;

    bool init(const Theme& theme);
    void run(const std::string& filepath, const ReadOptions& opts,
             bool follow_mode);

private:
    // --- Rendering ---
    void render();
    void draw_status_bar();
    void draw_content();
    void draw_search_bar();
    void draw_command_bar();
    void draw_bottom_bar();
    void draw_line(int screen_row, int line_idx);

    void draw_line_with_highlights(int row, int col, const std::string& text,
                                    size_t line_idx, int max_width);

    // --- Input handling ---
    bool handle_input();
    void handle_search_input(int ch);
    void handle_command_input(int ch);
    void perform_search();
    void navigate_search(bool forward);

    // --- Navigation ---
    void move_cursor(int delta);
    void scroll_horizontal(int delta);
    void load_next_chunk();
    void load_prev_chunk();
    void go_to_bottom();
    void jump_to_line(int target_line);
    void toggle_follow();

    // Full-search chunk scanning
    void search_forward_chunks();
    void search_backward_chunks();

    // --- Helpers ---
    int  content_height() const;
    int  content_width() const;
    void update_global_base_forward(size_t prev_lines);
    uint64_t global_line(size_t chunk_idx) const;

    // --- State ---
    LogReader reader_;
    Theme theme_;

    int rows_ = 0;
    int cols_ = 0;

    std::vector<std::string> lines_;
    size_t cursor_line_ = 0;
    size_t scroll_x_ = 0;

    /// Global line number (0-based) of the first line in the current chunk.
    uint64_t global_line_base_ = 0;

    bool follow_mode_ = false;
    bool running_ = true;

    SearchState search_;

    /// Whether the current search is full-file (\\) or viewport-only (/).
    bool full_search_ = false;

    /// Command mode: user typed ':' and is entering a line number.
    bool command_mode_ = false;
    std::string command_buf_;

    std::string status_msg_;
    int status_ttl_ = 0;

    void set_status(const std::string& msg, int ttl = 30);
};

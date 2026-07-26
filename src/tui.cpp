#include "tui.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <curses.h>
#include <sstream>
#include <unistd.h>

// ============================================================================
// Initialization
// ============================================================================

TUI::~TUI() {
    if (!isendwin()) {
        endwin();
    }
}

bool TUI::init(const Theme& theme) {
    theme_ = theme;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);

    if (has_colors()) {
        start_color();
        apply_theme(theme_);
    }

    getmaxyx(stdscr, rows_, cols_);
    return true;
}

// ============================================================================
// Main loop
// ============================================================================

void TUI::run(const std::string& filepath, const ReadOptions& opts,
              bool follow_mode) {
    follow_mode_ = follow_mode;

    if (!reader_.open(filepath, opts)) {
        endwin();
        fprintf(stderr, "Error: Cannot open file '%s': %s\n",
                filepath.c_str(), strerror(errno));
        return;
    }

    lines_ = reader_.read_initial();
    cursor_line_ = 0;
    scroll_x_ = 0;
    global_line_base_ = 0;

    while (running_) {
        if (follow_mode_) {
            auto new_lines = reader_.reload();
            if (!new_lines.empty()) {
                lines_.insert(lines_.end(), new_lines.begin(), new_lines.end());
                // Keep cursor at bottom if already at bottom
                if (cursor_line_ >= lines_.size() - new_lines.size() - 1) {
                    cursor_line_ = lines_.size() > 0 ? lines_.size() - 1 : 0;
                }
            }
        }

        getmaxyx(stdscr, rows_, cols_);
        render();
        handle_input();

        if (status_ttl_ > 0) {
            status_ttl_--;
            if (status_ttl_ == 0) {
                status_msg_.clear();
            }
        }
    }
}

// ============================================================================
// Helpers
// ============================================================================

int TUI::content_height() const {
    int reserved = 2; // status bar + bottom bar
    if (search_.active) reserved++;
    if (command_mode_) reserved++;
    return std::max(1, rows_ - reserved);
}

int TUI::content_width() const {
    return std::max(20, cols_ - 7);
}

uint64_t TUI::global_line(size_t chunk_idx) const {
    return global_line_base_ + static_cast<uint64_t>(chunk_idx);
}

void TUI::update_global_base_forward(size_t prev_lines) {
    global_line_base_ += static_cast<uint64_t>(prev_lines);
}

// ============================================================================
// Rendering
// ============================================================================

void TUI::render() {
    erase();

    if (has_colors()) {
        draw_status_bar();
        draw_content();
        if (search_.active) draw_search_bar();
        if (command_mode_) draw_command_bar();
        draw_bottom_bar();
    } else {
        mvprintw(0, 0, "=== idit ===");
        draw_content();
    }

    refresh();
}

void TUI::draw_status_bar() {
    int attr = COLOR_PAIR(CP_STATUS_BAR);
    attron(attr);
    mvhline(0, 0, ' ', cols_);

    std::ostringstream oss;
    oss << " " << reader_.path()
        << "  [Chunk lines: " << lines_.size()
        << " | Global L" << (global_line_base_ + 1)
        << " | Bytes: " << reader_.chunk_start() << "-" << reader_.chunk_end()
        << " / " << reader_.file_size();

    if (reader_.has_prev()) oss << " | <";
    else oss << " |  ";

    if (reader_.has_next()) oss << ">";
    else oss << " ";

    if (follow_mode_) oss << " | FOLLOW";

    oss << " ]";

    std::string status = oss.str();
    if (status.size() > static_cast<size_t>(cols_)) {
        status.resize(cols_);
    }

    mvaddstr(0, 0, status.c_str());
    attroff(attr);
}

void TUI::draw_content() {
    int height = content_height();
    int width  = content_width();

    if (lines_.empty()) return;

    // Calculate visible range
    size_t start_line = cursor_line_;
    size_t end_line = std::min(lines_.size(), start_line + static_cast<size_t>(height));

    // If near the end, adjust to fill the screen
    if (end_line - start_line < static_cast<size_t>(height) &&
        lines_.size() > static_cast<size_t>(height)) {
        start_line = lines_.size() - static_cast<size_t>(height);
        end_line = lines_.size();
        cursor_line_ = std::max(cursor_line_, start_line);
    }

    int row = 1; // Start after status bar
    int max_row = rows_ - 1; // Leave last row for bottom bar
    if (search_.active) max_row--;
    if (command_mode_) max_row--;

    for (size_t i = start_line; i < end_line && row < max_row; ++i, ++row) {
        draw_line(row, static_cast<int>(i));
    }
}

void TUI::draw_line(int screen_row, int line_idx) {
    int width = content_width();
    int line_num_width = 6;
    const auto& line = lines_[line_idx];

    bool is_cursor = (static_cast<size_t>(line_idx) == cursor_line_);

    // Draw global line number
    int ln_attr = COLOR_PAIR(CP_LINE_NUMBER);
    if (is_cursor) {
        ln_attr = COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD;
    }
    attron(ln_attr);
    uint64_t gline = global_line(static_cast<size_t>(line_idx)) + 1;
    if (gline <= 99999) {
        mvprintw(screen_row, 0, " %5llu ", static_cast<unsigned long long>(gline));
    } else {
        // Overflow — just show the number
        mvprintw(screen_row, 0, " %5llu ", static_cast<unsigned long long>(gline % 100000));
    }
    attroff(ln_attr);

    // Build visible portion
    std::string visible;
    bool truncated = false;

    if (scroll_x_ < line.size()) {
        size_t avail = static_cast<size_t>(width);
        size_t remaining = line.size() - scroll_x_;
        if (remaining > avail) {
            visible = line.substr(scroll_x_, avail - 1);
            truncated = true;
        } else {
            visible = line.substr(scroll_x_);
        }
    }

    // Draw line content background
    if (is_cursor) {
        attron(COLOR_PAIR(CP_HIGHLIGHT));
        for (int x = line_num_width; x < cols_; ++x) {
            mvaddch(screen_row, x, ' ');
        }
    }

    // Draw text with search highlights
    if (!visible.empty()) {
        draw_line_with_highlights(screen_row, line_num_width, visible,
                                   static_cast<size_t>(line_idx), width);
    } else if (truncated || scroll_x_ >= line.size()) {
        if (is_cursor) {
            mvaddch(screen_row, line_num_width, ' ' | COLOR_PAIR(CP_HIGHLIGHT));
        }
    }

    // Truncation marker
    if (truncated) {
        int marker_col = line_num_width + width - 1;
        if (marker_col < cols_) {
            attron(COLOR_PAIR(CP_TRUNCATION) | A_REVERSE);
            mvaddch(screen_row, marker_col, '>');
            attroff(COLOR_PAIR(CP_TRUNCATION) | A_REVERSE);
        }
    }

    if (is_cursor) {
        attroff(COLOR_PAIR(CP_HIGHLIGHT));
    }
}

void TUI::draw_line_with_highlights(int row, int col, const std::string& text,
                                      size_t line_idx, int max_width) {
    if (!search_.active || search_.pattern.empty()) {
        mvaddnstr(row, col, text.c_str(), max_width);
        return;
    }

    auto it = std::find(search_.match_lines.begin(), search_.match_lines.end(), line_idx);
    bool has_match = (it != search_.match_lines.end());

    if (!has_match) {
        mvaddnstr(row, col, text.c_str(), max_width);
        return;
    }

    bool is_current = false;
    if (search_.current_match >= 0 &&
        static_cast<size_t>(search_.current_match) < search_.match_lines.size()) {
        is_current = (search_.match_lines[search_.current_match] == line_idx);
    }

    const std::string& pattern = search_.pattern;
    int cur_col = col;
    size_t pos = 0;
    std::string lower_text = text;
    std::string lower_pattern = pattern;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);

    while (pos < text.size() && cur_col < col + max_width) {
        size_t found = lower_text.find(lower_pattern, pos);
        if (found == std::string::npos || found >= text.size()) {
            int remaining = std::min(static_cast<int>(text.size() - pos),
                                     col + max_width - cur_col);
            if (remaining > 0) {
                mvaddnstr(row, cur_col, text.c_str() + pos, remaining);
            }
            break;
        }

        if (found > pos) {
            int before_len = std::min(static_cast<int>(found - pos),
                                      col + max_width - cur_col);
            if (before_len > 0) {
                mvaddnstr(row, cur_col, text.c_str() + pos, before_len);
                cur_col += before_len;
            }
        }

        if (cur_col < col + max_width) {
            int match_attr = COLOR_PAIR(CP_SEARCH_MATCH);
            if (is_current) match_attr |= A_BOLD;
            attron(match_attr);
            int match_len = std::min(static_cast<int>(pattern.size()),
                                     col + max_width - cur_col);
            mvaddnstr(row, cur_col, text.c_str() + found, match_len);
            attroff(match_attr);
            cur_col += match_len;
        }

        pos = found + pattern.size();
    }
}

void TUI::draw_search_bar() {
    int row = 1 + content_height();
    int attr = search_.match_lines.empty()
                   ? COLOR_PAIR(CP_SEARCH_BAR_ERROR)
                   : COLOR_PAIR(CP_SEARCH_BAR);

    attron(attr);
    mvhline(row, 0, ' ', cols_);

    std::ostringstream oss;
    oss << (full_search_ ? " \\" : " /") << search_.pattern;

    if (!search_.match_lines.empty()) {
        oss << "   Match " << (search_.current_match + 1)
            << "/" << search_.match_lines.size();
        if (full_search_) oss << " (scanning)";
    } else if (!search_.pattern.empty()) {
        oss << "   [No matches]";
        if (full_search_) oss << " — try n/N to scan";
    }

    std::string bar = oss.str();
    if (bar.size() > static_cast<size_t>(cols_)) bar.resize(cols_);
    mvaddstr(row, 0, bar.c_str());
    attroff(attr);
}

void TUI::draw_command_bar() {
    int row = 1 + content_height();
    if (search_.active) row++;

    int attr = COLOR_PAIR(CP_SEARCH_BAR);
    attron(attr);
    mvhline(row, 0, ' ', cols_);

    std::string bar = " :" + command_buf_ + "_";
    if (bar.size() > static_cast<size_t>(cols_)) bar.resize(cols_);
    mvaddstr(row, 0, bar.c_str());
    attroff(attr);
}

void TUI::draw_bottom_bar() {
    int row = rows_ - 1;
    int attr = COLOR_PAIR(CP_STATUS_BAR);
    attron(attr);
    mvhline(row, 0, ' ', cols_);

    std::string msg;
    if (!status_msg_.empty()) {
        msg = " " + status_msg_;
    } else {
        msg = " q:quit  j/k:move  h/l:scroll  f:follow  /:search  "
              "\\:full  n/N:match  PgUp/Dn:chunk  g/G:top/bot  ::jump  "
              "0/$:line-start/end";
    }

    if (msg.size() > static_cast<size_t>(cols_)) msg.resize(cols_);
    mvaddstr(row, 0, msg.c_str());
    attroff(attr);
}

// ============================================================================
// Input handling
// ============================================================================

bool TUI::handle_input() {
    int ch = getch();
    if (ch == ERR) return false;

    if (command_mode_) {
        handle_command_input(ch);
        return true;
    }

    if (search_.active) {
        handle_search_input(ch);
        return true;
    }

    switch (ch) {
    case 'q':
    case 'Q':
        running_ = false;
        break;

    case 'j':
    case KEY_DOWN:
        move_cursor(1);
        break;

    case 'k':
    case KEY_UP:
        move_cursor(-1);
        break;

    case 'l':
    case KEY_RIGHT:
        scroll_horizontal(3);
        break;

    case 'h':
    case KEY_LEFT:
        scroll_horizontal(-3);
        break;

    case 'g':
        cursor_line_ = 0;
        scroll_x_ = 0;
        break;

    case 'G':
        go_to_bottom();
        break;

    case KEY_NPAGE:
    case 4: // Ctrl+D
        load_next_chunk();
        break;

    case KEY_PPAGE:
    case 2: // Ctrl+B
        load_prev_chunk();
        break;

    case 'f':
    case 'F':
        toggle_follow();
        break;

    case '/':
        full_search_ = false;
        search_.active = true;
        search_.pattern.clear();
        search_.match_lines.clear();
        search_.current_match = -1;
        set_status("Search (current viewport)");
        break;

    case '\\':
        full_search_ = true;
        search_.active = true;
        search_.pattern.clear();
        search_.match_lines.clear();
        search_.current_match = -1;
        set_status("Full search (all chunks)");
        break;

    case ':':
        command_mode_ = true;
        command_buf_.clear();
        set_status("Jump to line (Enter to confirm, Esc to cancel)");
        break;

    case 'n':
        navigate_search(true);
        break;

    case 'N':
        navigate_search(false);
        break;

    case 'r':
    case 'R':
        lines_ = reader_.read_at(reader_.chunk_start());
        cursor_line_ = std::min(cursor_line_, lines_.size() > 0 ? lines_.size() - 1 : 0);
        set_status("Reloaded");
        break;

    case '0':
        scroll_x_ = 0;
        break;

    case '$':
        if (cursor_line_ < lines_.size()) {
            size_t line_len = lines_[cursor_line_].size();
            if (line_len > static_cast<size_t>(content_width())) {
                scroll_x_ = line_len - content_width() + 1;
            }
        }
        break;

    case KEY_RESIZE:
        getmaxyx(stdscr, rows_, cols_);
        break;

    default:
        break;
    }

    return true;
}

void TUI::handle_search_input(int ch) {
    switch (ch) {
    case 27: // Escape
        search_.active = false;
        full_search_ = false;
        set_status("Search cancelled");
        break;

    case '\n':
    case KEY_ENTER:
        perform_search();
        // Full search: if no match in current chunk, scan forward
        if (full_search_ && search_.match_lines.empty()) {
            search_forward_chunks();
        }
        if (!search_.match_lines.empty()) {
            search_.current_match = 0;
            cursor_line_ = search_.match_lines[0];
        }
        set_status("");
        break;

    case KEY_BACKSPACE:
    case 127:
    case '\b':
        if (!search_.pattern.empty()) {
            search_.pattern.pop_back();
            perform_search();
            if (!search_.match_lines.empty()) {
                search_.current_match = 0;
            }
        }
        break;

    default:
        if (ch >= 32 && ch < 127) {
            search_.pattern += static_cast<char>(ch);
            perform_search();
            if (!search_.match_lines.empty()) {
                search_.current_match = 0;
            }
        }
        break;
    }
}

void TUI::handle_command_input(int ch) {
    switch (ch) {
    case 27: // Escape
        command_mode_ = false;
        command_buf_.clear();
        set_status("Cancelled");
        break;

    case '\n':
    case KEY_ENTER: {
        if (!command_buf_.empty()) {
            int target = std::stoi(command_buf_);
            command_mode_ = false;
            command_buf_.clear();
            if (target >= 1) {
                jump_to_line(target);
            } else {
                set_status("Invalid line number");
            }
        } else {
            command_mode_ = false;
            command_buf_.clear();
        }
        break;
    }

    case KEY_BACKSPACE:
    case 127:
    case '\b':
        if (!command_buf_.empty()) {
            command_buf_.pop_back();
        }
        break;

    default:
        if (ch >= '0' && ch <= '9') {
            command_buf_ += static_cast<char>(ch);
        }
        break;
    }
}

void TUI::perform_search() {
    search_.match_lines.clear();
    search_.current_match = -1;

    if (search_.pattern.empty()) return;

    std::string lower_pattern = search_.pattern;
    std::transform(lower_pattern.begin(), lower_pattern.end(),
                   lower_pattern.begin(), ::tolower);

    for (size_t i = 0; i < lines_.size(); ++i) {
        std::string lower_line = lines_[i];
        std::transform(lower_line.begin(), lower_line.end(),
                       lower_line.begin(), ::tolower);
        if (lower_line.find(lower_pattern) != std::string::npos) {
            search_.match_lines.push_back(i);
        }
    }

    if (!search_.match_lines.empty()) {
        cursor_line_ = search_.match_lines[0];
        set_status("Found " + std::to_string(search_.match_lines.size()) + " match(es)");
    } else {
        set_status("No matches found", 60);
    }
}

void TUI::navigate_search(bool forward) {
    if (!search_.active || search_.pattern.empty()) return;

    if (!search_.match_lines.empty()) {
        // Navigate within current chunk
        if (forward) {
            search_.current_match++;
            if (search_.current_match >= static_cast<ssize_t>(search_.match_lines.size())) {
                // Last match in chunk — if full search, try next chunk
                if (full_search_) {
                    search_forward_chunks();
                    return;
                }
                search_.current_match = 0; // wrap in viewport mode
            }
        } else {
            search_.current_match--;
            if (search_.current_match < 0) {
                // First match in chunk — if full search, try previous chunk
                if (full_search_) {
                    search_backward_chunks();
                    return;
                }
                search_.current_match = static_cast<ssize_t>(search_.match_lines.size()) - 1;
            }
        }

        if (search_.current_match >= 0 &&
            static_cast<size_t>(search_.current_match) < search_.match_lines.size()) {
            cursor_line_ = search_.match_lines[search_.current_match];
            scroll_x_ = 0;
        }
    } else if (full_search_) {
        // No matches in current chunk but full search active — try forward
        if (forward) {
            search_forward_chunks();
        } else {
            search_backward_chunks();
        }
    }
}

// ---------------------------------------------------------------------------
// Full-file search helpers — scan forward/backward across chunk boundaries
// ---------------------------------------------------------------------------

void TUI::search_forward_chunks() {
    if (!full_search_ || !reader_.has_next()) {
        set_status("No more matches in file");
        return;
    }

    int chunks_scanned = 0;
    const int max_scan = 500; // safety limit

    while (reader_.has_next() && chunks_scanned < max_scan) {
        size_t prev_count = lines_.size();
        auto new_lines = reader_.read_forward();
        update_global_base_forward(prev_count);
        lines_ = std::move(new_lines);
        cursor_line_ = 0;
        scroll_x_ = 0;
        chunks_scanned++;

        perform_search();
        if (!search_.match_lines.empty()) {
            search_.current_match = 0;
            cursor_line_ = search_.match_lines[0];
            set_status("Found in chunk L" + std::to_string(global_line_base_ + 1));
            return;
        }
    }

    set_status("Scanned " + std::to_string(chunks_scanned) +
               " chunks — no match");
}

void TUI::search_backward_chunks() {
    if (!full_search_ || !reader_.has_prev()) {
        set_status("No more matches in file");
        return;
    }

    int chunks_scanned = 0;
    const int max_scan = 500;

    while (reader_.has_prev() && chunks_scanned < max_scan) {
        auto new_lines = reader_.read_backward();
        if (new_lines.empty()) break;

        if (global_line_base_ >= new_lines.size()) {
            global_line_base_ -= static_cast<uint64_t>(new_lines.size());
        } else {
            global_line_base_ = 0;
        }

        lines_ = std::move(new_lines);
        cursor_line_ = lines_.size() > 0 ? lines_.size() - 1 : 0;
        scroll_x_ = 0;
        chunks_scanned++;

        perform_search();
        if (!search_.match_lines.empty()) {
            // Go to the last match in this chunk (since we came from the end)
            search_.current_match = static_cast<ssize_t>(search_.match_lines.size()) - 1;
            cursor_line_ = search_.match_lines[search_.current_match];
            set_status("Found in chunk L" + std::to_string(global_line_base_ + 1));
            return;
        }
    }

    set_status("Scanned " + std::to_string(chunks_scanned) +
               " chunks — no match");
}

// ============================================================================
// Navigation
// ============================================================================

void TUI::move_cursor(int delta) {
    if (lines_.empty()) return;

    if (delta > 0) {
        if (cursor_line_ + static_cast<size_t>(delta) >= lines_.size()) {
            if (reader_.has_next()) {
                load_next_chunk();
                cursor_line_ = 0;
            } else {
                cursor_line_ = lines_.size() - 1;
            }
        } else {
            cursor_line_ += static_cast<size_t>(delta);
        }
    } else {
        if (cursor_line_ < static_cast<size_t>(-delta)) {
            if (reader_.has_prev()) {
                load_prev_chunk();
                if (!lines_.empty()) {
                    cursor_line_ = lines_.size() - 1;
                }
            } else {
                cursor_line_ = 0;
            }
        } else {
            cursor_line_ = static_cast<size_t>(
                static_cast<ssize_t>(cursor_line_) + delta);
        }
    }
    scroll_x_ = 0;
}

void TUI::scroll_horizontal(int delta) {
    if (delta > 0) {
        scroll_x_ += static_cast<size_t>(delta);
    } else {
        size_t abs_delta = static_cast<size_t>(-delta);
        if (scroll_x_ > abs_delta) {
            scroll_x_ -= abs_delta;
        } else {
            scroll_x_ = 0;
        }
    }
}

void TUI::load_next_chunk() {
    if (!reader_.has_next()) {
        set_status("Already at end of file");
        return;
    }

    size_t prev_count = lines_.size();
    auto new_lines = reader_.read_forward();

    update_global_base_forward(prev_count);

    lines_ = std::move(new_lines);
    cursor_line_ = 0;
    scroll_x_ = 0;

    if (search_.active && !search_.pattern.empty()) {
        perform_search();
        if (!search_.match_lines.empty()) {
            search_.current_match = 0;
        }
    }

    set_status("Chunk L" + std::to_string(global_line_base_ + 1) +
               " (" + std::to_string(lines_.size()) + " lines)");
}

void TUI::load_prev_chunk() {
    if (!reader_.has_prev()) {
        set_status("Already at beginning of file");
        return;
    }

    auto new_lines = reader_.read_backward();
    if (new_lines.empty()) {
        set_status("Already at beginning of file");
        return;
    }

    // Update global base: subtract the new chunk's line count
    if (global_line_base_ >= new_lines.size()) {
        global_line_base_ -= static_cast<uint64_t>(new_lines.size());
    } else {
        global_line_base_ = 0;
    }

    lines_ = std::move(new_lines);
    cursor_line_ = lines_.size() > 0 ? lines_.size() - 1 : 0;
    scroll_x_ = 0;

    if (search_.active && !search_.pattern.empty()) {
        perform_search();
        if (!search_.match_lines.empty()) {
            search_.current_match = 0;
        }
    }

    set_status("Chunk L" + std::to_string(global_line_base_ + 1) +
               " (" + std::to_string(lines_.size()) + " lines)");
}

void TUI::go_to_bottom() {
    if (reader_.file_size() == 0) return;

    // Read the last ~64 KB of the file (enough to fill several screens)
    uint64_t end_offset = reader_.file_size();
    uint64_t read_size = 65536;
    uint64_t start_offset = (end_offset > read_size) ? end_offset - read_size : 0;

    lines_ = reader_.read_at(start_offset);
    cursor_line_ = lines_.size() > 0 ? lines_.size() - 1 : 0;
    scroll_x_ = 0;

    // Estimate global line number (rough, since we don't know exact count)
    if (!lines_.empty() && reader_.file_size() > 0) {
        uint64_t total = 0;
        for (const auto& l : lines_) total += l.size() + 1;
        uint64_t avg_len = total / lines_.size();
        if (avg_len < 1) avg_len = 1;
        global_line_base_ = start_offset / avg_len;
    }

    set_status("End of file (~L" + std::to_string(global_line_base_ + 1) + ")");
}

void TUI::jump_to_line(int target_line) {
    if (reader_.file_size() == 0 || target_line < 1) {
        set_status("Invalid target");
        return;
    }

    // Estimate byte offset from target line number
    // Use a rough estimate: total_lines ≈ file_size / avg_line_len
    // We don't know the exact line count, so use avg ~ (min+max)/2
    // For a 50GB file with ~100 char lines, this is approximate.
    // First try: use the current lines as a sample for avg line length.
    uint64_t avg_len = 80; // reasonable default
    if (!lines_.empty()) {
        uint64_t total = 0;
        for (const auto& l : lines_) total += l.size() + 1; // +1 for newline
        avg_len = total / lines_.size();
    }
    if (avg_len < 1) avg_len = 1;

    uint64_t est_total = reader_.file_size() / avg_len;
    uint64_t target_0based = static_cast<uint64_t>(target_line - 1);
    double frac = static_cast<double>(target_0based) / std::max(uint64_t(1), est_total);
    if (frac > 1.0) frac = 1.0;

    uint64_t offset = static_cast<uint64_t>(frac * reader_.file_size());

    // Align to line start
    lines_ = reader_.read_at(offset);

    if (lines_.empty()) {
        set_status("Jump failed — empty region");
        return;
    }

    cursor_line_ = 0;
    scroll_x_ = 0;
    global_line_base_ = target_0based;

    set_status("Jumped to ~L" + std::to_string(target_line));
}

void TUI::toggle_follow() {
    follow_mode_ = !follow_mode_;
    if (follow_mode_) {
        if (!lines_.empty()) {
            cursor_line_ = lines_.size() - 1;
        }
        set_status("Follow mode ON");
    } else {
        set_status("Follow mode OFF");
    }
}

void TUI::set_status(const std::string& msg, int ttl) {
    status_msg_ = msg;
    status_ttl_ = ttl;
}

#include "tui.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
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
    timeout(100);   // 100 ms poll interval

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

void TUI::run(LogViewer& viewer, const std::string& filepath,
              const ReadOptions& opts, Config& config, bool followMode) {
    viewer_ = &viewer;
    config_ = &config;

    if (!viewer_->open(filepath, opts)) {
        endwin();
        fprintf(stderr, "Error: Cannot open file '%s': %s\n",
                filepath.c_str(), strerror(errno));
        return;
    }

    if (followMode) {
        viewer_->toggleFollow();
    }

    while (viewer_->isRunning()) {
        viewer_->update();
        getmaxyx(stdscr, rows_, cols_);
        render();
        handleInput();
    }
}

// ============================================================================
// UI helpers
// ============================================================================

int TUI::contentHeight() const {
    int reserved = 2;   // status bar + bottom bar
    if (viewer_->isSearchActive()) ++reserved;
    if (viewer_->isCommandMode()) ++reserved;
    return std::max(1, rows_ - reserved);
}

int TUI::contentWidth() const {
    return std::max(20, cols_ - 7);   // 6 for line number + 1 margin
}

// ============================================================================
// Rendering
// ============================================================================

void TUI::render() {
    erase();

    if (settingsActive_) {
        drawSettings();
        refresh();
        return;
    }

    if (has_colors()) {
        drawStatusBar();
        drawContent();
        if (viewer_->isSearchActive()) drawSearchBar();
        if (viewer_->isCommandMode()) drawCommandBar();
        drawBottomBar();
    } else {
        mvprintw(0, 0, "=== idit ===");
        drawContent();
    }

    refresh();
}

void TUI::drawStatusBar() {
    int attr = COLOR_PAIR(CP_STATUS_BAR);
    attron(attr);
    mvhline(0, 0, ' ', cols_);

    std::ostringstream oss;
    oss << " " << viewer_->filePath()
        << "  [Chunk lines: " << viewer_->lineCount()
        << " | Global L" << (viewer_->globalLineBase() + 1)
        << " | Bytes: " << viewer_->chunkStart() << "-" << viewer_->chunkEnd()
        << " / " << viewer_->fileSize();

    if (viewer_->hasPrev()) oss << " | <";
    else                     oss << " |  ";

    if (viewer_->hasNext()) oss << ">";
    else                     oss << " ";

    if (viewer_->followMode()) oss << " | FOLLOW";

    oss << " ]";

    std::string status = oss.str();
    if (status.size() > static_cast<size_t>(cols_)) {
        status.resize(static_cast<size_t>(cols_));
    }

    mvaddstr(0, 0, status.c_str());
    attroff(attr);
}

void TUI::drawContent() {
    int height = contentHeight();
    int width  = contentWidth();

    if (viewer_->lines().empty()) return;

    size_t total      = viewer_->lineCount();
    size_t cursorLine = viewer_->cursorLine();

    // Compute visible range: start from cursorLine, pin to bottom if near end
    size_t startLine = cursorLine;
    if (startLine + static_cast<size_t>(height) > total) {
        startLine = (total > static_cast<size_t>(height))
                        ? total - static_cast<size_t>(height)
                        : 0;
    }
    size_t endLine = std::min(total, startLine + static_cast<size_t>(height));

    int row     = 1;       // start after status bar
    int max_row = rows_ - 1;
    if (viewer_->isSearchActive()) --max_row;
    if (viewer_->isCommandMode()) --max_row;

    for (size_t i = startLine; i < endLine && row < max_row; ++i, ++row) {
        drawLine(row, static_cast<int>(i));
    }
}

void TUI::drawLine(int screenRow, int lineIdx) {
    int width         = contentWidth();
    int lineNumWidth  = 6;
    const auto& lines = viewer_->lines();
    const auto& line  = lines[static_cast<size_t>(lineIdx)];
    bool isCursor     = (static_cast<size_t>(lineIdx) == viewer_->cursorLine());
    size_t scrollX    = viewer_->scrollX();

    // ---- Line number ----
    int lnAttr = COLOR_PAIR(CP_LINE_NUMBER);
    if (isCursor) {
        lnAttr = COLOR_PAIR(CP_HIGHLIGHT) | A_BOLD;
    }
    attron(lnAttr);
    uint64_t gline = viewer_->globalLine(static_cast<size_t>(lineIdx)) + 1;
    if (gline <= 99999) {
        mvprintw(screenRow, 0, " %5llu ", static_cast<unsigned long long>(gline));
    } else {
        mvprintw(screenRow, 0, " %5llu ",
                 static_cast<unsigned long long>(gline % 100000));
    }
    attroff(lnAttr);

    // ---- Visible portion ----
    std::string visible;
    bool truncated = false;

    if (scrollX < line.size()) {
        size_t avail     = static_cast<size_t>(width);
        size_t remaining = line.size() - scrollX;
        if (remaining > avail) {
            visible   = line.substr(scrollX, avail - 1);
            truncated = true;
        } else {
            visible = line.substr(scrollX);
        }
    }

    // Cursor line background
    if (isCursor) {
        attron(COLOR_PAIR(CP_HIGHLIGHT));
        for (int x = lineNumWidth; x < cols_; ++x) {
            mvaddch(screenRow, x, ' ');
        }
    }

    // Draw text (with search highlights if applicable)
    if (!visible.empty()) {
        drawLineWithHighlights(screenRow, lineNumWidth, visible,
                               static_cast<size_t>(lineIdx), width);
    } else if (truncated || scrollX >= line.size()) {
        if (isCursor) {
            mvaddch(screenRow, lineNumWidth, ' ' | COLOR_PAIR(CP_HIGHLIGHT));
        }
    }

    // Truncation marker
    if (truncated) {
        int markerCol = lineNumWidth + width - 1;
        if (markerCol < cols_) {
            attron(COLOR_PAIR(CP_TRUNCATION) | A_REVERSE);
            mvaddch(screenRow, markerCol, '>');
            attroff(COLOR_PAIR(CP_TRUNCATION) | A_REVERSE);
        }
    }

    if (isCursor) {
        attroff(COLOR_PAIR(CP_HIGHLIGHT));
    }
}

void TUI::drawLineWithHighlights(int row, int col, const std::string& text,
                                 size_t lineIdx, int maxWidth) {
    if (!viewer_->isSearchActive() || viewer_->searchPattern().empty()) {
        mvaddnstr(row, col, text.c_str(), maxWidth);
        return;
    }

    bool hasMatch    = viewer_->isLineSearchMatch(lineIdx);
    bool isCurrent   = viewer_->isCurrentSearchMatch(lineIdx);

    if (!hasMatch) {
        mvaddnstr(row, col, text.c_str(), maxWidth);
        return;
    }

    // Use cached lower-case pattern from LogViewer
    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
    const std::string& lowerPat = viewer_->searchLowerPattern();

    int curCol  = col;
    size_t pos  = 0;

    while (pos < text.size() && curCol < col + maxWidth) {
        size_t found = lowerText.find(lowerPat, pos);
        if (found == std::string::npos || found >= text.size()) {
            int remaining = std::min(static_cast<int>(text.size() - pos),
                                     col + maxWidth - curCol);
            if (remaining > 0) {
                mvaddnstr(row, curCol, text.c_str() + pos, remaining);
            }
            break;
        }

        // Text before match
        if (found > pos) {
            int beforeLen = std::min(static_cast<int>(found - pos),
                                     col + maxWidth - curCol);
            if (beforeLen > 0) {
                mvaddnstr(row, curCol, text.c_str() + pos, beforeLen);
                curCol += beforeLen;
            }
        }

        // Match text (highlighted)
        if (curCol < col + maxWidth) {
            int matchAttr = COLOR_PAIR(CP_SEARCH_MATCH);
            if (isCurrent) matchAttr |= A_BOLD;
            attron(matchAttr);
            int matchLen = std::min(static_cast<int>(viewer_->searchPattern().size()),
                                    col + maxWidth - curCol);
            mvaddnstr(row, curCol, text.c_str() + found, matchLen);
            attroff(matchAttr);
            curCol += matchLen;
        }

        pos = found + viewer_->searchPattern().size();
    }
}

void TUI::drawSearchBar() {
    int row  = 1 + contentHeight();
    int attr = viewer_->searchMatches().empty()
                   ? COLOR_PAIR(CP_SEARCH_BAR_ERROR)
                   : COLOR_PAIR(CP_SEARCH_BAR);

    attron(attr);
    mvhline(row, 0, ' ', cols_);

    std::ostringstream oss;
    oss << (viewer_->isFullSearch() ? " \\" : " /")
        << viewer_->searchPattern();

    if (!viewer_->searchMatches().empty()) {
        oss << "   Match " << (viewer_->searchCurrentMatch() + 1)
            << "/" << viewer_->searchMatches().size();
        if (viewer_->isFullSearch()) oss << " (scanning)";
    } else if (!viewer_->searchPattern().empty()) {
        oss << "   [No matches]";
        if (viewer_->isFullSearch()) oss << " — try n/N to scan";
    }

    std::string bar = oss.str();
    if (bar.size() > static_cast<size_t>(cols_)) bar.resize(static_cast<size_t>(cols_));
    mvaddstr(row, 0, bar.c_str());
    attroff(attr);
}

void TUI::drawCommandBar() {
    int row = 1 + contentHeight();
    if (viewer_->isSearchActive()) ++row;

    int attr = COLOR_PAIR(CP_SEARCH_BAR);
    attron(attr);
    mvhline(row, 0, ' ', cols_);

    std::string bar = " :" + viewer_->commandBuffer() + "_";
    if (bar.size() > static_cast<size_t>(cols_)) bar.resize(static_cast<size_t>(cols_));
    mvaddstr(row, 0, bar.c_str());
    attroff(attr);
}

void TUI::drawBottomBar() {
    int row  = rows_ - 1;
    int attr = COLOR_PAIR(CP_STATUS_BAR);
    attron(attr);
    mvhline(row, 0, ' ', cols_);

    std::string msg;
    if (!viewer_->statusMsg().empty()) {
        msg = " " + viewer_->statusMsg();
    } else {
        msg = " q:quit  j/k:move  h/l:scroll  f:follow  /:search  "
              "\\:full  n/N:match  PgUp/Dn:chunk  g/G:top/bot  ::jump  "
              "0/$:line-start/end";
    }

    if (msg.size() > static_cast<size_t>(cols_)) msg.resize(static_cast<size_t>(cols_));
    mvaddstr(row, 0, msg.c_str());
    attroff(attr);
}

// ============================================================================
// Input handling
// ============================================================================

bool TUI::handleInput() {
    int ch = getch();
    if (ch == ERR) return false;

    // Settings screen has its own input handling
    if (settingsActive_) {
        if (ch == 'S' || ch == 's' || ch == 27) { // Esc or S to close
            settingsActive_ = false;
        }
        return true;
    }

    if (viewer_->isCommandMode()) {
        handleCommandInput(ch);
        return true;
    }

    if (viewer_->isSearchActive()) {
        handleSearchInput(ch);
        return true;
    }

    switch (ch) {
    case 'q':
    case 'Q':
        viewer_->quit();
        break;

    case 'j':
    case KEY_DOWN:
        viewer_->moveCursor(1);
        break;

    case 'k':
    case KEY_UP:
        viewer_->moveCursor(-1);
        break;

    case 'l':
    case KEY_RIGHT:
        viewer_->scrollHorizontal(3);
        break;

    case 'h':
    case KEY_LEFT:
        viewer_->scrollHorizontal(-3);
        break;

    case 'g':
        viewer_->goToTop();
        break;

    case 'G':
        viewer_->goToBottom();
        break;

    case KEY_NPAGE:
    case 4:   // Ctrl+D
        viewer_->loadNextChunk();
        break;

    case KEY_PPAGE:
    case 2:   // Ctrl+B
        viewer_->loadPrevChunk();
        break;

    case 'f':
    case 'F':
        viewer_->toggleFollow();
        break;

    case '/':
        viewer_->beginSearch(false);
        break;

    case '\\':
        viewer_->beginSearch(true);
        break;

    case ':':
        viewer_->beginCommand();
        break;

    case 'n':
        viewer_->navigateSearch(true);
        break;

    case 'N':
        viewer_->navigateSearch(false);
        break;

    case 'r':
    case 'R':
        viewer_->reloadChunk();
        break;

    case '0':
        viewer_->scrollToLineStart();
        break;

    case 'S':   // Settings
        settingsActive_ = true;
        break;

    case '$':
        viewer_->scrollToLineEnd();
        // Clamp to actual viewport
        {
            int width = contentWidth();
            size_t maxScroll = 0;
            if (viewer_->cursorLine() < viewer_->lines().size()) {
                size_t len = viewer_->lines()[viewer_->cursorLine()].size();
                maxScroll = (len > static_cast<size_t>(width))
                                ? len - static_cast<size_t>(width) + 1
                                : 0;
            }
            // HACK: directly adjust scrollX_ — LogViewer::scrollToLineEnd
            // doesn't know the viewport width.  We'll fix this later by
            // passing viewport info to the core.
            if (viewer_->scrollX() > maxScroll) {
                viewer_->scrollHorizontal(-static_cast<int>(viewer_->scrollX() - maxScroll));
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

void TUI::handleSearchInput(int ch) {
    switch (ch) {
    case 27:   // Escape
        viewer_->cancelSearch();
        break;

    case '\n':
    case KEY_ENTER:
        viewer_->confirmSearch();
        break;

    case KEY_BACKSPACE:
    case 127:
    case '\b':
        viewer_->popSearchChar();
        break;

    default:
        if (ch >= 32 && ch < 127) {
            viewer_->appendSearchChar(static_cast<char>(ch));
        }
        break;
    }
}

void TUI::handleCommandInput(int ch) {
    switch (ch) {
    case 27:   // Escape
        viewer_->cancelCommand();
        break;

    case '\n':
    case KEY_ENTER:
        viewer_->confirmCommand();
        break;

    case KEY_BACKSPACE:
    case 127:
    case '\b':
        viewer_->popCommandChar();
        break;

    default:
        if (ch >= '0' && ch <= '9') {
            viewer_->appendCommandChar(static_cast<char>(ch));
        }
        break;
    }
}

// ============================================================================
// Settings screen
// ============================================================================

void TUI::drawSettings() {
    int attr = COLOR_PAIR(CP_STATUS_BAR);
    attron(attr);
    mvhline(0, 0, ' ', cols_);
    mvaddstr(0, 2, " Settings ");
    attroff(attr);

    int row = 2;
    int col = 4;
    mvprintw(row, col, "Configuration: ~/.config/idit/config.lua");
    row += 2;

    mvprintw(row, col, "  UI Font:       %-20s  (size: %.0f)",
             config_->settings.uiFont.family.c_str(),
             config_->settings.uiFont.size);
    row++;
    mvprintw(row, col, "  Content Font:  %-20s  (size: %.0f)",
             config_->settings.contentFont.family.c_str(),
             config_->settings.contentFont.size);
    row++;
    mvprintw(row, col, "  Theme:         %-20s",
             config_->settings.theme.c_str());
    row++;
    mvprintw(row, col, "  Window Lines:  %zu",
             config_->settings.windowLines);
    row++;
    mvprintw(row, col, "  Chunk Size:    %zu bytes",
             config_->settings.chunkSize);
    row += 2;

    mvprintw(row, col, "Edit ~/.config/idit/config.lua to change these values.");
    row++;
    mvprintw(row, col, "Themes are in ~/.config/idit/themes/<name>.lua");
    row += 2;

    mvprintw(row, col, "Press S or Esc to close.");
}

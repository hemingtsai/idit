#pragma once

#include "log_reader.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Search state managed by LogViewer.
struct SearchState {
    bool active = false;
    bool fullSearch = false;          // true = \, false = /
    std::string pattern;
    std::string lowerPattern;         // cached lowercase for case-insensitive matching
    std::vector<size_t> matchLines;   // line indices within current chunk
    ssize_t currentMatch = -1;        // index into matchLines (-1 = none)

    void clear() {
        active = false;
        fullSearch = false;
        pattern.clear();
        lowerPattern.clear();
        matchLines.clear();
        currentMatch = -1;
    }
};

/// Command-line mode state (line jump via ':').
struct CommandState {
    bool active = false;
    std::string buffer;

    void clear() {
        active = false;
        buffer.clear();
    }
};

/// Core log viewing logic, independent of any UI framework.
///
/// Holds all mutable state (cursor, scroll, search, command mode, follow mode)
/// and exposes pure-logic actions that frontends (TUI, GUI) call in response to
/// user input.  Frontends read state via const accessors to render.
class LogViewer {
public:
    LogViewer() = default;

    // ---- File operations ------------------------------------------------

    /// Open a log file and read the initial chunk.  Returns true on success.
    bool open(const std::string& path, const ReadOptions& opts = ReadOptions());

    /// Close the current file and reset all state.
    void close();

    bool isOpen() const { return reader_.is_open(); }

    // ---- Per-frame update -----------------------------------------------

    /// Call once per frame.  Polls for new data in follow mode, ticks the
    /// status-message TTL, and handles truncation recovery.
    void update();

    // ---- Read-only state access (for frontend rendering) ----------------

    const std::vector<std::string>& lines()        const { return lines_; }
    size_t  cursorLine()    const { return cursorLine_; }
    size_t  scrollX()       const { return scrollX_; }
    uint64_t globalLineBase() const { return globalLineBase_; }
    uint64_t globalLine(size_t chunkIdx) const {
        return globalLineBase_ + static_cast<uint64_t>(chunkIdx);
    }
    bool followMode() const { return followMode_; }
    bool isRunning()  const { return running_; }

    // File / chunk metadata
    const std::string& filePath()   const { return reader_.path(); }
    uint64_t fileSize()   const { return reader_.file_size(); }
    uint64_t chunkStart() const { return reader_.chunk_start(); }
    uint64_t chunkEnd()   const { return reader_.chunk_end(); }
    bool hasPrev()  const { return reader_.has_prev(); }
    bool hasNext()  const { return reader_.has_next(); }
    size_t lineCount() const { return lines_.size(); }

    // Search state
    bool isSearchActive()       const { return search_.active; }
    bool isFullSearch()         const { return search_.fullSearch; }
    const std::string& searchPattern()      const { return search_.pattern; }
    const std::string& searchLowerPattern() const { return search_.lowerPattern; }
    const std::vector<size_t>& searchMatches()  const { return search_.matchLines; }
    ssize_t searchCurrentMatch()        const { return search_.currentMatch; }
    bool isLineSearchMatch(size_t lineIdx)    const;
    bool isCurrentSearchMatch(size_t lineIdx) const;

    // Command mode
    bool isCommandMode()          const { return command_.active; }
    const std::string& commandBuffer() const { return command_.buffer; }

    // Status message (shown in bottom bar)
    const std::string& statusMsg() const { return statusMsg_; }

    // ---- Actions (called by frontends on user input) --------------------

    // Navigation
    void moveCursor(int delta);
    void scrollHorizontal(int delta);
    void loadNextChunk();
    void loadPrevChunk();
    void goToTop();
    void goToBottom();
    void jumpToLine(uint64_t targetLine);
    void scrollToLineStart();
    void scrollToLineEnd();
    void reloadChunk();

    // Follow mode
    void toggleFollow();

    // Search (character-by-character for TUI, or bulk-set for GUI)
    void beginSearch(bool fullSearch);
    void cancelSearch();
    void appendSearchChar(char ch);
    void popSearchChar();
    void confirmSearch();
    void setSearchPattern(const std::string& pattern);   // for GUI InputText
    void navigateSearch(bool forward);

    // Command mode (line jump)
    void beginCommand();
    void cancelCommand();
    void appendCommandChar(char ch);
    void popCommandChar();
    void confirmCommand();

    // Quit
    void quit() { running_ = false; }

private:
    void performSearch();
    void searchForwardChunks();
    void searchBackwardChunks();
    void setStatus(const std::string& msg, int ttl = 30);

    LogReader reader_;
    ReadOptions opts_;

    std::vector<std::string> lines_;
    size_t   cursorLine_      = 0;
    size_t   scrollX_         = 0;
    uint64_t globalLineBase_  = 0;

    bool followMode_ = false;
    bool running_    = true;

    SearchState  search_;
    CommandState command_;

    std::string statusMsg_;
    int         statusTtl_ = 0;
};

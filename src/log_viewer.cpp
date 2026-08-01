#include "log_viewer.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

// ============================================================================
// File operations
// ============================================================================

bool LogViewer::open(const std::string& path, const ReadOptions& opts) {
    close();
    opts_ = opts;

    if (!reader_.open(path, opts)) {
        return false;
    }

    lines_ = reader_.read_initial();
    cursorLine_     = 0;
    scrollX_        = 0;
    globalLineBase_ = 0;
    followMode_     = false;
    running_        = true;
    search_.clear();
    command_.clear();
    statusMsg_.clear();
    statusTtl_ = 0;

    return true;
}

void LogViewer::close() {
    reader_.close();
    lines_.clear();
    cursorLine_     = 0;
    scrollX_        = 0;
    globalLineBase_ = 0;
    followMode_     = false;
    running_        = false;
    search_.clear();
    command_.clear();
    statusMsg_.clear();
    statusTtl_ = 0;
}

// ============================================================================
// Per-frame update
// ============================================================================

void LogViewer::update() {
    // ---- Follow-mode polling ----
    if (followMode_) {
        auto new_lines = reader_.reload();

        if (reader_.was_truncated()) {
            // File was truncated (logrotate) — reset to beginning
            lines_ = reader_.read_initial();
            cursorLine_     = 0;
            scrollX_        = 0;
            globalLineBase_ = 0;
            setStatus("File truncated, reset to beginning");
        } else if (!new_lines.empty()) {
            bool at_bottom = lines_.empty() ||
                cursorLine_ >= lines_.size() - 1;
            lines_.insert(lines_.end(), new_lines.begin(), new_lines.end());
            if (at_bottom && !lines_.empty()) {
                cursorLine_ = lines_.size() - 1;
            }
        }
    }

    // ---- Status message TTL ----
    if (statusTtl_ > 0) {
        --statusTtl_;
        if (statusTtl_ == 0) {
            statusMsg_.clear();
        }
    }
}

// ============================================================================
// Search helpers
// ============================================================================

bool LogViewer::isLineSearchMatch(size_t lineIdx) const {
    return std::find(search_.matchLines.begin(),
                     search_.matchLines.end(),
                     lineIdx) != search_.matchLines.end();
}

bool LogViewer::isCurrentSearchMatch(size_t lineIdx) const {
    if (search_.currentMatch < 0 ||
        static_cast<size_t>(search_.currentMatch) >= search_.matchLines.size()) {
        return false;
    }
    return search_.matchLines[static_cast<size_t>(search_.currentMatch)] == lineIdx;
}

void LogViewer::performSearch() {
    search_.matchLines.clear();
    search_.currentMatch = -1;

    if (search_.pattern.empty()) return;

    // Cache the lowercase pattern once
    search_.lowerPattern = search_.pattern;
    std::transform(search_.lowerPattern.begin(), search_.lowerPattern.end(),
                   search_.lowerPattern.begin(), ::tolower);

    const std::string& lp = search_.lowerPattern;

    for (size_t i = 0; i < lines_.size(); ++i) {
        std::string lower_line = lines_[i];
        std::transform(lower_line.begin(), lower_line.end(),
                       lower_line.begin(), ::tolower);
        if (lower_line.find(lp) != std::string::npos) {
            search_.matchLines.push_back(i);
        }
    }

    if (!search_.matchLines.empty()) {
        cursorLine_ = search_.matchLines[0];
        setStatus("Found " + std::to_string(search_.matchLines.size()) + " match(es)");
    } else {
        setStatus("No matches found", 60);
    }
}

void LogViewer::searchForwardChunks() {
    if (!search_.fullSearch || !reader_.has_next()) {
        setStatus("No more matches in file");
        return;
    }

    int chunks_scanned = 0;
    const int max_scan = 500; // safety limit

    while (reader_.has_next() && chunks_scanned < max_scan) {
        size_t prev_count = lines_.size();
        auto new_lines = reader_.read_forward();
        globalLineBase_ += static_cast<uint64_t>(prev_count);
        lines_ = std::move(new_lines);
        cursorLine_ = 0;
        scrollX_ = 0;
        ++chunks_scanned;

        performSearch();
        if (!search_.matchLines.empty()) {
            search_.currentMatch = 0;
            cursorLine_ = search_.matchLines[0];
            setStatus("Found in chunk L" + std::to_string(globalLineBase_ + 1));
            return;
        }
    }

    setStatus("Scanned " + std::to_string(chunks_scanned) + " chunks — no match");
}

void LogViewer::searchBackwardChunks() {
    if (!search_.fullSearch || !reader_.has_prev()) {
        setStatus("No more matches in file");
        return;
    }

    int chunks_scanned = 0;
    const int max_scan = 500;

    while (reader_.has_prev() && chunks_scanned < max_scan) {
        auto new_lines = reader_.read_backward();
        if (new_lines.empty()) break;

        if (globalLineBase_ >= new_lines.size()) {
            globalLineBase_ -= static_cast<uint64_t>(new_lines.size());
        } else {
            globalLineBase_ = 0;
        }

        lines_ = std::move(new_lines);
        cursorLine_ = lines_.size() > 0 ? lines_.size() - 1 : 0;
        scrollX_ = 0;
        ++chunks_scanned;

        performSearch();
        if (!search_.matchLines.empty()) {
            search_.currentMatch = static_cast<ssize_t>(search_.matchLines.size()) - 1;
            cursorLine_ = search_.matchLines[static_cast<size_t>(search_.currentMatch)];
            setStatus("Found in chunk L" + std::to_string(globalLineBase_ + 1));
            return;
        }
    }

    setStatus("Scanned " + std::to_string(chunks_scanned) + " chunks — no match");
}

// ============================================================================
// Navigation
// ============================================================================

void LogViewer::moveCursor(int delta) {
    if (lines_.empty()) return;

    if (delta > 0) {
        if (cursorLine_ + static_cast<size_t>(delta) >= lines_.size()) {
            if (reader_.has_next()) {
                loadNextChunk();
                cursorLine_ = 0;
            } else {
                cursorLine_ = lines_.size() - 1;
            }
        } else {
            cursorLine_ += static_cast<size_t>(delta);
        }
    } else {
        if (cursorLine_ < static_cast<size_t>(-delta)) {
            if (reader_.has_prev()) {
                loadPrevChunk();
                if (!lines_.empty()) {
                    cursorLine_ = lines_.size() - 1;
                }
            } else {
                cursorLine_ = 0;
            }
        } else {
            cursorLine_ = static_cast<size_t>(
                static_cast<ssize_t>(cursorLine_) + delta);
        }
    }
    scrollX_ = 0;
}

void LogViewer::setCursorLine(size_t line) {
    if (line < lines_.size()) {
        cursorLine_ = line;
    }
}

void LogViewer::scrollHorizontal(int delta) {
    if (delta > 0) {
        scrollX_ += static_cast<size_t>(delta);
    } else {
        size_t abs_delta = static_cast<size_t>(-delta);
        if (scrollX_ > abs_delta) {
            scrollX_ -= abs_delta;
        } else {
            scrollX_ = 0;
        }
    }
}

void LogViewer::loadNextChunk() {
    if (!reader_.has_next()) {
        setStatus("Already at end of file");
        return;
    }

    size_t prev_count = lines_.size();
    auto new_lines = reader_.read_forward();

    globalLineBase_ += static_cast<uint64_t>(prev_count);

    lines_ = std::move(new_lines);
    cursorLine_ = 0;
    scrollX_ = 0;

    if (search_.active && !search_.pattern.empty()) {
        performSearch();
        if (!search_.matchLines.empty()) {
            search_.currentMatch = 0;
        }
    }

    setStatus("Chunk L" + std::to_string(globalLineBase_ + 1) +
              " (" + std::to_string(lines_.size()) + " lines)");
}

void LogViewer::loadPrevChunk() {
    if (!reader_.has_prev()) {
        setStatus("Already at beginning of file");
        return;
    }

    auto new_lines = reader_.read_backward();
    if (new_lines.empty()) {
        setStatus("Already at beginning of file");
        return;
    }

    if (globalLineBase_ >= new_lines.size()) {
        globalLineBase_ -= static_cast<uint64_t>(new_lines.size());
    } else {
        globalLineBase_ = 0;
    }

    lines_ = std::move(new_lines);
    cursorLine_ = lines_.size() > 0 ? lines_.size() - 1 : 0;
    scrollX_ = 0;

    if (search_.active && !search_.pattern.empty()) {
        performSearch();
        if (!search_.matchLines.empty()) {
            search_.currentMatch = 0;
        }
    }

    setStatus("Chunk L" + std::to_string(globalLineBase_ + 1) +
              " (" + std::to_string(lines_.size()) + " lines)");
}

void LogViewer::goToTop() {
    cursorLine_ = 0;
    scrollX_ = 0;
}

void LogViewer::goToBottom() {
    if (reader_.file_size() == 0) return;

    // Read the last ~64 KB to get the final lines.
    uint64_t end_offset   = reader_.file_size();
    uint64_t read_size    = 65536;
    uint64_t start_offset = (end_offset > read_size) ? end_offset - read_size : 0;

    lines_       = reader_.read_at(start_offset);
    cursorLine_  = lines_.size() > 0 ? lines_.size() - 1 : 0;
    scrollX_     = 0;

    // Estimate global line number from the visible sample.
    // This is inherently approximate — exact line numbers require an index.
    if (!lines_.empty() && reader_.file_size() > 0) {
        uint64_t total   = 0;
        for (const auto& l : lines_) total += l.size() + 1;
        uint64_t avg_len = total / lines_.size();
        if (avg_len < 1) avg_len = 1;
        globalLineBase_ = start_offset / avg_len;
    }

    setStatus("End of file (~L" + std::to_string(globalLineBase_ + 1) + ")");
}

void LogViewer::jumpToLine(uint64_t targetLine) {
    if (reader_.file_size() == 0 || targetLine < 1) {
        setStatus("Invalid target");
        return;
    }

    // Estimate byte offset using the average line length from the current
    // chunk (or a reasonable default).  This is approximate for files with
    // variable-length lines; exact jumping requires a line index.
    uint64_t avg_len = 80;
    if (!lines_.empty()) {
        uint64_t total = 0;
        for (const auto& l : lines_) total += l.size() + 1;
        avg_len = total / lines_.size();
    }
    if (avg_len < 1) avg_len = 1;

    uint64_t target_0based = targetLine - 1;
    uint64_t est_total = reader_.file_size() / avg_len;
    double frac = (est_total > 0)
        ? static_cast<double>(target_0based) / static_cast<double>(est_total)
        : 0.0;
    if (frac > 1.0) frac = 1.0;

    uint64_t offset = static_cast<uint64_t>(frac * static_cast<double>(reader_.file_size()));

    lines_ = reader_.read_at(offset);
    if (lines_.empty()) {
        setStatus("Jump failed — empty region");
        return;
    }

    cursorLine_     = 0;
    scrollX_        = 0;
    globalLineBase_ = target_0based;

    setStatus("Jumped to ~L" + std::to_string(targetLine));
}

void LogViewer::scrollToLineStart() {
    scrollX_ = 0;
}

void LogViewer::scrollToLineEnd() {
    if (cursorLine_ < lines_.size()) {
        size_t len = lines_[cursorLine_].size();
        // We don't know the viewport width here — frontend should clamp.
        if (len > 0) scrollX_ = len - 1;
    }
}

void LogViewer::reloadChunk() {
    lines_ = reader_.read_at(reader_.chunk_start());
    cursorLine_ = std::min(cursorLine_, lines_.size() > 0 ? lines_.size() - 1 : 0);
    setStatus("Reloaded");
}

// ============================================================================
// Follow mode
// ============================================================================

void LogViewer::toggleFollow() {
    followMode_ = !followMode_;
    if (followMode_) {
        if (!lines_.empty()) {
            cursorLine_ = lines_.size() - 1;
        }
        setStatus("Follow mode ON");
    } else {
        setStatus("Follow mode OFF");
    }
}

// ============================================================================
// Search interface
// ============================================================================

void LogViewer::beginSearch(bool fullSearch) {
    search_.clear();
    search_.active     = true;
    search_.fullSearch = fullSearch;
    setStatus(fullSearch ? "Full search (all chunks)" : "Search (current viewport)");
}

void LogViewer::cancelSearch() {
    search_.clear();
    setStatus("Search cancelled");
}

void LogViewer::appendSearchChar(char ch) {
    if (ch < 32 || ch >= 127) return;
    search_.pattern += ch;
    performSearch();
    if (!search_.matchLines.empty()) {
        search_.currentMatch = 0;
    }
}

void LogViewer::popSearchChar() {
    if (!search_.pattern.empty()) {
        search_.pattern.pop_back();
        performSearch();
        if (!search_.matchLines.empty()) {
            search_.currentMatch = 0;
        }
    }
}

void LogViewer::confirmSearch() {
    performSearch();
    if (search_.fullSearch && search_.matchLines.empty()) {
        searchForwardChunks();
    }
    if (!search_.matchLines.empty()) {
        search_.currentMatch = 0;
        cursorLine_ = search_.matchLines[0];
    }
    setStatus("");
}

void LogViewer::setSearchPattern(const std::string& pattern) {
    search_.pattern = pattern;
    performSearch();
    if (!search_.matchLines.empty()) {
        search_.currentMatch = 0;
        cursorLine_ = search_.matchLines[0];
    }
}

void LogViewer::navigateSearch(bool forward) {
    if (!search_.active || search_.pattern.empty()) return;

    if (!search_.matchLines.empty()) {
        if (forward) {
            ++search_.currentMatch;
            if (search_.currentMatch >= static_cast<ssize_t>(search_.matchLines.size())) {
                if (search_.fullSearch) {
                    searchForwardChunks();
                    return;
                }
                search_.currentMatch = 0; // wrap in viewport mode
            }
        } else {
            --search_.currentMatch;
            if (search_.currentMatch < 0) {
                if (search_.fullSearch) {
                    searchBackwardChunks();
                    return;
                }
                search_.currentMatch = static_cast<ssize_t>(search_.matchLines.size()) - 1;
            }
        }

        if (search_.currentMatch >= 0 &&
            static_cast<size_t>(search_.currentMatch) < search_.matchLines.size()) {
            cursorLine_ = search_.matchLines[static_cast<size_t>(search_.currentMatch)];
            scrollX_ = 0;
        }
    } else if (search_.fullSearch) {
        if (forward) {
            searchForwardChunks();
        } else {
            searchBackwardChunks();
        }
    }
}

// ============================================================================
// Command mode (line jump)
// ============================================================================

void LogViewer::beginCommand() {
    command_.clear();
    command_.active = true;
    setStatus("Jump to line (Enter to confirm, Esc to cancel)");
}

void LogViewer::cancelCommand() {
    command_.clear();
    setStatus("Cancelled");
}

void LogViewer::appendCommandChar(char ch) {
    if (ch >= '0' && ch <= '9') {
        command_.buffer += ch;
    }
}

void LogViewer::popCommandChar() {
    if (!command_.buffer.empty()) {
        command_.buffer.pop_back();
    }
}

void LogViewer::confirmCommand() {
    if (command_.buffer.empty()) {
        command_.clear();
        return;
    }

    command_.active = false;

    // Safe conversion with exception handling
    uint64_t target;
    try {
        size_t pos = 0;
        target = std::stoull(command_.buffer, &pos);
        if (pos != command_.buffer.size()) {
            setStatus("Invalid line number");
            command_.buffer.clear();
            return;
        }
    } catch (const std::exception&) {
        setStatus("Invalid line number");
        command_.buffer.clear();
        return;
    }

    command_.buffer.clear();

    if (target >= 1) {
        jumpToLine(target);
    } else {
        setStatus("Invalid line number");
    }
}

// ============================================================================
// Helpers
// ============================================================================

void LogViewer::setStatus(const std::string& msg, int ttl) {
    statusMsg_ = msg;
    statusTtl_ = ttl;
}

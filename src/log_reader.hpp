#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Configuration for the log reader's chunk strategy.
struct ReadOptions {
    size_t window_lines = 200;   ///< Target lines per chunk
    size_t chunk_size   = 65536; ///< Read chunk size in bytes
};

/// Streaming file reader that reads the file in configurable chunks.
/// Never loads the entire file into memory.
class LogReader {
public:
    LogReader() = default;
    ~LogReader();

    // Non-copyable, movable
    LogReader(const LogReader&) = delete;
    LogReader& operator=(const LogReader&) = delete;
    LogReader(LogReader&&) noexcept;
    LogReader& operator=(LogReader&&) noexcept;

    /// Open a file for reading. Returns true on success.
    bool open(const std::string& path, const ReadOptions& opts = ReadOptions());

    /// Close the current file.
    void close();

    /// Returns true if a file is currently open.
    bool is_open() const { return fd_ >= 0; }

    /// Read the initial chunk from the beginning of the file.
    std::vector<std::string> read_initial();

    /// Read the next chunk forward.
    std::vector<std::string> read_forward();

    /// Read the previous chunk backward.
    std::vector<std::string> read_backward();

    /// Reload the current chunk (for follow mode).
    std::vector<std::string> reload();

    /// Seek to a byte offset and read from there.
    std::vector<std::string> read_at(uint64_t offset);

    uint64_t file_size()   const { return static_cast<uint64_t>(file_size_); }
    uint64_t chunk_start() const { return static_cast<uint64_t>(chunk_start_); }
    uint64_t chunk_end()   const { return static_cast<uint64_t>(chunk_end_); }

    bool has_prev() const { return chunk_start_ > 0; }
    bool has_next() const { return chunk_end_ < file_size_; }

    const std::string& path() const { return path_; }
    const std::vector<size_t>& line_offsets() const { return line_offsets_; }

private:
    std::string read_raw(uint64_t offset, size_t size, size_t& bytes_read);
    std::vector<std::string> split_lines(const std::string& data,
                                         bool is_continuation);

    int fd_ = -1;
    std::string path_;
    uint64_t file_size_   = 0;
    uint64_t chunk_start_ = 0;
    uint64_t chunk_end_   = 0;
    ReadOptions opts_;
    std::string partial_line_;
    std::vector<size_t> line_offsets_;
};

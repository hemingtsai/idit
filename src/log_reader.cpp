#include "log_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

LogReader::~LogReader() {
    close();
}

LogReader::LogReader(LogReader&& other) noexcept
    : fd_(other.fd_)
    , path_(std::move(other.path_))
    , file_size_(other.file_size_)
    , chunk_start_(other.chunk_start_)
    , chunk_end_(other.chunk_end_)
    , opts_(other.opts_)
    , partial_line_(std::move(other.partial_line_))
    , line_offsets_(std::move(other.line_offsets_))
{
    other.fd_ = -1;
}

LogReader& LogReader::operator=(LogReader&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        file_size_ = other.file_size_;
        chunk_start_ = other.chunk_start_;
        chunk_end_ = other.chunk_end_;
        opts_ = other.opts_;
        partial_line_ = std::move(other.partial_line_);
        line_offsets_ = std::move(other.line_offsets_);
        other.fd_ = -1;
    }
    return *this;
}

bool LogReader::open(const std::string& path, const ReadOptions& opts) {
    close();
    opts_ = opts;
    path_ = path;

    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) {
        return false;
    }

    struct stat st;
    if (::fstat(fd_, &st) < 0) {
        close();
        return false;
    }
    file_size_ = static_cast<uint64_t>(st.st_size);

    return true;
}

void LogReader::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    path_.clear();
    file_size_ = 0;
    chunk_start_ = 0;
    chunk_end_ = 0;
    partial_line_.clear();
    line_offsets_.clear();
}

std::string LogReader::read_raw(uint64_t offset, size_t size, size_t& bytes_read) {
    if (offset >= file_size_) {
        bytes_read = 0;
        return {};
    }

    size_t remaining = static_cast<size_t>(file_size_ - offset);
    size_t to_read = std::min(size, remaining);

    std::string buf(to_read, '\0');
    ssize_t n = ::pread(fd_, buf.data(), to_read, static_cast<off_t>(offset));
    if (n < 0) {
        bytes_read = 0;
        return {};
    }
    buf.resize(static_cast<size_t>(n));
    bytes_read = static_cast<size_t>(n);
    return buf;
}

std::vector<std::string> LogReader::split_lines(const std::string& data,
                                                 bool is_continuation) {
    std::vector<std::string> lines;
    line_offsets_.clear();

    std::string current;
    size_t line_start = 0;

    if (is_continuation && !partial_line_.empty()) {
        current = std::move(partial_line_);
        partial_line_.clear();
    }

    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '\n') {
            current += data.substr(line_start, i - line_start);
            lines.push_back(std::move(current));
            current.clear();
            line_offsets_.push_back(i + 1);
            line_start = i + 1;
        }
    }

    if (line_start < data.size()) {
        current += data.substr(line_start);
    }

    if (!current.empty()) {
        partial_line_ = std::move(current);
    }

    return lines;
}

/// Flush partial_line_ as a final line if we've read to EOF.
static void flush_partial_at_eof(std::vector<std::string>& lines,
                                  std::string& partial_line,
                                  uint64_t chunk_end, uint64_t file_size) {
    if (!partial_line.empty() && chunk_end >= file_size) {
        lines.push_back(std::move(partial_line));
        partial_line.clear();
    }
}

std::vector<std::string> LogReader::read_initial() {
    chunk_start_ = 0;
    partial_line_.clear();
    line_offsets_.clear();

    size_t bytes_read = 0;
    std::string data = read_raw(0, opts_.chunk_size, bytes_read);
    chunk_end_ = chunk_start_ + bytes_read;

    auto lines = split_lines(data, false);
    flush_partial_at_eof(lines, partial_line_, chunk_end_, file_size_);
    return lines;
}

std::vector<std::string> LogReader::read_forward() {
    if (chunk_end_ >= file_size_) {
        // Flush remaining partial line at EOF
        if (!partial_line_.empty()) {
            std::vector<std::string> result;
            result.push_back(std::move(partial_line_));
            partial_line_.clear();
            return result;
        }
        return {};
    }

    uint64_t start_offset = chunk_end_;
    bool has_partial = !partial_line_.empty();

    size_t bytes_read = 0;
    std::string data = read_raw(start_offset, opts_.chunk_size, bytes_read);

    chunk_start_ = start_offset;
    chunk_end_ = start_offset + bytes_read;

    auto lines = split_lines(data, has_partial);
    flush_partial_at_eof(lines, partial_line_, chunk_end_, file_size_);
    return lines;
}

std::vector<std::string> LogReader::read_backward() {
    if (chunk_start_ == 0) {
        return {};
    }

    uint64_t read_size = std::min(static_cast<uint64_t>(opts_.chunk_size), chunk_start_);
    uint64_t start_offset = chunk_start_ - read_size;

    size_t bytes_read = 0;
    std::string data = read_raw(start_offset, read_size, bytes_read);

    size_t skip_to = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '\n') {
            skip_to = i + 1;
            break;
        }
    }

    chunk_start_ = start_offset;
    chunk_end_ = start_offset + bytes_read;
    partial_line_.clear();

    std::string valid_data = data.substr(skip_to);
    return split_lines(valid_data, false);
}

std::vector<std::string> LogReader::reload() {
    struct stat st;
    if (::fstat(fd_, &st) < 0) {
        return {};
    }

    uint64_t new_size = static_cast<uint64_t>(st.st_size);
    if (new_size <= file_size_) {
        file_size_ = new_size;
        return {};
    }

    size_t bytes_read = 0;
    std::string data = read_raw(file_size_,
                                static_cast<size_t>(new_size - file_size_),
                                bytes_read);

    file_size_ = new_size;

    bool at_end = (chunk_end_ >= file_size_ - bytes_read);
    if (at_end) {
        chunk_end_ = new_size;
        auto lines = split_lines(data, !partial_line_.empty());
        flush_partial_at_eof(lines, partial_line_, chunk_end_, file_size_);
        return lines;
    }

    return {};
}

std::vector<std::string> LogReader::read_at(uint64_t offset) {
    struct stat st;
    if (::fstat(fd_, &st) < 0) {
        return {};
    }
    file_size_ = static_cast<uint64_t>(st.st_size);

    if (offset >= file_size_) {
        offset = file_size_ > 0 ? file_size_ - 1 : 0;
    }

    uint64_t search_start = (offset > 512) ? offset - 512 : 0;
    size_t search_bytes = 0;
    std::string search_data = read_raw(search_start,
                                       static_cast<size_t>(offset - search_start + 512),
                                       search_bytes);

    size_t line_start_in_search = 0;
    size_t rel_offset = static_cast<size_t>(offset - search_start);
    for (size_t i = 0; i < rel_offset && i < search_data.size(); ++i) {
        if (search_data[i] == '\n') {
            line_start_in_search = i + 1;
        }
    }

    chunk_start_ = search_start + line_start_in_search;
    partial_line_.clear();

    size_t bytes_read = 0;
    std::string data = read_raw(chunk_start_, opts_.chunk_size, bytes_read);
    chunk_end_ = chunk_start_ + bytes_read;

    auto lines = split_lines(data, false);
    flush_partial_at_eof(lines, partial_line_, chunk_end_, file_size_);
    return lines;
}

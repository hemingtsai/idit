#include "log_reader.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>

int main() {
    // Test 1: Open and read initial chunk
    {
        LogReader reader;
        ReadOptions opts;
        opts.chunk_size = 256;
        opts.window_lines = 10;

        if (!reader.open("/tmp/test_log.txt", opts)) {
            std::cerr << "FAIL: Could not open test file\n";
            return 1;
        }
        std::cout << "File size: " << reader.file_size() << " bytes\n";
        assert(reader.file_size() > 0);

        auto lines = reader.read_initial();
        std::cout << "Initial chunk: " << lines.size() << " lines (chunk: "
                  << reader.chunk_start() << "-" << reader.chunk_end() << ")\n";
        assert(!lines.empty());
        assert(lines[0].find("Starting application") != std::string::npos);

        std::cout << "PASS: read_initial\n";
    }

    // Test 2: Read forward through all chunks
    {
        LogReader reader;
        ReadOptions opts;
        opts.chunk_size = 200;
        reader.open("/tmp/test_log.txt", opts);

        auto lines = reader.read_initial();
        size_t total_lines = lines.size();
        int chunks = 1;

        while (reader.has_next()) {
            lines = reader.read_forward();
            total_lines += lines.size();
            chunks++;
            if (chunks > 100) break; // safety limit
        }

        std::cout << "Read " << total_lines << " lines in " << chunks
                  << " chunks\n";
        assert(total_lines >= 30);
        std::cout << "PASS: read_forward streaming\n";
    }

    // Test 3: Read backward
    {
        LogReader reader;
        ReadOptions opts;
        opts.chunk_size = 200;
        reader.open("/tmp/test_log.txt", opts);

        reader.read_initial();
        if (reader.has_next()) reader.read_forward();
        if (reader.has_next()) reader.read_forward();

        auto fwd_lines = reader.read_forward();
        std::cout << "Forward chunk: " << fwd_lines.size() << " lines\n";

        auto back_lines = reader.read_backward();
        std::cout << "Backward chunk: " << back_lines.size() << " lines\n";

        if (reader.has_prev()) {
            back_lines = reader.read_backward();
            std::cout << "Second backward chunk: " << back_lines.size() << " lines\n";
        }

        std::cout << "PASS: read_backward\n";
    }

    // Test 4: read_at
    {
        LogReader reader;
        ReadOptions opts;
        opts.chunk_size = 200;
        reader.open("/tmp/test_log.txt", opts);

        auto lines = reader.read_at(500);
        std::cout << "read_at(500): " << lines.size() << " lines\n";
        assert(!lines.empty());

        lines = reader.read_at(0);
        std::cout << "read_at(0): " << lines.size() << " lines, first: "
                  << (lines.empty() ? "" : lines[0].substr(0, 50)) << "\n";
        std::cout << "PASS: read_at\n";
    }

    // Test 5: reload
    {
        LogReader reader;
        ReadOptions opts;
        opts.chunk_size = 200;
        reader.open("/tmp/test_log.txt", opts);

        reader.read_initial();
        auto new_lines = reader.reload();
        std::cout << "Reload (no new data): " << new_lines.size() << " lines\n";
        assert(new_lines.empty());
        std::cout << "PASS: reload\n";
    }

    // Test 6: Empty file
    {
        system("touch /tmp/idit_empty_test.txt");
        LogReader reader;
        reader.open("/tmp/idit_empty_test.txt");
        auto lines = reader.read_initial();
        assert(lines.empty());
        assert(!reader.has_next());
        assert(!reader.has_prev());
        reader.close();
        system("rm -f /tmp/idit_empty_test.txt");
        std::cout << "PASS: empty file\n";
    }

    // Test 7: File with no trailing newline (EOF partial line)
    {
        // Create file without trailing newline
        FILE* f = fopen("/tmp/idit_noeol_test.txt", "w");
        fprintf(f, "line1\nline2\nline3_without_newline");
        fclose(f);

        LogReader reader;
        ReadOptions opts;
        opts.chunk_size = 256;
        reader.open("/tmp/idit_noeol_test.txt", opts);

        auto lines = reader.read_initial();
        std::cout << "No-EOL file: " << lines.size() << " lines\n";
        for (size_t i = 0; i < lines.size(); ++i) {
            std::cout << "  [" << i << "]: '" << lines[i] << "'\n";
        }
        assert(lines.size() == 3);
        assert(lines[2] == "line3_without_newline");

        system("rm -f /tmp/idit_noeol_test.txt");
        std::cout << "PASS: no trailing newline\n";
    }

    std::cout << "\nAll tests passed!\n";
    return 0;
}

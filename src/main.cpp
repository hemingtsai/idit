#include "log_reader.hpp"
#include "theme.hpp"
#include "tui.hpp"

#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <string>

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] <file>\n"
              << "\n"
              << "A streaming log reader that reads files in configurable chunks.\n"
              << "\n"
              << "Options:\n"
              << "  -l, --window-lines N   Target lines per chunk (default: 200)\n"
              << "  -s, --chunk-size N     Read chunk size in bytes (default: 65536)\n"
              << "  -f, --follow           Follow mode (like tail -f)\n"
              << "  -t, --theme FILE       Theme file path\n"
              << "  -h, --help             Show this help\n"
              << "\n"
              << "Keybindings:\n"
              << "  q                       Quit\n"
              << "  j / k / Up / Down       Move cursor up/down\n"
              << "  h / l / Left / Right    Scroll horizontally\n"
              << "  g / G                   Go to top/bottom of chunk\n"
              << "  PgUp / PgDn / Ctrl+B/F  Load previous/next chunk\n"
              << "  0 / $                   Scroll to line start/end\n"
              << "  f                       Toggle follow mode\n"
              << "  /                       Search (Enter to confirm, Esc to cancel)\n"
              << "  n / N                   Next/previous search match\n"
              << "  r                       Reload current chunk\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    ReadOptions opts;
    bool follow_mode = false;
    std::string theme_path;

    // clang-format off
    static struct option long_opts[] = {
        {"window-lines", required_argument, nullptr, 'l'},
        {"chunk-size",   required_argument, nullptr, 's'},
        {"follow",       no_argument,       nullptr, 'f'},
        {"theme",        required_argument, nullptr, 't'},
        {"help",         no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    // clang-format on

    int opt;
    while ((opt = getopt_long(argc, argv, "l:s:ft:h", long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'l':
            opts.window_lines = static_cast<size_t>(std::stoul(optarg));
            break;
        case 's':
            opts.chunk_size = static_cast<size_t>(std::stoul(optarg));
            break;
        case 'f':
            follow_mode = true;
            break;
        case 't':
            theme_path = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc) {
        std::cerr << "Error: No file specified.\n\n";
        print_usage(argv[0]);
        return 1;
    }

    std::string filepath = argv[optind];

    // Load theme
    Theme theme;
    if (!theme_path.empty()) {
        if (!load_theme(theme_path, theme)) {
            std::cerr << "Warning: Could not load theme '" << theme_path
                      << "', using defaults.\n";
        }
    }

    // Initialize and run TUI
    TUI tui;
    if (!tui.init(theme)) {
        std::cerr << "Error: Failed to initialize ncurses.\n";
        return 1;
    }

    tui.run(filepath, opts, follow_mode);

    return 0;
}

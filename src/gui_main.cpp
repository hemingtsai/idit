#include "config.hpp"
#include "log_reader.hpp"
#include "log_viewer.hpp"
#include "gui.hpp"

#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <string>

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] <file>\n"
              << "\n"
              << "GUI log reader (ImGui frontend).\n"
              << "\n"
              << "Options:\n"
              << "  -l, --window-lines N   Target lines per chunk (default: from config)\n"
              << "  -s, --chunk-size N     Read chunk size in bytes (default: from config)\n"
              << "  -f, --follow           Follow mode (like tail -f)\n"
              << "  -h, --help             Show this help\n"
              << "\n"
              << "Keybindings (same as TUI):\n"
              << "  q                       Quit\n"
              << "  j / k / Up / Down       Move cursor up/down\n"
              << "  h / l / Left / Right    Scroll horizontally\n"
              << "  g / Shift+G             Go to top/bottom\n"
              << "  PgUp / PgDn             Load previous/next chunk\n"
              << "  f                       Toggle follow mode\n"
              << "  /                       Search (Enter to confirm, Esc to cancel)\n"
              << "  \\                       Full-file search\n"
              << "  n / Shift+N             Next/previous search match\n"
              << "  :                       Jump to line number\n"
              << "  r                       Reload current chunk\n"
              << "  S                       Open settings dialog\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    ReadOptions opts;
    bool follow_mode = false;

    // clang-format off
    static struct option long_opts[] = {
        {"window-lines", required_argument, nullptr, 'l'},
        {"chunk-size",   required_argument, nullptr, 's'},
        {"follow",       no_argument,       nullptr, 'f'},
        {"help",         no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };
    // clang-format on

    int opt;
    while ((opt = getopt_long(argc, argv, "l:s:fh", long_opts, nullptr)) != -1) {
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

    // Load config (creates default if missing)
    Config config;
    config.load();

    // Apply config defaults when CLI flags not set
    if (opts.window_lines == 200)  opts.window_lines = config.settings.windowLines;
    if (opts.chunk_size   == 65536) opts.chunk_size   = config.settings.chunkSize;

    // Create LogViewer core and GUI frontend
    LogViewer viewer;
    GUI gui;

    if (!gui.init(config)) {
        std::cerr << "Error: Failed to initialize GUI.\n";
        return 1;
    }

    gui.run(viewer, filepath, opts, config, follow_mode);
    gui.shutdown();

    return 0;
}

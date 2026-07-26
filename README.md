# idit — Interactive Streaming Log Reader

A terminal-based log file reader written in C++ with ncurses, designed for
viewing large log files without loading the entire file into memory.

## Features

- **Streaming reads** – Only a configurable chunk of the file is loaded at a time
- **No line wrapping** – Long lines extend beyond the screen; use horizontal scroll
- **Truncation indicator** – Reverse-video `>` marks lines that continue off-screen
- **Continuous line numbers** – Global line numbering across chunk boundaries
- **Line jump** – `:` + number to jump to any line (via byte-offset estimation)
- **Go to end** – `G` jumps to the last chunk of the file
- **Search** – `/` to search within the current chunk, `n`/`N` to navigate matches
- **Follow mode** – `-f` flag for `tail -f`-like behavior
- **Customizable themes** – Color schemes via simple key-value theme files

## Building

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Requires:
- C++17 compiler
- ncurses development libraries

## Usage

```
idit [OPTIONS] <file>
```

### Options

| Flag | Long | Description |
|------|------|-------------|
| `-l N` | `--window-lines N` | Target lines per chunk (default: 200) |
| `-s N` | `--chunk-size N` | Read chunk size in bytes (default: 65536) |
| `-f` | `--follow` | Follow mode (like `tail -f`) |
| `-t FILE` | `--theme FILE` | Theme file path |
| `-h` | `--help` | Show help |

### Keybindings

| Key | Action |
|-----|--------|
| `q` | Quit |
| `j` / `k` / `↑` / `↓` | Move cursor up/down |
| `h` / `l` / `←` / `→` | Scroll horizontally |
| `g` / `G` | Go to top of chunk / jump to end of file |
| `:` | Jump to line number (type number + Enter) |
| `PgUp` / `PgDn` | Load previous/next chunk |
| `0` / `$` | Scroll to line start/end |
| `f` | Toggle follow mode |
| `/` | Search (Enter to confirm, Esc to cancel) |
| `n` / `N` | Next/previous search match |
| `r` | Reload current chunk |

## Themes

Theme files use a simple `key = value` format. See `themes/` directory for examples.

```ini
[colors]
background = black
foreground = white
status_bar_bg = blue
status_bar_fg = white
line_number_fg = green
highlight_bg = yellow
highlight_fg = black
search_match_bg = red
search_match_fg = white
truncation_bg = red
truncation_fg = white
search_bar_bg = cyan
search_bar_fg = black
```

Available color names: `black`, `red`, `green`, `yellow`, `blue`, `magenta`,
`cyan`, `white`, and their `bright_` variants.

## Architecture

```
src/
├── main.cpp          Entry point, CLI argument parsing
├── log_reader.hpp    Streaming file reader API
├── log_reader.cpp    Chunk-based file I/O with partial-line handling
├── tui.hpp           ncurses TUI interface
├── tui.cpp           Rendering, input handling, search
├── theme.hpp         Theme data structures and color mapping
└── theme.cpp         Theme file parser, ncurses color pair init
```

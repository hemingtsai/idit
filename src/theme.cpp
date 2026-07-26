#include "theme.hpp"

#include <algorithm>
#include <cctype>
#include <curses.h>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

// Color name -> ncurses COLOR_* mapping
static const std::map<std::string, int> COLOR_MAP = {
    {"black",        COLOR_BLACK},
    {"red",          COLOR_RED},
    {"green",        COLOR_GREEN},
    {"yellow",       COLOR_YELLOW},
    {"blue",         COLOR_BLUE},
    {"magenta",      COLOR_MAGENTA},
    {"cyan",         COLOR_CYAN},
    {"white",        COLOR_WHITE},
    {"bright_black", 8},
    {"bright_red",   9},
    {"bright_green", 10},
    {"bright_yellow",11},
    {"bright_blue",  12},
    {"bright_magenta",13},
    {"bright_cyan",  14},
    {"bright_white", 15},
    {"default",      -1},
};

int parse_color(const std::string& name) {
    auto it = COLOR_MAP.find(name);
    if (it != COLOR_MAP.end()) {
        return it->second;
    }
    // Try case-insensitive
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    for (const auto& [key, val] : COLOR_MAP) {
        if (key == lower) {
            return val;
        }
    }
    return -1;
}

bool load_theme(const std::string& path, Theme& theme) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r");
        line = line.substr(start, end - start + 1);

        // Skip comments and section headers
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;

        // Parse key = value
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim key and value
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);

        if (key == "background")      theme.background      = val;
        else if (key == "foreground") theme.foreground      = val;
        else if (key == "status_bar_bg")   theme.status_bar_bg   = val;
        else if (key == "status_bar_fg")   theme.status_bar_fg   = val;
        else if (key == "line_number_fg")  theme.line_number_fg  = val;
        else if (key == "highlight_bg")    theme.highlight_bg    = val;
        else if (key == "highlight_fg")    theme.highlight_fg    = val;
        else if (key == "search_match_bg") theme.search_match_bg = val;
        else if (key == "search_match_fg") theme.search_match_fg = val;
        else if (key == "truncation_bg")   theme.truncation_bg   = val;
        else if (key == "truncation_fg")   theme.truncation_fg   = val;
        else if (key == "search_bar_bg")   theme.search_bar_bg   = val;
        else if (key == "search_bar_fg")   theme.search_bar_fg   = val;
    }

    return true;
}

void apply_theme(const Theme& theme) {
    int bg  = parse_color(theme.background);
    int fg  = parse_color(theme.foreground);
    int sbb = parse_color(theme.status_bar_bg);
    int sbf = parse_color(theme.status_bar_fg);
    int lnf = parse_color(theme.line_number_fg);
    int hb  = parse_color(theme.highlight_bg);
    int hf  = parse_color(theme.highlight_fg);
    int smb = parse_color(theme.search_match_bg);
    int smf = parse_color(theme.search_match_fg);
    int tb  = parse_color(theme.truncation_bg);
    int tf  = parse_color(theme.truncation_fg);
    int srb = parse_color(theme.search_bar_bg);
    int srf = parse_color(theme.search_bar_fg);

    if (bg  < 0) bg  = COLOR_BLACK;
    if (fg  < 0) fg  = COLOR_WHITE;
    if (sbb < 0) sbb = COLOR_BLUE;
    if (sbf < 0) sbf = COLOR_WHITE;
    if (lnf < 0) lnf = COLOR_GREEN;
    if (hb  < 0) hb  = COLOR_YELLOW;
    if (hf  < 0) hf  = COLOR_BLACK;
    if (smb < 0) smb = COLOR_RED;
    if (smf < 0) smf = COLOR_WHITE;
    if (tb  < 0) tb  = COLOR_RED;
    if (tf  < 0) tf  = COLOR_WHITE;
    if (srb < 0) srb = COLOR_CYAN;
    if (srf < 0) srf = COLOR_BLACK;

    // Use default background if requested
    if (bg == -1) {
        use_default_colors();
        bg = -1; // COLOR_DEFAULT via use_default_colors
    }

    init_pair(CP_DEFAULT,      fg,  bg);
    init_pair(CP_STATUS_BAR,   sbf, sbb);
    init_pair(CP_LINE_NUMBER,  lnf, bg);
    init_pair(CP_HIGHLIGHT,    hf,  hb);
    init_pair(CP_SEARCH_MATCH, smf, smb);
    init_pair(CP_TRUNCATION,   tf,  tb);
    init_pair(CP_SEARCH_BAR,   srf, srb);
    init_pair(CP_SEARCH_BAR_ERROR, COLOR_WHITE, COLOR_RED);
}

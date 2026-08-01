#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// ============================================================================
// Paths
// ============================================================================

static std::string homeDir() {
    const char* h = getenv("HOME");
    return h ? std::string(h) : ".";
}

std::string Config::dir() {
    return homeDir() + "/.config/idit";
}

std::string Config::path() {
    return dir() + "/config.lua";
}

std::string Config::themeDir() {
    return dir() + "/themes";
}

std::string Config::themePath(const std::string& name) {
    return themeDir() + "/" + name + ".lua";
}

// ============================================================================
// Helpers
// ============================================================================

static void ensureDir(const std::string& p) {
    mkdir(p.c_str(), 0755);
}

static void writeFile(const std::string& p, const std::string& content) {
    std::ofstream of(p);
    if (of.is_open()) {
        of << content;
    }
}

/// Write the default config.lua if it doesn't exist.
static void createDefaultConfig() {
    ensureDir(Config::dir());
    if (std::ifstream(Config::path()).good()) return; // already exists

    const char* defaultConfig = R"lua(
-- idit configuration
-- Edit this file to change settings, then restart idit.
return {
    -- UI font (used for toolbars, menus, dialogs)
    ui_font = "default",
    ui_font_size = 14,

    -- Content font (used for log lines) — "monospace" recommended
    content_font = "monospace",
    content_font_size = 13,

    -- Theme name (loaded from ~/.config/idit/themes/<name>.lua)
    theme = "default",

    -- Default chunk read settings
    window_lines = 200,
    chunk_size = 65536,
}
)lua";
    writeFile(Config::path(), defaultConfig);
}

/// Write the default theme if it doesn't exist.
static void createDefaultTheme() {
    ensureDir(Config::themeDir());
    std::string tp = Config::themePath("default");
    if (std::ifstream(tp).good()) return;

    const char* defaultTheme = R"lua(
-- idit Default Theme
return {
    background       = "black",
    foreground       = "white",
    status_bar_bg    = "blue",
    status_bar_fg    = "white",
    line_number_fg   = "green",
    highlight_bg     = "yellow",
    highlight_fg     = "black",
    search_match_bg  = "red",
    search_match_fg  = "white",
    truncation_bg    = "red",
    truncation_fg    = "white",
    search_bar_bg    = "cyan",
    search_bar_fg    = "black",
}
)lua";
    writeFile(tp, defaultTheme);

    // Also create dark theme
    std::string dp = Config::themePath("dark");
    if (std::ifstream(dp).good()) return;
    const char* darkTheme = R"lua(
-- idit Dark Theme
return {
    background       = "black",
    foreground       = "bright_white",
    status_bar_bg    = "bright_black",
    status_bar_fg    = "bright_white",
    line_number_fg   = "bright_cyan",
    highlight_bg     = "bright_yellow",
    highlight_fg     = "black",
    search_match_bg  = "bright_red",
    search_match_fg  = "black",
    truncation_bg    = "bright_magenta",
    truncation_fg    = "black",
    search_bar_bg    = "bright_green",
    search_bar_fg    = "black",
}
)lua";
    writeFile(dp, darkTheme);
}

// ============================================================================
// Load / Save
// ============================================================================

bool Config::load() {
    // Ensure config files exist
    createDefaultConfig();
    createDefaultTheme();

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::math);

    auto result = lua.safe_script_file(path(), sol::script_pass_on_error);
    if (!result.valid()) {
        // Failed to parse — use defaults
        setDefaults();
        return false;
    }

    sol::table t = result;
    if (!t.valid()) {
        setDefaults();
        return false;
    }

    settings.uiFont.family       = t.get_or("ui_font",         std::string("default"));
    settings.uiFont.size         = t.get_or("ui_font_size",    14.0f);
    settings.contentFont.family  = t.get_or("content_font",    std::string("monospace"));
    settings.contentFont.size    = t.get_or("content_font_size", 13.0f);
    settings.theme               = t.get_or("theme",           std::string("default"));
    settings.windowLines         = t.get_or("window_lines",    200ULL);
    settings.chunkSize           = t.get_or("chunk_size",      65536ULL);

    return true;
}

bool Config::save() {
    ensureDir(dir());

    std::ostringstream oss;
    oss << "-- idit configuration\n";
    oss << "return {\n";
    oss << "    ui_font = \""           << settings.uiFont.family     << "\",\n";
    oss << "    ui_font_size = "         << settings.uiFont.size       << ",\n";
    oss << "    content_font = \""       << settings.contentFont.family << "\",\n";
    oss << "    content_font_size = "    << settings.contentFont.size   << ",\n";
    oss << "    theme = \""              << settings.theme             << "\",\n";
    oss << "    window_lines = "         << settings.windowLines       << ",\n";
    oss << "    chunk_size = "           << settings.chunkSize         << ",\n";
    oss << "}\n";

    writeFile(path(), oss.str());
    return true;
}

void Config::setDefaults() {
    settings = UISettings{};
}

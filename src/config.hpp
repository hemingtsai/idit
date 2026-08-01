#pragma once

#include <cstddef>
#include <string>

/// Font configuration for UI and content areas.
struct FontConfig {
    std::string family = "default"; ///< "default", "monospace", or path to .ttf
    float       size   = 14.0f;
};

/// All user-configurable settings loaded from ~/.config/idit/config.lua
struct UISettings {
    FontConfig  uiFont;
    FontConfig  contentFont;
    std::string theme       = "default";
    size_t      windowLines = 200;
    size_t      chunkSize   = 65536;
};

/// Loads and saves Lua-based settings from ~/.config/idit/
class Config {
public:
    /// Config directory: ~/.config/idit
    static std::string dir();

    /// Main config file: ~/.config/idit/config.lua
    static std::string path();

    /// Theme directory: ~/.config/idit/themes/
    static std::string themeDir();

    /// Path to a named theme: ~/.config/idit/themes/<name>.lua
    static std::string themePath(const std::string& name);

    /// Load settings from config.lua.  Creates default config if missing.
    /// Returns true on success.
    bool load();

    /// Save current settings to config.lua.
    bool save();

    /// Reset all settings to hard-coded defaults.
    void setDefaults();

    UISettings settings;
};

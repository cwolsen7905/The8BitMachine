#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Thin wrapper around a platform-native open-file dialog.
// On macOS this calls NSOpenPanel; other platforms can provide their own
// implementation later.
// ---------------------------------------------------------------------------
class FileDialog {
public:
    // Show an open-file dialog.
    // `filters` — extensions without the dot, e.g. {"prg","bin"}; empty = all files.
    // Returns the chosen absolute path, or "" if cancelled.
    static std::string openFile(const char*                     title,
                                const std::vector<std::string>& filters = {});

    // Show a save-file dialog.
    // `defaultName` — suggested filename (no path).
    // Returns the chosen absolute path, or "" if cancelled.
    static std::string saveFile(const char*                     title,
                                const std::string&              defaultName = "",
                                const std::vector<std::string>& filters = {});
};

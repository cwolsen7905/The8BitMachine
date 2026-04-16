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
    // `title`   — window title shown to the user
    // `filters` — list of allowed extensions without the dot, e.g. {"prg","bin","rom"}
    //             Empty list = allow all files.
    // Returns the chosen absolute path, or "" if the user cancelled.
    static std::string openFile(const char*                      title,
                                const std::vector<std::string>&  filters = {});
};

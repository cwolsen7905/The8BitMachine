#include "FileDialog.h"
#include <nfd.h>

static std::string buildSpec(const std::vector<std::string>& filters) {
    std::string spec;
    for (size_t i = 0; i < filters.size(); ++i) {
        if (i > 0) spec += ',';
        spec += filters[i];
    }
    return spec;
}

std::string FileDialog::openFile(const char* /*title*/,
                                 const std::vector<std::string>& filters)
{
    NFD_Init();
    nfdchar_t* outPath = nullptr;
    nfdresult_t result;

    if (filters.empty()) {
        result = NFD_OpenDialogU8(&outPath, nullptr, 0, nullptr);
    } else {
        std::string spec = buildSpec(filters);
        nfdfilteritem_t item = { "Supported files", spec.c_str() };
        result = NFD_OpenDialogU8(&outPath, &item, 1, nullptr);
    }

    std::string path;
    if (result == NFD_OKAY && outPath) {
        path = outPath;
        NFD_FreePathU8(outPath);
    }
    NFD_Quit();
    return path;
}

std::string FileDialog::saveFile(const char* /*title*/,
                                 const std::string& defaultName,
                                 const std::vector<std::string>& filters)
{
    NFD_Init();
    nfdchar_t* outPath = nullptr;
    nfdresult_t result;
    const nfdchar_t* name = defaultName.empty() ? nullptr : defaultName.c_str();

    if (filters.empty()) {
        result = NFD_SaveDialogU8(&outPath, nullptr, 0, nullptr, name);
    } else {
        std::string spec = buildSpec(filters);
        nfdfilteritem_t item = { "Supported files", spec.c_str() };
        result = NFD_SaveDialogU8(&outPath, &item, 1, nullptr, name);
    }

    std::string path;
    if (result == NFD_OKAY && outPath) {
        path = outPath;
        NFD_FreePathU8(outPath);
    }
    NFD_Quit();
    return path;
}

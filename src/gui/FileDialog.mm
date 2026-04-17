// Objective-C++ translation unit — compiled only on macOS.
#import  <AppKit/AppKit.h>
#include "FileDialog.h"

std::string FileDialog::openFile(const char*                     title,
                                 const std::vector<std::string>& filters)
{
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title                  = [NSString stringWithUTF8String:title];
        panel.allowsMultipleSelection = NO;
        panel.canChooseDirectories   = NO;
        panel.canChooseFiles         = YES;

        if (!filters.empty()) {
            NSMutableArray<NSString*>* types = [NSMutableArray array];
            for (const auto& ext : filters)
                [types addObject:[NSString stringWithUTF8String:ext.c_str()]];

            // allowedFileTypes is deprecated in macOS 12 but remains functional.
            // A UTType-based replacement can be added when minimum deployment
            // target is raised to 12.0.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            panel.allowedFileTypes = types;
#pragma clang diagnostic pop
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = panel.URLs.firstObject;
            if (url) return std::string(url.path.UTF8String);
        }
    }
    return {};
}

std::string FileDialog::saveFile(const char*                     title,
                                 const std::string&              defaultName,
                                 const std::vector<std::string>& filters)
{
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.title = [NSString stringWithUTF8String:title];

        if (!defaultName.empty())
            panel.nameFieldStringValue =
                [NSString stringWithUTF8String:defaultName.c_str()];

        if (!filters.empty()) {
            NSMutableArray<NSString*>* types = [NSMutableArray array];
            for (const auto& ext : filters)
                [types addObject:[NSString stringWithUTF8String:ext.c_str()]];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            panel.allowedFileTypes = types;
#pragma clang diagnostic pop
        }

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = panel.URL;
            if (url) return std::string(url.path.UTF8String);
        }
    }
    return {};
}

#include "gui/Application.h"
#include <cstdio>

int main(int /*argc*/, char* /*argv*/[]) {
    Application app;
    if (!app.init()) {
        std::fprintf(stderr, "Failed to initialize application.\n");
        return 1;
    }
    app.run();
    return 0;
}

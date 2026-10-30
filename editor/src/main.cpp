#include "EditorApp.h"

#include <cstdlib>
#include <cstring>

// Usage: MagnumEditor [--project <folder>] [--control-port <0|port>] [--exec "cmd; cmd"] [--shot <png> [--shot-target window|viewport] [--shot-frames N]]
int main(int argc, char** argv) {
    magnum::editor::EditorOptions options;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) {
            options.shotPath = argv[++i];
        } else if (!std::strcmp(argv[i], "--shot-target") && i + 1 < argc) {
            options.shotTarget = argv[++i];
        } else if (!std::strcmp(argv[i], "--shot-frames") && i + 1 < argc) {
            options.shotFrames = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--project") && i + 1 < argc) {
            options.project = argv[++i];
        } else if (!std::strcmp(argv[i], "--control-port") && i + 1 < argc) {
            options.controlPort = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--exec") && i + 1 < argc) {
            options.exec = argv[++i];
        }
    }

    magnum::editor::EditorApp app;
    if (!app.Init(options)) {
        app.Shutdown();
        return 1;
    }
    app.Run();
    app.Shutdown();
    return 0;
}

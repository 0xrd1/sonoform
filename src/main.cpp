#include "App.h"
#include <string>

int main(int argc, char** argv) {
    std::string audioPath;
    if (argc > 1) audioPath = argv[1];

    App app;
    if (!app.Init(audioPath)) {
        return 1;
    }
    app.Run();
    app.Shutdown();
    return 0;
}

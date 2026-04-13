#include "App.h"
#include <iostream>

int main() {
    try {
        App app(1280, 720, "Skeletal Animation Viewer");
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        return 1;
    }
    return 0;
}

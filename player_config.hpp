#pragma once
#include <string>

struct PlayerConfig {
    std::string videoPath;
    int width = 0;
    int height = 0;
    bool autoLoop = false;
    bool blockMode = false;
    enum ColorMode {
        MONO,
        COLOR_8BIT,
        COLOR_24BIT
    } colorMode = MONO;
    
    double speedMultiplier = 1.0;
    size_t bufferSize = 16;
    
    // Command line parsing
    static PlayerConfig fromCommandLine(int argc, char* argv[]);
    // Interactive configuration
    static PlayerConfig fromInteractive();
};

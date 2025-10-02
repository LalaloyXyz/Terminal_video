#include "player_config.hpp"
#include <iostream>

PlayerConfig PlayerConfig::fromCommandLine(int argc, char* argv[]) {
    PlayerConfig config;
    config.videoPath = argv[1];
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--color" || arg == "-c") {
            config.colorMode = COLOR_8BIT;
        } else if (arg == "--truecolor" || arg == "-t") {
            config.colorMode = COLOR_24BIT;
        } else if (arg == "--width" || arg == "-w") {
            if (i + 1 < argc) config.width = std::atoi(argv[++i]);
        } else if (arg == "--height" || arg == "-h") {
            if (i + 1 < argc) config.height = std::atoi(argv[++i]);
        } else if (arg == "--loop" || arg == "-l") {
            config.autoLoop = true;
        } else if (arg == "--block" || arg == "-b") {
            config.blockMode = true;
        }
    }
    return config;
}

PlayerConfig PlayerConfig::fromInteractive() {
    PlayerConfig config;
    std::cout << "ASCII Video Player with Color Support\n"
              << "====================================\n"
              << "1. Play video file\n"
              << "2. Play from camera\n"
              << "Choice (1/2): ";
    
    int choice;
    std::cin >> choice;
    std::cin.ignore();
    
    if (choice == 1) {
        std::cout << "Enter video file path: ";
        std::getline(std::cin, config.videoPath);
        
        std::cout << "Enable auto-loop? (y/n): ";
        char loopChoice;
        std::cin >> loopChoice;
        config.autoLoop = (loopChoice == 'y' || loopChoice == 'Y');
        
        std::cout << "Color mode:\n"
                  << "1. Monochrome\n"
                  << "2. 8-bit color (256 colors)\n"
                  << "3. 24-bit color (true color)\n"
                  << "Choice (1/2/3): ";
        
        int colorChoice;
        std::cin >> colorChoice;
        switch (colorChoice) {
            case 2: config.colorMode = COLOR_8BIT; break;
            case 3: config.colorMode = COLOR_24BIT; break;
            default: config.colorMode = MONO;
        }
    }
    return config;
}

#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <poll.h>
#include <unordered_map>
#include <sstream>

class ASCIIVideoPlayer {
public:
    // Enhanced ASCII character set for better detail
    const std::string ASCII_CHARS = " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
    
    // ANSI color codes for 8-bit color mode
    enum ColorMode {
        MONO,        // Black and white
        COLOR_8BIT,  // 256-color mode
        COLOR_24BIT  // True color (RGB)
    };
    
    ColorMode current_color_mode = MONO;
    struct termios original_termios;
    bool terminal_modified = false;
    
    // Color caching structures
    struct ColorKey {
        uint8_t r, g, b;
        bool background;
        
        bool operator==(const ColorKey& other) const {
            return r == other.r && g == other.g && b == other.b && background == other.background;
        }
    };
    
    struct ColorKeyHasher {
        std::size_t operator()(const ColorKey& k) const {
            return ((std::size_t)k.r << 24) | ((std::size_t)k.g << 16) | ((std::size_t)k.b << 8) | (k.background ? 1 : 0);
        }
    };
    
    // Color caches for different modes
    std::unordered_map<ColorKey, std::string, ColorKeyHasher> color_cache_8bit;
    std::unordered_map<ColorKey, std::string, ColorKeyHasher> color_cache_24bit;
    std::unordered_map<uint32_t, int> rgb_to_8bit_cache; // RGB packed as uint32_t -> 8-bit color
    
    // Cache statistics
    struct CacheStats {
        size_t hits = 0;
        size_t misses = 0;
        size_t cache_size = 0;
        
        double hit_rate() const {
            return (hits + misses > 0) ? (double)hits / (hits + misses) * 100.0 : 0.0;
        }
    } cache_stats;
    
    // Performance: Pre-computed brightness lookup table
    std::vector<int> brightness_to_char_idx;
    
    struct TerminalSize {
        int width;
        int height;
    };
    
    struct TerminalGuard {
        struct termios& original;
        bool& modified;
        
        TerminalGuard(struct termios& orig, bool& mod) : original(orig), modified(mod) {
            if (!modified) {
                tcgetattr(STDIN_FILENO, &original);
                struct termios new_termios = original;
                new_termios.c_lflag &= ~(ICANON | ECHO);
                new_termios.c_cc[VMIN] = 0;
                new_termios.c_cc[VTIME] = 0;
                tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
                
                // Hide cursor and enable alternative screen
                std::cout << "\033[?25l\033[?1049h" << std::flush;
                modified = true;
            }
        }
        
        ~TerminalGuard() {
            if (modified) {
                // Show cursor and disable alternative screen
                std::cout << "\033[?25h\033[?1049l" << std::flush;
                tcsetattr(STDIN_FILENO, TCSANOW, &original);
                modified = false;
            }
        }
    };
    
    TerminalSize getTerminalSize() {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        return {w.ws_col, w.ws_row};
    }
    
    bool kbhit() {
        struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };
        return poll(&pfd, 1, 0) > 0;
    }
    
    // Signal handler for clean exit
    static ASCIIVideoPlayer* instance;
    static void signalHandler(int signal) {
        std::cout << "\033[?25h\033[?1049l\033[0m\n";
        std::exit(signal);
    }
    
    // Pack RGB into uint32_t for caching
    uint32_t packRGB(int r, int g, int b) {
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    
    // Cached RGB to 8-bit color conversion
    int rgbTo8BitColorCached(int r, int g, int b) {
        uint32_t rgb_key = packRGB(r, g, b);
        
        auto it = rgb_to_8bit_cache.find(rgb_key);
        if (it != rgb_to_8bit_cache.end()) {
            cache_stats.hits++;
            return it->second;
        }
        
        cache_stats.misses++;
        
        int color;
        if (r == g && g == b) {
            // Grayscale
            if (r < 8) color = 16;
            else if (r > 248) color = 231;
            else color = static_cast<int>(((r - 8) / 247.0) * 24) + 232;
        } else {
            // Color cube
            int ir = static_cast<int>((r / 255.0) * 5);
            int ig = static_cast<int>((g / 255.0) * 5);
            int ib = static_cast<int>((b / 255.0) * 5);
            color = 16 + (36 * ir) + (6 * ig) + ib;
        }
        
        rgb_to_8bit_cache[rgb_key] = color;
        cache_stats.cache_size = rgb_to_8bit_cache.size() + color_cache_8bit.size() + color_cache_24bit.size();
        return color;
    }
    
    // Cached color code generation
    std::string getColorCodeCached(int r, int g, int b, bool background = false) {
        if (current_color_mode == MONO) return "";
        
        ColorKey key = {(uint8_t)r, (uint8_t)g, (uint8_t)b, background};
        
        if (current_color_mode == COLOR_8BIT) {
            auto it = color_cache_8bit.find(key);
            if (it != color_cache_8bit.end()) {
                cache_stats.hits++;
                return it->second;
            }
            
            cache_stats.misses++;
            int color = rgbTo8BitColorCached(r, g, b);
            std::string code = "\033[" + std::to_string(background ? 48 : 38) + ";5;" + std::to_string(color) + "m";
            color_cache_8bit[key] = code;
            cache_stats.cache_size = rgb_to_8bit_cache.size() + color_cache_8bit.size() + color_cache_24bit.size();
            return code;
        } else if (current_color_mode == COLOR_24BIT) {
            auto it = color_cache_24bit.find(key);
            if (it != color_cache_24bit.end()) {
                cache_stats.hits++;
                return it->second;
            }
            
            cache_stats.misses++;
            std::string code = "\033[" + std::to_string(background ? 48 : 38) + ";2;" + 
                              std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
            color_cache_24bit[key] = code;
            cache_stats.cache_size = rgb_to_8bit_cache.size() + color_cache_8bit.size() + color_cache_24bit.size();
            return code;
        }
        
        return "";
    }
    
    std::string resetColor() {
        return (current_color_mode != MONO) ? "\033[0m" : "";
    }
    
    // Initialize brightness lookup table for performance
    void initializeBrightnessLookup() {
        brightness_to_char_idx.resize(256);
        for (int i = 0; i < 256; ++i) {
            brightness_to_char_idx[i] = (i * (ASCII_CHARS.size() - 1)) / 255;
        }
    }

public:
    ASCIIVideoPlayer() {
        instance = this;
        initializeBrightnessLookup();
        
        // Set up signal handlers for clean exit
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        signal(SIGQUIT, signalHandler);
    }
    
    void setColorMode(ColorMode mode) {
        if (current_color_mode != mode) {
            current_color_mode = mode;
            // Clear caches when switching modes to save memory
            if (mode != COLOR_8BIT) {
                color_cache_8bit.clear();
                rgb_to_8bit_cache.clear();
            }
            if (mode != COLOR_24BIT) {
                color_cache_24bit.clear();
            }
            cache_stats.cache_size = rgb_to_8bit_cache.size() + color_cache_8bit.size() + color_cache_24bit.size();
        }
    }
    
    ColorMode getColorMode() const {
        return current_color_mode;
    }
    
    // Clear all caches (useful for memory management)
    void clearColorCaches() {
        color_cache_8bit.clear();
        color_cache_24bit.clear();
        rgb_to_8bit_cache.clear();
        cache_stats = CacheStats(); // Reset stats
    }
    
    // Get cache statistics
    CacheStats getCacheStats() const {
        return cache_stats;
    }
    
    std::string frameToAscii(const cv::Mat& frame, int target_width = 0, int target_height = 0) {
        if (frame.empty()) return "";
        
        // Auto-detect terminal size if not specified
        if (target_width == 0 || target_height == 0) {
            TerminalSize term = getTerminalSize();
            target_width = std::min(term.width - 2, 120);
            target_height = std::min(term.height - 3, 40);
        }
        
        // Calculate proper aspect ratio (characters are taller than wide)
        float char_aspect_ratio = 2.2f; // Typical character height/width ratio
        float frame_aspect = static_cast<float>(frame.cols) / frame.rows;
        
        int new_width, new_height;
        if (frame_aspect > (target_width * char_aspect_ratio) / target_height) {
            new_width = target_width;
            new_height = static_cast<int>(target_width / frame_aspect / char_aspect_ratio);
        } else {
            new_height = target_height;
            new_width = static_cast<int>(target_height * frame_aspect * char_aspect_ratio);
        }
        
        // Resize frame
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(new_width, new_height), 0, 0, cv::INTER_AREA);
        
        // Convert to ASCII with cached colors
        std::string ascii_frame;
        ascii_frame.reserve(new_width * new_height * 20 + new_height); // Extra space for color codes
        
        if (current_color_mode == MONO) {
            // Monochrome mode - convert to grayscale
            cv::Mat gray;
            cv::cvtColor(resized, gray, cv::COLOR_BGR2GRAY);
            cv::equalizeHist(gray, gray); // Enhance contrast
            
            for (int y = 0; y < gray.rows; ++y) {
                const uint8_t* row = gray.ptr<uint8_t>(y);
                for (int x = 0; x < gray.cols; ++x) {
                    ascii_frame += ASCII_CHARS[brightness_to_char_idx[row[x]]];
                }
                ascii_frame += '\n';
            }
        } else {
            // Color mode with caching
            std::string lastColorCode;
            cv::Vec3b lastPixel = {255, 255, 255}; // Invalid initial color
            
            for (int y = 0; y < resized.rows; ++y) {
                const cv::Vec3b* row = resized.ptr<cv::Vec3b>(y);
                for (int x = 0; x < resized.cols; ++x) {
                    cv::Vec3b pixel = row[x];
                    
                    // Only generate color code if pixel color changed
                    if (pixel != lastPixel) {
                        std::string colorCode = getColorCodeCached(pixel[2], pixel[1], pixel[0], false); // BGR to RGB
                        if (colorCode != lastColorCode) {
                            ascii_frame += colorCode;
                            lastColorCode = colorCode;
                        }
                        lastPixel = pixel;
                    }
                    
                    // Use pre-computed brightness lookup
                    int brightness = static_cast<int>(0.299 * pixel[2] + 0.587 * pixel[1] + 0.114 * pixel[0]);
                    ascii_frame += ASCII_CHARS[brightness_to_char_idx[brightness]];
                }
                ascii_frame += resetColor() + '\n';
                lastColorCode.clear(); // Reset color at end of line
            }
        }
        
        return ascii_frame;
    }
    
    // Alternative color method using background colors (block style) with caching
    std::string frameToColorBlocks(const cv::Mat& frame, int target_width = 0, int target_height = 0) {
        if (frame.empty() || current_color_mode == MONO) return frameToAscii(frame, target_width, target_height);
        
        // Auto-detect terminal size if not specified
        if (target_width == 0 || target_height == 0) {
            TerminalSize term = getTerminalSize();
            target_width = std::min(term.width - 2, 120);
            target_height = std::min(term.height - 3, 40);
        }
        
        // Calculate proper aspect ratio
        float char_aspect_ratio = 2.2f;
        float frame_aspect = static_cast<float>(frame.cols) / frame.rows;
        
        int new_width, new_height;
        if (frame_aspect > (target_width * char_aspect_ratio) / target_height) {
            new_width = target_width;
            new_height = static_cast<int>(target_width / frame_aspect / char_aspect_ratio);
        } else {
            new_height = target_height;
            new_width = static_cast<int>(target_height * frame_aspect * char_aspect_ratio);
        }
        
        // Resize frame
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(new_width, new_height), 0, 0, cv::INTER_AREA);
        
        std::string ascii_frame;
        ascii_frame.reserve(new_width * new_height * 20 + new_height);
        
        std::string lastColorCode;
        cv::Vec3b lastPixel = {255, 255, 255}; // Invalid initial color
        
        for (int y = 0; y < resized.rows; ++y) {
            const cv::Vec3b* row = resized.ptr<cv::Vec3b>(y);
            for (int x = 0; x < resized.cols; ++x) {
                cv::Vec3b pixel = row[x];
                
                // Only generate color code if pixel color changed
                if (pixel != lastPixel) {
                    std::string colorCode = getColorCodeCached(pixel[2], pixel[1], pixel[0], true); // BGR to RGB, background
                    if (colorCode != lastColorCode) {
                        ascii_frame += colorCode;
                        lastColorCode = colorCode;
                    }
                    lastPixel = pixel;
                }
                
                ascii_frame += ' ';
            }
            ascii_frame += resetColor() + '\n';
            lastColorCode.clear(); // Reset color at end of line
        }
        
        return ascii_frame;
    }
    
    void displayVideoInfo(const cv::VideoCapture& cap) {
        double fps = cap.get(cv::CAP_PROP_FPS);
        int frame_count = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
        double duration = frame_count / fps;
        int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
        int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
        
        std::cout << "Terminal Video Player\n";
        std::cout << "============================================\n";
        std::cout << "Video Info:\n";
        std::cout << "Resolution: " << width << "x" << height << "\n";
        std::cout << "FPS: " << fps << "\n";
        std::cout << "Duration: " << static_cast<int>(duration/60) << ":" 
                  << std::setfill('0') << std::setw(2) << static_cast<int>(duration) % 60 << "\n";
        std::cout << "Frame Count: " << frame_count << "\n";
        std::cout << "Color Mode: " << (current_color_mode == MONO ? "Monochrome" : 
                                       current_color_mode == COLOR_8BIT ? "8-bit Color (Cached)" : "24-bit Color (Cached)") << "\n",
        std::cout << "Press any key to start...\n";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cin.get();
    }
    
    bool playVideoAscii(const std::string& videoPath, int width = 0, int height = 0) {
        cv::VideoCapture cap(videoPath);
        if (!cap.isOpened()) {
            std::cerr << "Error: Failed to open video file: " << videoPath << std::endl;
            return false;
        }
        displayVideoInfo(cap);
        TerminalGuard guard(original_termios, terminal_modified);
        double fps = cap.get(cv::CAP_PROP_FPS);
        double base_delay = (fps > 0.0) ? (1000.0 / fps) : 33.33;
        double speed_multiplier = 1.0;
        cv::Mat frame;
        bool paused = false, block_mode = false, fullscreen_mode = false;
        int frame_number = 0, total_frames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
        int original_width = width, original_height = height;
        auto last_time = std::chrono::steady_clock::now();
        
        while (true) {
            if (kbhit()) {
                char key;
                while (read(STDIN_FILENO, &key, 1) > 0) {
                    switch (key) {
                        case 'q': case 'Q': case 27: goto cleanup;
                        case ' ': paused = !paused; break;
                        case '+': case '=': speed_multiplier = std::min(speed_multiplier * 1.5, 5.0); break;
                        case '-': case '_': speed_multiplier = std::max(speed_multiplier / 1.5, 0.2); break;
                        case 'c': case 'C': 
                            setColorMode(static_cast<ColorMode>((current_color_mode + 1) % 3)); 
                            break;
                        case 'b': case 'B': block_mode = !block_mode; break;
                        case 'f': case 'F':
                            fullscreen_mode = !fullscreen_mode;
                            if (fullscreen_mode) {
                                TerminalSize term = getTerminalSize();
                                width = term.width - 2;
                                height = term.height - 4;
                            } else {
                                width = original_width;
                                height = original_height;
                            }
                        case 'r': case 'R':
                            clearColorCaches();
                            break;
                    }
                }
            }
            
            if (!paused) {
                if (!cap.read(frame) || frame.empty()) break;
                frame_number++;
            }
            
            std::cout << "\033[2J\033[H";
            if (!frame.empty()) {
                std::string ascii = (block_mode && current_color_mode != MONO) ? 
                                   frameToColorBlocks(frame, width, height) : frameToAscii(frame, width, height);
                std::cout << ascii;
                
                int progress = (total_frames > 0) ? (frame_number * 100 / total_frames) : 0;
                std::string color_mode_str = (current_color_mode == MONO ? "MONO" : 
                                            current_color_mode == COLOR_8BIT ? "8BIT" : "24BIT");
                std::cout << resetColor() << "\n[" << (paused ? "PAUSED" : "PLAYING") << "] "
                          << "Frame: " << frame_number << "/" << total_frames
                          << " (" << progress << "%) "
                          << "Speed: " << std::fixed << std::setprecision(1) << speed_multiplier << "x "
                          << "Mode: " << color_mode_str << (block_mode ? "-BLOCK" : "")
                          << (fullscreen_mode ? " FULLSCREEN" : "")
                          << " | Cache: " << std::fixed << std::setprecision(1) << cache_stats.hit_rate() << "%"
                          << "\n[Q]Quit [SPACE]Pause [+/-]Speed [C]Color [B]Block [F]Fullscreen [R]ClearCache";
            }
            std::cout << std::flush;
            
            double current_delay = base_delay / speed_multiplier;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
            if (elapsed < current_delay) std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(current_delay - elapsed)));
            last_time = std::chrono::steady_clock::now();
        }
        
    cleanup:
        std::cout << resetColor() << "\n\nPlayback finished!\n";
        return true;
    }
    
    bool playFromCamera(int camera_id = 0, int width = 80, int height = 24) {
        cv::VideoCapture cap(camera_id);
        if (!cap.isOpened()) {
            std::cerr << "Error: Failed to open camera " << camera_id << std::endl;
            return false;
        }
        std::cout << "Camera feed started. Controls: [Q]uit [C]olor [B]lock [F]ullscreen [S]tats [R]eset cache\n";
        TerminalGuard guard(original_termios, terminal_modified);
        cv::Mat frame;
        bool block_mode = false, fullscreen_mode = false;
        int original_width = width, original_height = height;
        auto last_time = std::chrono::steady_clock::now();
        
        while (true) {
            if (kbhit()) {
                char key;
                while (read(STDIN_FILENO, &key, 1) > 0) {
                    if (key == 'q' || key == 'Q') goto camera_cleanup;
                    else if (key == 'c' || key == 'C') setColorMode(static_cast<ColorMode>((current_color_mode + 1) % 3));
                    else if (key == 'b' || key == 'B') block_mode = !block_mode;
                    else if (key == 's' || key == 'S') {
                        std::cout << "\033[2J\033[H";
                        std::cout << "Press any key to continue...";
                        std::cin.get();
                    }
                    else if (key == 'r' || key == 'R') clearColorCaches();
                    else if (key == 'f' || key == 'F') {
                        fullscreen_mode = !fullscreen_mode;
                        if (fullscreen_mode) {
                            TerminalSize term = getTerminalSize();
                            width = term.width - 2;
                            height = term.height - 4;
                        } else {
                            width = original_width;
                            height = original_height;
                        }
                    }
                }
            }
            
            if (!cap.read(frame) || frame.empty()) continue;
            
            std::cout << "\033[2J\033[H";
            std::string ascii = (block_mode && current_color_mode != MONO) ? 
                               frameToColorBlocks(frame, width, height) : frameToAscii(frame, width, height);
            std::cout << ascii;
            
            std::string color_mode_str = (current_color_mode == MONO ? "MONO" : 
                                        current_color_mode == COLOR_8BIT ? "8BIT" : "24BIT");
            std::cout << resetColor() << "Mode: " << color_mode_str << (block_mode ? "-BLOCK" : "")
                      << (fullscreen_mode ? " FULLSCREEN" : "")
                      << " | Cache: " << std::fixed << std::setprecision(1) << cache_stats.hit_rate() << "%"
                      << " | [Q]uit [C]olor [B]lock [F]ullscreen [S]tats [R]eset" << std::flush;
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
            if (elapsed < 33) std::this_thread::sleep_for(std::chrono::milliseconds(33 - elapsed));
            last_time = std::chrono::steady_clock::now();
        }
        
    camera_cleanup:
        std::cout << resetColor();
        return true;
    }
};

// Static member definition
ASCIIVideoPlayer* ASCIIVideoPlayer::instance = nullptr;

int main(int argc, char* argv[]) {
    ASCIIVideoPlayer player;
    
    if (argc > 1) {
        // Parse command line arguments
        std::string videoPath = argv[1];
        int width = 0, height = 0;
        ASCIIVideoPlayer::ColorMode color_mode = ASCIIVideoPlayer::MONO;
        
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "--color" || arg == "-c") {
                color_mode = ASCIIVideoPlayer::COLOR_8BIT;
            } else if (arg == "--truecolor" || arg == "-t") {
                color_mode = ASCIIVideoPlayer::COLOR_24BIT;
            } else if (arg == "--width" || arg == "-w") {
                if (i + 1 < argc) width = std::atoi(argv[++i]);
            } else if (arg == "--height" || arg == "-h") {
                if (i + 1 < argc) height = std::atoi(argv[++i]);
            }
        }
        
        player.setColorMode(color_mode);
        
        if (!player.playVideoAscii(videoPath, width, height)) {
            return -1;
        }
    } else {
        // Interactive mode
        std::cout << "ASCII Video Player with Color Support\n";
        std::cout << "====================================\n";
        std::cout << "1. Play video file\n";
        std::cout << "2. Play from camera\n";
        std::cout << "Choice (1/2): ";
        
        int choice;
        std::cin >> choice;
        std::cin.ignore(); // Clear the input buffer
        
        if (choice == 1) {
            std::cout << "Enter video file path: ";
            std::string videoPath;
            std::getline(std::cin, videoPath);
            
            std::cout << "Color mode:\n";
            std::cout << "1. Monochrome\n";
            std::cout << "2. 8-bit color (256 colors)\n";
            std::cout << "3. 24-bit color (true color)\n";
            std::cout << "Choice (1/2/3): ";
            
            int color_choice;
            std::cin >> color_choice;
            
            ASCIIVideoPlayer::ColorMode mode = ASCIIVideoPlayer::MONO;
            if (color_choice == 2) mode = ASCIIVideoPlayer::COLOR_8BIT;
            else if (color_choice == 3) mode = ASCIIVideoPlayer::COLOR_24BIT;
            
            player.setColorMode(mode);
            
            if (!player.playVideoAscii(videoPath)) {
                return -1;
            }
        } else if (choice == 2) {
            std::cout << "Color mode (1=Mono, 2=8bit, 3=24bit): ";
            int color_choice;
            std::cin >> color_choice;
            
            ASCIIVideoPlayer::ColorMode mode = ASCIIVideoPlayer::MONO;
            if (color_choice == 2) mode = ASCIIVideoPlayer::COLOR_8BIT;
            else if (color_choice == 3) mode = ASCIIVideoPlayer::COLOR_24BIT;
            
            player.setColorMode(mode);
            player.playFromCamera();
        } else {
            std::cout << "Invalid choice.\n";
            return -1;
        }
    }
    
    return 0;
}
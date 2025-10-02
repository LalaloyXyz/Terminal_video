cmake_minimum_required(VERSION 3.10)
project(terminal_video)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCV package
find_package(OpenCV REQUIRED)

# Add optimization flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native")

# Add threading support
find_package(Threads REQUIRED)

# Add executable
add_executable(terminal_video 
    main.cpp
    player_config.cpp
)

# Link libraries
target_link_libraries(terminal_video 
    ${OpenCV_LIBS}
    Threads::Threads
)

# Include directories
target_include_directories(terminal_video PRIVATE
    ${OpenCV_INCLUDE_DIRS}
)

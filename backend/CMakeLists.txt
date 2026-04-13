cmake_minimum_required(VERSION 3.10)
project(Server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

# Add traffic.cpp to the build
add_executable(server
    server.cpp
    traffic.cpp
    dijkstra.cpp
    bellman.cpp
    backtrack.cpp
)

# httplib + traffic fetcher both require pthreads on Linux
find_package(Threads REQUIRED)
if(WIN32)
    target_link_libraries(server Threads::Threads)
else()
    target_link_libraries(server Threads::Threads m)
endif()
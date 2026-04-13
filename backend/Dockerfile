FROM gcc:latest

WORKDIR /app

COPY . .

# Compile your C++ application
RUN g++ -std=c++17 -O2 -I. server.cpp dijkstra.cpp bellman.cpp backtrack.cpp traffic.cpp -o server -pthread

# Inform Render which port the app uses
EXPOSE 8080

CMD ["./server"]
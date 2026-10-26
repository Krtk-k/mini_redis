#include <cstdio>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <type_traits>
#include <unistd.h>
#include <sys/epoll.h>
#include <cerrno>
#include <vector>
#include <sstream>
#include <unordered_map>

#include "threadSafeCache.hpp"
#include "threadPool.hpp"

ThreadSafeCache memory(16);
ThreadPool pool(10);

void make_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags|O_NONBLOCK);
}

void send_reply(int fd, const std::string& res) {
    size_t sent = 0;
    while(sent < res.size()) {
        ssize_t n = send(fd, res.data() + sent, res.size() - sent, MSG_NOSIGNAL);
        if(n > 0) sent += n;
        else if(n == -1 && errno == EINTR) continue;
        else break;
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd == -1) {
        perror("socket: ");
        return 1;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in server_add;
    server_add.sin_family = AF_INET;
    server_add.sin_port = htons(8080);
    server_add.sin_addr.s_addr = INADDR_ANY;
    if(bind(server_fd, (sockaddr*)&server_add, sizeof(server_add)) == -1) {
        perror("bind: ");
        return 1;
    }
    make_non_blocking(server_fd);
    if(listen(server_fd, 1000) == -1) {
        perror("listen: ");
        return 1;
    }
    std::cout << "\nServer listning on port 8080\n";

    int epoll_fd = epoll_create1(0);
    struct epoll_event event;
    event.events = EPOLLIN|EPOLLET;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
    epoll_event active_events[1000];

    unordered_map<int, std::string> client_buffers;

    while(1) {
        int event_num = epoll_wait(epoll_fd, active_events, 1000, -1);
        for(int i = 0; i<event_num; i++) {
            int active_fd = active_events[i].data.fd;
            if(active_fd == server_fd) {
                while(1) {
                    struct sockaddr_in client_add;
                    socklen_t size = sizeof(client_add);
                    int client_fd = accept(server_fd, (sockaddr*)&client_add, &size);
                    if(client_fd == -1) {
                        if(errno == EINTR) continue;
                        break;
                    }
                    std::cout << "[debug] Client accepted\n";
                    make_non_blocking(client_fd);
                    struct epoll_event client_event;
                    client_event.events = EPOLLIN|EPOLLET;
                    client_event.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
                }
            }
            else {
                bool should_close = false;
                char temp_buf[4096];
                while(1) {
                    int bytes_read = read(active_fd, temp_buf, sizeof(temp_buf));
                    if(bytes_read > 0) {
                        client_buffers[active_fd].append(temp_buf, bytes_read);
                        continue;
                    }
                    else if(bytes_read == 0) {
                        should_close = true;
                        break;
                    }
                    else if(errno == EAGAIN || errno == EWOULDBLOCK) break;
                    else {
                        should_close = true;
                        break;
                    }
                }
                std::string& line = client_buffers[active_fd];
                size_t pos;
                while((pos = line.find("\r\n")) != std::string::npos) {
                    std::string cmmd = line.substr(0, pos);
                    std::stringstream ss(cmmd);
                    line.erase(0, pos+2);
                    string token;
                    std::vector<std::string> tokens;
                    while(ss >> token) tokens.push_back(token);
                    if(tokens.empty()) continue;
                    if(tokens.size() == 3 && tokens[0] == "SET") {
                        pool.add_task([tokens = std::move(tokens), active_fd = std::move(active_fd)] {
                            memory.set(tokens[1], tokens[2]);
                            send_reply(active_fd, "SUCCESS\r\n");
                        });
                    }
                    else if(tokens.size() == 2 && tokens[0] == "GET") {
                        pool.add_task([tokens = std::move(tokens), active_fd = std::move(active_fd)] {
                            std::cout << "\n[debug] task successfully added to thread\n";
                            std::string val = memory.get(tokens[1]);
                            if(val == "NULL") send_reply(active_fd, "nil\r\n");
                            else send_reply(active_fd, val + "\r\n");
                        });
                    }
                    else send_reply(active_fd, "ERROR unknown or malformed command\r\n");
                }
                if(should_close) {
                    client_buffers.erase(active_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, nullptr);
                    close(active_fd);                
                }
            }
        }
    }
}

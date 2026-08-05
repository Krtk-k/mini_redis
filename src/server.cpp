#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
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

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    //error
    struct sockaddr_in server_add;
    server_add.sin_family = AF_INET;
    server_add.sin_port = 8080;
    server_add.sin_addr.s_addr = INADDR_ANY;
    bind(server_fd, (sockaddr*)&server_add, sizeof(server_add));
    //error
    listen(server_fd, 10000);
    //error
    make_non_blocking(server_fd);

    int epoll_fd = epoll_create1(0);
    struct epoll_event event;
    event.events = EPOLLIN|EPOLLET;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
    epoll_event active_events[10];

    unordered_map<int, std::string> client_buffers;

    while(1) {
        int event_num = epoll_wait(epoll_fd, active_events, 10, -1);
        for(int i = 0; i<event_num; i++) {
            int active_fd = active_events[i].data.fd;
            if(active_fd == server_fd) {
                struct sockaddr_in client_add;
                socklen_t size = sizeof(client_add);
                int client_fd = accept(server_fd, (sockaddr*)&client_add, &size);
                if(client_fd == -1) continue;
                make_non_blocking(client_fd);
                struct epoll_event client_event;
                client_event.events = EPOLLIN|EPOLLET;
                client_event.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
            }
            else {
                std::vector<char> temp_buf(1024);
                int bytes_read = read(active_fd, temp_buf.data(), temp_buf.size());
                if(bytes_read>0) {
                    client_buffers[active_fd].append(temp_buf.data(), temp_buf.size());
                    size_t pos;
                    while((pos = client_buffers[active_fd].find("\r\n")) != std::string::npos) {
                        std::string command = client_buffers[active_fd].substr(0, pos);
                        client_buffers[active_fd].erase(0, pos+2);
                        std::stringstream ss(command);
                        std::vector<std::string> tokens;
                        std::string token;
                        while(ss >> token) {
                            if(token.empty()) tokens.push_back(token);
                        }
                        if(tokens[0] == "SET") {
                            pool.add_task([&tokens, &active_fd] {
                                memory.set(tokens[1], tokens[2]);
                                std::string res = "SUCCESS\r\n";
                                // add error handling
                                write(active_fd, res.c_str(), res.length());
                            });
                        }
                        else if(tokens[0] == "GET") {
                            pool.add_task([&tokens, &active_fd] {
                                std::string val = memory.get(tokens[1]);
                                std::string res;
                                if(val == "NULL") res = "nil\r\n";
                                else res = val +"\r\n";
                                write(active_fd, res.c_str(), res.length());
                            });
                        }
                    }
                }
                else if(bytes_read == 0) {
                    client_buffers.erase(active_fd);
                    close(active_fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, nullptr);
                }
                else continue; // Error in sending data try again
            }
        }
    }
}

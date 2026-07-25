#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <vector>
#include <sstring>

#include "threadSafeCache.hpp"
#include "threadPool.hpp"

void make_non_blocking(int fd) {
    int flags = fstat(fd, F_GETFL, 0);
    fstat(fd, F_SETFL, flags|O_NONBLOCKING);
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
    listen(server_fd, 1024);
    //error
    make_non_blocking(server_fd);

    int epoll_fd = epoll_create1(0);
    struct epoll_event event;
    event.events = EPOLLIN|EPOLLET;
    event.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);
    epoll_event active_events[10];

    while(1) {
        int event_num = epoll_wait(epoll_fd, active_events, 10);
        for(int i = 0; i<event_num; i++) {
            int active_fd = active_events[i].data.fd;
            if(active_fd == server_fd) {
                struct sockaddr_in client_add;
                socklen_t size = sizeof(client_add);
                int client_fd = accept(server_fd, (sockaddr*)&client_add, size);
                if(client_fd == -1) continue;
                struct epoll_event client_event;
                client_event.events = EPOLLIN|EPOLLET;
                client_event.data.fd = client_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event);
            }
            else {
                vector<char> buffer(1024);
                
            }
        }
    }
}
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <vector>
#include <sstream>
#include <fcntl.h>
#include <unordered_map>
#include <iostream>

void make_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags|O_NONBLOCK);
}

int const MAX_CLIENT = 10;

int main() {
    int epoll_fd = epoll_create1(0);

    struct sockaddr_in server_add;
    server_add.sin_family = AF_INET;
    server_add.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server_add.sin_addr);
    
    //int max_clients = 1000
    int client_count = 1000;
    for(int i = 0; i<MAX_CLIENT; i++) {
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(client_fd == -1) {
            client_count--;
            continue;
        }
        make_non_blocking(client_fd);
        connect(client_fd, (sockaddr*)&server_add, sizeof(server_add));
        struct epoll_event event;
        event.events = EPOLLIN|EPOLLOUT|EPOLLET;
        event.data.fd = client_fd;

        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
    }

    std::cout << "\n" << client_count << " clients created successfully\n";

    std::vector<epoll_event> active_events(MAX_CLIENT);
    int client_processed = 0;
    std::unordered_map<int, std::string> client_buffers;

    uint incorrect_req = 0;

    for(int client_no = 0; client_no<client_count; client_no++) {
        int num_events = epoll_wait(epoll_fd, active_events.data(), MAX_CLIENT, -1);

        for(int i = 0; i<num_events; i++) {
            int active_fd = active_events[i].data.fd;

            if(active_events[i].events & EPOLLOUT) {
                std::cout << "[debug] client ready to write" << std::endl;
                std::string buffer;
                if((client_no^1) == client_no+1) 
                    buffer = "SET mohan friend\r\nSET ram friend\r\nSET lion wild\r\n";
                else 
                    buffer = "GET mohan\r\nGET rabbit\r\n";

                write(active_fd, buffer.c_str(), buffer.length());

                struct epoll_event mod_client;
                mod_client.events = EPOLLIN|EPOLLET;
                mod_client.data.fd = active_fd;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, active_fd, &mod_client);
                std::cout << "[debug] modified epoll_ctl()" << std::endl;
            }
            if(active_events[i].events & EPOLLIN) {
                std::cout << "[debug] client ready to read" << std::endl;
                std::vector<char> temp_buf(1024);
                int bytes_read = read(active_fd, temp_buf.data(), temp_buf.size());
                if(bytes_read == 0) {
                    std::cout << "[debug] nothing to read" << std::endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, nullptr);
                    client_buffers.erase(active_fd);
                    close(active_fd);
                }
                else if(bytes_read>0) {
                    std::cout << "[debug] successful read" << std::endl;
                    client_buffers[active_fd].append(temp_buf.data(), temp_buf.size());
                    size_t pos;
                    if((pos = client_buffers[active_fd].find("\r\n")) != std::string::npos) {
                        std::string server_res = client_buffers[active_fd].substr(0, pos);
                        client_buffers[active_fd].erase(0, pos+2);
                        if(server_res == "nil") incorrect_req++;
                        client_processed++;

                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, nullptr);
                        close(active_fd);
                    }
                }
                else {
                    std::cout << "[debug] testing the hypothesis" << std::endl;
                    if(errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, active_fd, nullptr);
                    close(active_fd);
                }
            }
        }
    }
    // std::cout << "\n" << "OUT OF " << client_count << " " << client_processed << "WERE EXECUTED PROCESSED SUCCESSFULLY\n";
    // std::cout << "Number of incorrect requests = " << incorrect_req << "\n";

    close(epoll_fd);
    return 0;
}

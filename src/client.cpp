#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

void make_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

const int MAX_CLIENT = 10;
const int TIMEOUT_MS = 5000;

struct ClientState {
    bool is_set_client = false;
    std::string to_send;
    size_t sent = 0;
    int expected_replies = 0;
    int got_replies = 0;
    std::string in_buf;
};

bool reply_ok(const ClientState& st, const std::string& r) {
    int idx = st.got_replies;
    if(idx >= st.expected_replies) return false;
    if(st.is_set_client) return r == "SUCCESS";
    if(idx == 0) return r == "friend" || r == "nil";
    return r == "nil";
}

int main() {
    int epoll_fd = epoll_create1(0);
    if(epoll_fd == -1) { perror("epoll_create1"); return 1; }

    struct sockaddr_in server_add{};
    server_add.sin_family = AF_INET;
    server_add.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server_add.sin_addr);

    std::unordered_map<int, ClientState> clients;

    int client_count = 0;
    for(int i = 0; i < MAX_CLIENT; i++) {
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if(client_fd == -1) { perror("socket"); continue; }
        make_non_blocking(client_fd);

        if(connect(client_fd, (sockaddr*)&server_add, sizeof(server_add)) == -1 && errno != EINPROGRESS) {
            perror("connect");
            close(client_fd);
            continue;
        }

        ClientState st;
        if(i % 2 == 0) {
            st.is_set_client = true;
            st.to_send = "SET mohan friend\r\nSET ram friend\r\nSET lion wild\r\n";
            st.expected_replies = 3;
        } else {
            st.to_send = "GET mohan\r\nGET rabbit\r\n";
            st.expected_replies = 2;
        }

        struct epoll_event event{};
        event.events = EPOLLIN | EPOLLOUT | EPOLLET;
        event.data.fd = client_fd;
        if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
            perror("epoll_ctl add");
            close(client_fd);
            continue;
        }
        clients[client_fd] = std::move(st);
        client_count++;
    }

    std::cout << "\n" << client_count << " clients created successfully\n";

    std::vector<epoll_event> active_events(MAX_CLIENT);
    int finished = 0;
    int client_processed = 0;
    int wrong_replies = 0;
    unsigned int nil_replies = 0;

    while(finished < client_count) {
        int num_events = epoll_wait(epoll_fd, active_events.data(), MAX_CLIENT, TIMEOUT_MS);
        if(num_events == -1) {
            if(errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        if(num_events == 0) {
            std::cerr << "timeout: server stopped answering, " << (client_count - finished)
                      << " clients unfinished\n";
            break;
        }

        for(int i = 0; i < num_events; i++) {
            int fd = active_events[i].data.fd;
            uint32_t ev = active_events[i].events;
            auto it = clients.find(fd);
            if(it == clients.end()) continue;
            ClientState& st = it->second;
            bool done = false;
            bool ok = false;

            if(ev & EPOLLOUT) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if(err != 0) {
                    std::cerr << "connect failed: " << strerror(err) << "\n";
                    done = true;
                }
                else if(st.sent < st.to_send.size()) {
                    while(st.sent < st.to_send.size()) {
                        ssize_t n = send(fd, st.to_send.data() + st.sent,
                                         st.to_send.size() - st.sent, MSG_NOSIGNAL);
                        if(n > 0) st.sent += n;
                        else if(n == -1 && errno == EINTR) continue;
                        else {
                            if(errno != EAGAIN && errno != EWOULDBLOCK) done = true;
                            break;
                        }
                    }
                    if(!done && st.sent == st.to_send.size()) {
                        struct epoll_event mod{};
                        mod.events = EPOLLIN | EPOLLET;
                        mod.data.fd = fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &mod);
                    }
                }
            }

            if(!done && (ev & (EPOLLIN | EPOLLERR | EPOLLHUP))) {
                char temp_buf[1024];
                while(1) {
                    ssize_t bytes_read = read(fd, temp_buf, sizeof(temp_buf));
                    if(bytes_read > 0) {
                        st.in_buf.append(temp_buf, bytes_read);
                    }
                    else if(bytes_read == 0) {
                        done = true;
                        break;
                    }
                    else {
                        if(errno == EINTR) continue;
                        if(errno != EAGAIN && errno != EWOULDBLOCK) done = true;
                        break;
                    }
                }

                size_t pos;
                while((pos = st.in_buf.find("\r\n")) != std::string::npos) {
                    std::string reply = st.in_buf.substr(0, pos);
                    st.in_buf.erase(0, pos + 2);
                    if(!reply_ok(st, reply)) {
                        wrong_replies++;
                        std::cerr << "unexpected reply #" << st.got_replies << ": '" << reply << "'\n";
                    }
                    if(!st.is_set_client && st.got_replies == 0 && reply == "nil") nil_replies++;
                    st.got_replies++;
                }
                if(st.got_replies >= st.expected_replies) { done = true; ok = true; }
            }

            if(done) {
                if(ok) client_processed++;
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                clients.erase(it);
                finished++;
            }
        }
    }

    for(auto& kv : clients) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, kv.first, nullptr);
        close(kv.first);
    }

    std::cout << "\nOUT OF " << client_count << ", " << client_processed
              << " WERE PROCESSED SUCCESSFULLY\n";
    std::cout << "Wrong replies = " << wrong_replies << "\n";
    std::cout << "GET mohan misses (nil) = " << nil_replies << "\n";

    close(epoll_fd);
    return 0;
}
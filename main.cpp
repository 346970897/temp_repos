#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <cstdlib>
#include <iostream>
#include <map>

const int MAX_EVENTS = 10;
const int BUFFER_SIZE = 1024;
const int PORT = 8080;
int socket_id = -1;
void sig_handle(int signum){
    close(socket_id);
}
void set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
}
int is_message_complete(const std::string & msg,size_t & pos){
    if(msg.empty()||msg.front()!='*') return -1;
    pos = msg.find('\n');
    if(pos==std::string::npos) return -1;
    int array_size = std::stoi(msg.substr(1,pos-1))*2;
    pos+=1;
    for (int i = 0; i < array_size; ++i){
        pos = msg.find('\n',pos+1);
        if(pos==std::string::npos) return 0;
    }
    return 1;
}
bool process_clent_message(std::string& msg){
    size_t pos = 0;
    try {
        int flag = is_message_complete(msg, pos);
        if (flag == 1) {
            std::cout << msg.substr(0, pos) << std::endl;
            msg.erase(0, pos); // 删除已处理的消息，包括结尾的\r\n
        } else if (flag == 0) {
            return false; // 消息不完整，可能需要等待更多数据
        } else {
            throw std::runtime_error("请求命令错误");
        }
    } catch (const std::exception &e) {
        std::cerr << "请求命令错误 " << e.what() << std::endl;
        msg.clear();
        return false;
    }
    return true;
}
int main() {
    int listener, epoll_fd;
    struct sockaddr_in addr;
    struct epoll_event event, events[MAX_EVENTS];

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == -1) {
        perror("socket");
        return -1;
    }
    socket_id = listener;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listener, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror("bind");
        return -1;
    }

    set_nonblocking(listener);

    int optval = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (listen(listener, SOMAXCONN) == -1) {
        perror("listen");
        return -1;
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return -1;
    }

    event.events = EPOLLIN;
    event.data.fd = listener;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener, &event) == -1) {
        perror("epoll_ctl");
        return -1;
    }
    std::map<int,std::string > client_buffers;
    while (true) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listener) {
                // Accept new connection
                while (true) {
                    struct sockaddr in_addr;
                    socklen_t in_len = sizeof(in_addr);
                    int conn_fd = accept(listener, &in_addr, &in_len);
                    if (conn_fd == -1) {
                        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                            // All connections processed
                            break;
                        } else {
                            perror("accept");
                            break;
                        }
                    }
                    set_nonblocking(conn_fd);
                    event.events = EPOLLIN | EPOLLET;
                    event.data.fd = conn_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &event) == -1) {
                        perror("epoll_ctl");
                        close(conn_fd);
                    }
                }
            } else {
                const int buffer_size = 1024;
                char buffer[buffer_size];
                int bytes_read;
                int client_fd = events[i].data.fd;
                // 尝试读取数据
                while ((bytes_read = recv(client_fd, buffer, buffer_size, 0)) > 0) {
                    // 将读取的数据追加到对应客户端的累积缓冲区
                    client_buffers[client_fd].append(buffer, bytes_read);
                   // std::cout<<buffer<<std::endl;
                }
                if (bytes_read < 0 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    // 如果读取失败且错误不是EAGAIN或EWOULDBLOCK，关闭连接
                    std::cerr << "Read error on client " << client_fd << std::endl;
                    close(client_fd);
                    client_buffers.erase(client_fd);
                } else if (bytes_read == 0) {
                    // 如果读取到0字节，表示客户端关闭了连接
                    std::cout << "Client " << client_fd << " disconnected." << std::endl;
                    close(client_fd);
                    client_buffers.erase(client_fd);
                } else{
                    std::string & buf = client_buffers[client_fd];
                    if(!process_clent_message(buf)){
                        continue;
                    }
                }
            }
            }
    }
    close(listener);
    return 0;
}



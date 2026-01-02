#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define PORT "3450"
#define BUFFER_SIZE 1024
#define MAX_CLIENTS 10

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in *)sa)->sin_addr);
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

int main(void) {
    int sockfd, newfd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;
    socklen_t sin_size;

    fd_set master_fds, read_fds;
    int fd_max;

    int client_sockets[MAX_CLIENTS];
    char buffer[BUFFER_SIZE];
    char addr_str[INET6_ADDRSTRLEN];

    for (int i = 0; i < MAX_CLIENTS; i++)
        client_sockets[i] = -1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rv = getaddrinfo(NULL, PORT, &hints, &servinfo);
    if (rv != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
            continue;

        int yes = 1;
        setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        break;
    }

    if (!p) {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    freeaddrinfo(servinfo);

    if (listen(sockfd, 10) == -1) {
        perror("listen");
        return 3;
    }

    printf("Server listening on port %s\n", PORT);

    FD_ZERO(&master_fds);
    FD_ZERO(&read_fds);

    FD_SET(sockfd, &master_fds);
    FD_SET(STDIN_FILENO, &master_fds);

    fd_max = sockfd;
    if (STDIN_FILENO > fd_max)
        fd_max = STDIN_FILENO;

    while (1) {
        read_fds = master_fds;

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(1);
        }

        for (int i = 0; i <= fd_max; i++) {
            if (!FD_ISSET(i, &read_fds))
                continue;

            
            if (i == sockfd) {
                sin_size = sizeof client_addr;
                newfd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size);
                if (newfd == -1) {
                    perror("accept");
                    continue;
                }

                FD_SET(newfd, &master_fds);
                if (newfd > fd_max)
                    fd_max = newfd;

                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (client_sockets[j] == -1) {
                        client_sockets[j] = newfd;
                        break;
                    }
                }

                inet_ntop(client_addr.ss_family,
                          get_in_addr((struct sockaddr *)&client_addr),
                          addr_str, sizeof addr_str);

                printf("New connection from %s (fd=%d)\n", addr_str, newfd);
            }

            
            else if (i == STDIN_FILENO) {
                char msg[256];

                if (fgets(msg, sizeof msg, stdin) == NULL)
                    continue;

                size_t len = strlen(msg);
                if (len == 0)
                    continue;

                for (int j = 0; j < MAX_CLIENTS; j++) {
                    int fd = client_sockets[j];
                    if (fd != -1) {
                        size_t total = 0;
                        while (total < len) {
                            ssize_t sent = send(fd, msg + total, len - total, 0);
                            if (sent <= 0) {
                                perror("send");
                                break;
                            }
                            total += sent;
                        }
                    }
                }
            }

            
            else {
                ssize_t nbytes = recv(i, buffer, BUFFER_SIZE - 1, 0);

                if (nbytes <= 0) {
                    if (nbytes == 0)
                        printf("Client %d disconnected\n", i);
                    else
                        perror("recv");

                    close(i);
                    FD_CLR(i, &master_fds);

                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (client_sockets[j] == i) {
                            client_sockets[j] = -1;
                            break;
                        }
                    }
                } else {
                    buffer[nbytes] = '\0';
                    printf("From client %d: %s", i, buffer);
                }
            }
        }
    }

    return 0;
}

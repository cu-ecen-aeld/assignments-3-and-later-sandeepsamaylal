#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested = 0;

static void signal_handler(int signo)
{
    (void)signo;
    stop_requested = 1;
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t written = 0;
    while (written < len) {
        ssize_t rc = write(fd, buf + written, len - written);
        if (rc == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        written += (size_t)rc;
    }
    return 0;
}

static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        ssize_t rc = send(fd, buf + sent, len - sent, 0);
        if (rc == -1) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)rc;
    }
    return 0;
}

static int send_file_to_client(int clientfd, const char *path)
{
    char filebuf[1024];
    int readfd = open(path, O_RDONLY);
    if (readfd == -1) {
        return -1;
    }

    while (1) {
        ssize_t rlen = read(readfd, filebuf, sizeof(filebuf));
        if (rlen == 0) {
            break;
        }
        if (rlen == -1) {
            if (errno == EINTR) {
                continue;
            }
            close(readfd);
            return -1;
        }

        if (send_all(clientfd, filebuf, (size_t)rlen) == -1) {
            close(readfd);
            return -1;
        }
    }

    if (close(readfd) == -1) {
        return -1;
    }

    return 0;
}

static int daemonize_process(void)
{
    int devnull;
    pid_t pid = fork();

    if (pid == -1) {
        return -1;
    }

    if (pid > 0) {
        exit(0);
    }

    if (setsid() == -1) {
        return -1;
    }

    if (chdir("/") == -1) {
        return -1;
    }

    devnull = open("/dev/null", O_RDWR);
    if (devnull == -1) {
        return -1;
    }

    if (dup2(devnull, STDIN_FILENO) == -1 ||
        dup2(devnull, STDOUT_FILENO) == -1 ||
        dup2(devnull, STDERR_FILENO) == -1) {
        close(devnull);
        return -1;
    }

    if (devnull > STDERR_FILENO) {
        if (close(devnull) == -1) {
            return -1;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *datafile = "/var/tmp/aesdsocketdata";
    int daemon_mode = 0;
    int opt;
    struct sockaddr_in serv_addr;
    struct sigaction sa;
    int optval = 1;

    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
        case 'd':
            daemon_mode = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
            return -1;
        }
    }

    if (optind != argc) {
        fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "socket failed: %s\n", strerror(errno));
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) == -1) {
        fprintf(stderr, "sigemptyset failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        fprintf(stderr, "sigaction SIGINT failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        fprintf(stderr, "sigaction SIGTERM failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        fprintf(stderr, "setsockopt failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(9000);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        fprintf(stderr, "bind failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    if (daemon_mode && daemonize_process() == -1) {
        fprintf(stderr, "daemonize failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 1) == -1) {
        fprintf(stderr, "listen failed: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    openlog("aesdsocket", 0, LOG_USER);

    while (!stop_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        char client_ip[INET_ADDRSTRLEN];
        char recvbuf[1024];
        char *packetbuf = NULL;
        size_t packetlen = 0;
        size_t packetcap = 0;
        int drop_packet = 0;
        int datafd;
        int clientfd;
        ssize_t recvlen;
        size_t i;

        clientfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (clientfd == -1) {
            if (errno == EINTR && stop_requested) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "accept failed: %s\n", strerror(errno));
            closelog();
            close(sockfd);
            return -1;
        }

        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL) {
            fprintf(stderr, "inet_ntop failed: %s\n", strerror(errno));
            close(clientfd);
            closelog();
            close(sockfd);
            return -1;
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        datafd = open(datafile, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (datafd == -1) {
            fprintf(stderr, "open datafile failed: %s\n", strerror(errno));
            close(clientfd);
            closelog();
            close(sockfd);
            return -1;
        }

        while (1) {
            recvlen = recv(clientfd, recvbuf, sizeof(recvbuf), 0);
            if (recvlen == 0) {
                break;
            }
            if (recvlen == -1) {
                if (errno == EINTR && stop_requested) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "recv failed: %s\n", strerror(errno));
                free(packetbuf);
                close(datafd);
                close(clientfd);
                closelog();
                close(sockfd);
                return -1;
            }

            for (i = 0; i < (size_t)recvlen; i++) {
                char ch = recvbuf[i];

                if (!drop_packet) {
                    if (packetlen + 1 > packetcap) {
                        size_t newcap = (packetcap == 0) ? 1024 : packetcap * 2;
                        char *newbuf = realloc(packetbuf, newcap);
                        if (newbuf == NULL) {
                            fprintf(stderr, "realloc failed: %s\n", strerror(errno));
                            drop_packet = 1;
                            packetlen = 0;
                        } else {
                            packetbuf = newbuf;
                            packetcap = newcap;
                        }
                    }

                    if (!drop_packet) {
                        packetbuf[packetlen++] = ch;
                    }
                }

                if (ch == '\n') {
                    if (drop_packet) {
                        drop_packet = 0;
                    } else {
                        if (write_all(datafd, packetbuf, packetlen) == -1) {
                            fprintf(stderr, "write datafile failed: %s\n", strerror(errno));
                            free(packetbuf);
                            close(datafd);
                            close(clientfd);
                            closelog();
                            close(sockfd);
                            return -1;
                        }
                        if (send_file_to_client(clientfd, datafile) == -1) {
                            fprintf(stderr, "send file to client failed: %s\n", strerror(errno));
                            free(packetbuf);
                            close(datafd);
                            close(clientfd);
                            closelog();
                            close(sockfd);
                            return -1;
                        }
                    }
                    packetlen = 0;
                }
            }
        }

        free(packetbuf);

        if (close(datafd) == -1) {
            fprintf(stderr, "close datafd failed: %s\n", strerror(errno));
            close(clientfd);
            closelog();
            close(sockfd);
            return -1;
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);

        if (close(clientfd) == -1) {
            fprintf(stderr, "close clientfd failed: %s\n", strerror(errno));
            closelog();
            close(sockfd);
            return -1;
        }
    }

    if (stop_requested) {
        syslog(LOG_INFO, "Caught signal, exiting");
    }

    closelog();

    if (close(sockfd) == -1) {
        fprintf(stderr, "close failed: %s\n", strerror(errno));
        return -1;
    }

    if (unlink(datafile) == -1 && errno != ENOENT) {
        fprintf(stderr, "unlink failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

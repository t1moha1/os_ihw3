#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

#define BUF_SIZE 128


static ssize_t read_line(int sock, char *buf, size_t maxlen) {
    size_t idx = 0;
    char c;

    while (idx < maxlen - 1) {
        ssize_t r = recv(sock, &c, 1, 0);
        if (r == 1) {
            buf[idx++] = c;
            if (c == '\n') {
                break;
            }
        } else if (r == 0) {
            return 0;
        } else {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
    }

    buf[idx] = '\0';
    return (ssize_t)idx;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <port> <sleep_time>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    int sleep_time = atoi(argv[3]);

    srand((unsigned)time(NULL));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port   = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    static int guest_id = 1;
    char name[32];
    char msg[BUF_SIZE];
    char resp[BUF_SIZE];

    while (1) {
        
        snprintf(name, sizeof(name), "Guest%d", guest_id++);
        int days = (rand() % 5) + 1;  

    
        snprintf(msg, sizeof(msg), "CHECKIN %s %d\n", name, days);
        if (send(sock, msg, strlen(msg), 0) < 0) {
            perror("send");
            break;
        }

       
        ssize_t n = read_line(sock, resp, sizeof(resp));
        if (n > 0) {
            fputs(resp, stdout);
        } else if (n == 0) {
            fprintf(stderr, "Server closed connection\n");
            break;
        } else {
            perror("recv");
            break;
        }

        sleep(sleep_time);
    }

    close(sock);
    return 0;
}


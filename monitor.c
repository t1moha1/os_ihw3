#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define BUF_SIZE 256

static int sock = -1;

static void* recv_thread(void *arg) {
    (void)arg;
    char buf[BUF_SIZE];
    while (1) {
        ssize_t n = recv(sock, buf, sizeof(buf)-1, 0);
        if (n > 0) {
            buf[n] = '\0';
            printf("[RECV] %s", buf);
            fflush(stdout);
        } else if (n == 0) {
            printf("[RECV] Сервер закрыл соединение.\n");
            exit(0);
        } else {
            if (errno == EINTR) continue;
            perror("recv");
            pthread_exit(NULL);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { 
        perror("socket"); return 1; 
    }

    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port   = htons(port)
    };
    if (inet_pton(AF_INET, ip, &srv.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return 1;
    }
    if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

    send(sock, "MONITOR\n", 8, 0);

    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, NULL);

    pthread_join(tid, NULL);

    close(sock);
    return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include "guest.h"

#define PORT_DEFAULT   8080
#define MAX_ROOMS      10
#define BACKLOG        10
#define DAY_TIME       6
#define MAX_MONITORS   10

static guest hotel[MAX_ROOMS];
static int free_counter = MAX_ROOMS;

static queue_node *q_head = NULL;
static queue_node *q_tail = NULL;
static ssize_t queue_len = 0;

static int monitor_socks[MAX_MONITORS];
static int monitor_count = 0;

static volatile sig_atomic_t running = 1;
static int server_fd = -1;

pthread_mutex_t mtx_rooms    = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_queue    = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_monitors = PTHREAD_MUTEX_INITIALIZER;

void sigint_handler(int sig) { 
    (void)sig;
    running = 0;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
}

void clean_queue(void) {
    pthread_mutex_lock(&mtx_queue);
    while (q_head) {
        queue_node *tmp = q_head;
        q_head = q_head->next;
        free(tmp);
    }
    q_tail = NULL;
    queue_len = 0;
    pthread_mutex_unlock(&mtx_queue);
}

void clean_listener(void) {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
        printf("Слушающий сокет закрыт.\n");
    }
}

void finally_msg(void) {
    printf("Сервер корректно завершил работу.\n");
}

void broadcast_monitor(const char *msg) {
    pthread_mutex_lock(&mtx_monitors);
    for (int i = 0; i < monitor_count; ) {
        if (send(monitor_socks[i], msg, strlen(msg), 0) < 0) {
            close(monitor_socks[i]);
            monitor_socks[i] = monitor_socks[--monitor_count];
        } else {
            ++i;
        }
    }
    pthread_mutex_unlock(&mtx_monitors);
}

void handle_checkin(int sock, const char *args) {
    char name[NAME_LEN];
    int days;
    if (sscanf(args, "%31s %d", name, &days) != 2) {
        const char *err = "ERROR неверный формат: CHECKIN <имя> <дни>\n";
        send(sock, err, strlen(err), 0);
        broadcast_monitor(err);
        return;
    }

    pthread_mutex_lock(&mtx_rooms);
    if (free_counter > 0) {
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (hotel[i].cur_days == 0) {
                strncpy(hotel[i].name, name, NAME_LEN-1);
                hotel[i].name[NAME_LEN-1] = '\0';
                hotel[i].cur_days = days;
                free_counter--;
                char resp[512];
                snprintf(resp, sizeof(resp),
                         "ASSIGNED Клиент %s → Номер %d на %d суток\n",
                         name, i+1, days);
                send(sock, resp, strlen(resp), 0);
                broadcast_monitor(resp);
                break;
            }
        }
    } else {
        pthread_mutex_unlock(&mtx_rooms);
        pthread_mutex_lock(&mtx_queue);
        queue_node *node = malloc(sizeof(*node));
        if (!node) { 
            perror("malloc"); exit(1); 
        }

        node->guest.cur_days = days;
        strncpy(node->guest.name, name, NAME_LEN-1);
        node->guest.name[NAME_LEN-1] = '\0';
        node->sock = sock;
        node->next = NULL;
        if (!q_tail) {
            q_head = q_tail = node;
        } else {
            q_tail->next = node;
            q_tail = node;
        }
        queue_len++;
        int pos = queue_len;
        pthread_mutex_unlock(&mtx_queue);

        char resp[256];
        snprintf(resp, sizeof(resp),
                 "QUEUED Клиент %s в очереди под номером %d\n",
                 name, pos);
        send(sock, resp, strlen(resp), 0);
        broadcast_monitor(resp);
        return;
    }
    pthread_mutex_unlock(&mtx_rooms);
}

void handle_queue_request(int sock) {
    pthread_mutex_lock(&mtx_queue);
    if (!q_head) {
        const char *empty = "QUEUE Пусто\n";
        send(sock, empty, strlen(empty), 0);
        broadcast_monitor(empty);
    } else {
        char resp[512] = "QUEUE Список ожидающих:\n";
        queue_node *p = q_head;
        for (int i = 1; p; p = p->next, i++) {
            char line[256];
            snprintf(line, sizeof(line),
                     "%d) %s (%d)\n",
                     i, p->guest.name, p->guest.cur_days);
            strncat(resp, line, sizeof(resp) - strlen(resp) - 1);
        }
        send(sock, resp, strlen(resp), 0);
        broadcast_monitor(resp);
    }
    pthread_mutex_unlock(&mtx_queue);
}

void* clock_thread(void *arg) {
    (void)arg;
    while (running) {
        sleep(DAY_TIME);

        
        pthread_mutex_lock(&mtx_rooms);
        for (int i = 0; i < MAX_ROOMS; i++) {
            if (hotel[i].cur_days > 0) {
                hotel[i].cur_days--;
                if (hotel[i].cur_days == 0) {
                    hotel[i].name[0] = '\0';
                    free_counter++;
                }
            }
        }
        pthread_mutex_unlock(&mtx_rooms);

        
        while (1) {
            pthread_mutex_lock(&mtx_rooms);
            int have_room = free_counter > 0;
            pthread_mutex_unlock(&mtx_rooms);

            pthread_mutex_lock(&mtx_queue);
            if (!have_room || !q_head) {
                pthread_mutex_unlock(&mtx_queue);
                break;
            }
            queue_node *node = q_head;
            q_head = q_head->next;
            if (!q_head) {
                q_tail = NULL;
            }
            queue_len--;
            pthread_mutex_unlock(&mtx_queue);

            
            pthread_mutex_lock(&mtx_rooms);
            for (int i = 0; i < MAX_ROOMS; i++) {
                if (hotel[i].cur_days == 0) {
                    hotel[i].cur_days = node->guest.cur_days;
                    strncpy(hotel[i].name, node->guest.name, NAME_LEN-1);
                    hotel[i].name[NAME_LEN-1] = '\0';
                    free_counter--;

                    char resp[256];
                    snprintf(resp, sizeof(resp),
                             "ASSIGNED Клиент %s → Номер %d на %d суток\n",
                             node->guest.name, i+1, node->guest.cur_days);

                    if (send(node->sock, resp, strlen(resp), 0) < 0) {
                        
                        close(node->sock);
                    } else {
                        broadcast_monitor(resp);
                    }
                    break;
                }
            }
            pthread_mutex_unlock(&mtx_rooms);
            free(node);
        }
    }
    return NULL;
}

void* handle_client(void *arg) {
    int sock = *(int*)arg;
    free(arg);

    char buf[128];
    while (1) {
        ssize_t len = recv(sock, buf, sizeof(buf)-1, 0);
        if (len <= 0) break;
        buf[len] = '\0';

        if (strncmp(buf, "MONITOR", 7) != 0) {
            broadcast_monitor(buf);
        }

        if (strncmp(buf, "CHECKIN", 7) == 0) {
            handle_checkin(sock, buf + 7);
        }
        else if (strncmp(buf, "QUEUE", 5) == 0) {
            handle_queue_request(sock);
        }
        else if (strncmp(buf, "MONITOR", 7) == 0) {
            pthread_mutex_lock(&mtx_monitors);
            if (monitor_count < MAX_MONITORS) {
                monitor_socks[monitor_count++] = sock;
                const char *ok = "OK монитор зарегистрирован\n";
                send(sock, ok, strlen(ok), 0);
            } else {
                const char *err = "ERROR слишком много мониторов\n";
                send(sock, err, strlen(err), 0);
            }
            pthread_mutex_unlock(&mtx_monitors);
        }
        else {
            const char *err = "ERROR неизвестная команда\n";
            send(sock, err, strlen(err), 0);
            broadcast_monitor(err);
        }
    }

    close(sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    int port = atoi(argv[1]);

    signal(SIGPIPE, SIG_IGN);

   
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (atexit(clean_queue)   != 0 ||
        atexit(clean_listener) != 0 ||
        atexit(finally_msg)    != 0)
    {
        fprintf(stderr, "Не удалось зарегистрировать atexit-функции\n");
        return 1;
    }

    struct sigaction sa = {0};
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction SIGINT");
        exit(1);
    }
    // ловим также SIGTERM, чтобы при 'kill' выполнить корректный shutdown
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction SIGTERM");
        exit(1);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(1);
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(1);
    }
    

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        exit(1);
    }

    printf("Сервер запущен на порту %d\n", port);

    pthread_t ticker;
    pthread_create(&ticker, NULL, clock_thread, NULL);
    pthread_detach(ticker);

    while (running) {
        int *pclient = malloc(sizeof(int));
        if (!pclient) continue;
        *pclient = accept(server_fd, NULL, NULL);
        if (*pclient < 0) {
            free(pclient);
            perror("accept");
            continue;
        }
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, pclient);
        pthread_detach(tid);
    }

    return 0;
}
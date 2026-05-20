#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PORT 9999
#define MAX_CLIENTS 10
#define SEAT_COUNT 20
#define BUFFER_SIZE 1024
#define SAVE_FILE "seats.txt"   // ไฟล์เก็บสถานะที่นั่ง

// ---- Seat Data ----
typedef struct {
    char name[4];
    int  booked;
} Seat;

Seat seats[SEAT_COUNT];
pthread_mutex_t seat_mutex = PTHREAD_MUTEX_INITIALIZER;

// ---- Client info struct ----
typedef struct {
    int  fd;
    char ip[INET_ADDRSTRLEN];
    int  port;
} ClientInfo;

// ---- บันทึกสถานะลงไฟล์ ----
// เรียกทุกครั้งที่มีการจองสำเร็จ
void save_seats() {
    FILE *f = fopen(SAVE_FILE, "w");
    if (!f) return;
    for (int i = 0; i < SEAT_COUNT; i++)
        fprintf(f, "%s %d\n", seats[i].name, seats[i].booked);
    fclose(f);
}

// ---- โหลดสถานะจากไฟล์ ----
// เรียกตอน server เริ่มทำงาน
void load_seats() {
    FILE *f = fopen(SAVE_FILE, "r");
    if (!f) return;  // ไม่มีไฟล์ = ครั้งแรก ใช้ค่า default
    for (int i = 0; i < SEAT_COUNT; i++)
        fscanf(f, "%s %d", seats[i].name, &seats[i].booked);
    fclose(f);
    printf("[Server] Seat data loaded from %s\n", SAVE_FILE);
}

// ---- เซ็ตค่าเริ่มต้นที่นั่ง แล้วโหลดไฟล์ทับ ----
void init_seats() {
    int idx = 0;
    for (int i = 1; i <= 10; i++) {
        snprintf(seats[idx].name, sizeof(seats[idx].name), "A%d", i);
        seats[idx].booked = 0;
        idx++;
    }
    for (int i = 1; i <= 10; i++) {
        snprintf(seats[idx].name, sizeof(seats[idx].name), "B%d", i);
        seats[idx].booked = 0;
        idx++;
    }
    load_seats();  // ถ้ามี seats.txt อยู่แล้ว โหลดทับ
}

int find_seat(const char *seat_name) {
    char upper[5];
    int i;
    for (i = 0; seat_name[i] && i < 4; i++)
        upper[i] = (seat_name[i] >= 'a' && seat_name[i] <= 'z')
                   ? seat_name[i] - 32 : seat_name[i];
    upper[i] = '\0';
    for (int j = 0; j < SEAT_COUNT; j++)
        if (strcmp(seats[j].name, upper) == 0) return j;
    return -1;
}

void send_msg(int fd, const char *msg) {
    send(fd, msg, strlen(msg), 0);
}

void handle_view(int client_fd, const char *zone) {
    char buf[BUFFER_SIZE];
    int pos = snprintf(buf, sizeof(buf), "VIEW_RESULT %s", zone);
    pthread_mutex_lock(&seat_mutex);
    for (int i = 0; i < SEAT_COUNT; i++)
        if (seats[i].name[0] == zone[0])
            pos += snprintf(buf + pos, sizeof(buf) - pos, " %s:%s",
                            seats[i].name, seats[i].booked ? "Booked" : "Available");
    pthread_mutex_unlock(&seat_mutex);
    snprintf(buf + pos, sizeof(buf) - pos, "\n");
    send_msg(client_fd, buf);
}

void handle_book(int client_fd, char *args) {
    char seat_name[8], name[64], email[64], phone[32];
    char *token;

    token = strtok(args, "|");
    if (!token) { send_msg(client_fd, "BOOK_INVALID UNKNOWN\n"); return; }
    strncpy(seat_name, token, sizeof(seat_name) - 1); seat_name[sizeof(seat_name)-1] = '\0';

    token = strtok(NULL, "|");
    if (!token) { send_msg(client_fd, "BOOK_INVALID UNKNOWN\n"); return; }
    strncpy(name, token, sizeof(name) - 1); name[sizeof(name)-1] = '\0';

    token = strtok(NULL, "|");
    if (!token) { send_msg(client_fd, "BOOK_INVALID UNKNOWN\n"); return; }
    strncpy(email, token, sizeof(email) - 1); email[sizeof(email)-1] = '\0';

    token = strtok(NULL, "|");
    if (!token) { send_msg(client_fd, "BOOK_INVALID UNKNOWN\n"); return; }
    strncpy(phone, token, sizeof(phone) - 1); phone[sizeof(phone)-1] = '\0';

    char upper_seat[8];
    int i;
    for (i = 0; seat_name[i] && i < 7; i++)
        upper_seat[i] = (seat_name[i] >= 'a' && seat_name[i] <= 'z')
                        ? seat_name[i] - 32 : seat_name[i];
    upper_seat[i] = '\0';

    int idx = find_seat(upper_seat);
    if (idx == -1) {
        char resp[BUFFER_SIZE];
        snprintf(resp, sizeof(resp), "BOOK_INVALID %s\n", upper_seat);
        send_msg(client_fd, resp);
        return;
    }

    pthread_mutex_lock(&seat_mutex);
    if (seats[idx].booked) {
        pthread_mutex_unlock(&seat_mutex);
        char resp[BUFFER_SIZE];
        snprintf(resp, sizeof(resp), "BOOK_TAKEN %s\n", upper_seat);
        send_msg(client_fd, resp);
        return;
    }
    seats[idx].booked = 1;
    save_seats();  // บันทึกทันทีหลังจองสำเร็จ
    pthread_mutex_unlock(&seat_mutex);

    printf("\n===== New Booking Received =====\n");
    printf("Concert: NackMei World Tour\n");
    printf("Name: %s\n", name);
    printf("Email: %s\n", email);
    printf("Phone: %s\n", phone);
    printf("Seat: %s\n", upper_seat);
    printf("Status: Booked\n");
    printf("===============================\n\n");
    fflush(stdout);

    char resp[BUFFER_SIZE];
    snprintf(resp, sizeof(resp), "BOOK_OK %s\n", upper_seat);
    send_msg(client_fd, resp);
}

// ---- Client thread ----
void *client_handler(void *arg) {
    ClientInfo *info = (ClientInfo *)arg;
    int client_fd    = info->fd;
    char ip[INET_ADDRSTRLEN];
    int  port;
    strncpy(ip, info->ip, sizeof(ip));
    port = info->port;
    free(info);

    char buf[BUFFER_SIZE];
    while (1) {
        memset(buf, 0, sizeof(buf));
        int bytes = recv(client_fd, buf, sizeof(buf) - 1, 0);

        if (bytes <= 0) {
            printf("[Disconnected] Client %s:%d lost connection.\n", ip, port);
            fflush(stdout);
            break;
        }

        buf[strcspn(buf, "\n")] = '\0';

        if (strncmp(buf, "VIEW ", 5) == 0) {
            handle_view(client_fd, buf + 5);
        } else if (strncmp(buf, "BOOK ", 5) == 0) {
            handle_book(client_fd, buf + 5);
        } else if (strcmp(buf, "EXIT") == 0) {
            printf("[Disconnected] Client %s:%d exited.\n", ip, port);
            fflush(stdout);
            break;
        }
    }

    close(client_fd);
    return NULL;
}

// ---- Main ----
int main() {
    init_seats();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen"); exit(1);
    }

    printf("[Server] NackMei World Tour Booking Server started on port %d\n", PORT);
    printf("[Server] Waiting for connections...\n\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) { perror("accept"); continue; }

        ClientInfo *info = malloc(sizeof(ClientInfo));
        info->fd   = client_fd;
        info->port = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &client_addr.sin_addr, info->ip, sizeof(info->ip));

        printf("[Connected] Client %s:%d connected.\n", info->ip, info->port);
        fflush(stdout);

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, info);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}
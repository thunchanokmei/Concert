#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9999
#define HOST "127.0.0.1"
#define BUFFER_SIZE 1024

int sock_fd;

// ---- Network helpers ----
void send_msg(const char *msg) {
    send(sock_fd, msg, strlen(msg), 0);
}

int recv_msg(char *buf, int size) {
    memset(buf, 0, size);
    int bytes = recv(sock_fd, buf, size - 1, 0);
    if (bytes > 0) buf[strcspn(buf, "\n")] = '\0';
    return bytes;
}

// ---- Input helper ----
void read_line(const char *prompt, char *buf, int size) {
    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, size, stdin)) {
        buf[strcspn(buf, "\n")] = '\0';
    }
}

// ---- View Seats ----
void show_seats(const char *response) {
    // response format: VIEW_RESULT <zone> <seat>:<status> ...
    char tmp[BUFFER_SIZE];
    strncpy(tmp, response, sizeof(tmp) - 1);

    char *token = strtok(tmp, " ");  // "VIEW_RESULT"
    token = strtok(NULL, " ");       // zone letter
    if (!token) return;

    printf("\n===== Zone %s Seats =====\n", token);

    while ((token = strtok(NULL, " ")) != NULL) {
        char seat[8], status[16];
        char *colon = strchr(token, ':');
        if (!colon) continue;
        *colon = '\0';
        strncpy(seat,   token,    sizeof(seat)   - 1); seat[sizeof(seat)-1]     = '\0';
        strncpy(status, colon+1, sizeof(status) - 1); status[sizeof(status)-1] = '\0';

        // Pad seat name to width 3 for alignment
        if (strlen(seat) == 2)
            printf("%s  - %s\n", seat, status);
        else
            printf("%s - %s\n", seat, status);
    }
}

void view_seats_menu() {
    while (1) {
        printf("\nPlease select zone:\n");
        printf("1. Zone A\n");
        printf("2. Zone B\n");
        printf("3. Back to main menu\n");

        char choice[8];
        read_line("\nPlease select an option: ", choice, sizeof(choice));

        const char *zone = NULL;
        if (strcmp(choice, "1") == 0)      zone = "A";
        else if (strcmp(choice, "2") == 0) zone = "B";
        else if (strcmp(choice, "3") == 0) return;
        else { printf("Invalid option. Please try again.\n"); continue; }

        // Request seats from server
        char req[32];
        snprintf(req, sizeof(req), "VIEW %s\n", zone);
        send_msg(req);

        char buf[BUFFER_SIZE];
        if (recv_msg(buf, sizeof(buf)) <= 0) {
            printf("Server disconnected.\n"); return;
        }
        show_seats(buf);

        printf("\nDo you want to book a ticket?\n");
        printf("1. Book now\n");
        printf("2. Back to main menu\n");
        char sub[8];
        read_line("\nPlease select an option: ", sub, sizeof(sub));

        if (strcmp(sub, "1") == 0) {
            // Forward to booking — handled in main loop
            // We signal via a simple flag trick: just call extern
            extern void book_ticket_menu();
            book_ticket_menu();
            return;
        } else if (strcmp(sub, "2") == 0) {
            return;
        } else {
            printf("Invalid option.\n");
        }
    }
}

// ---- Book Ticket ----
void book_ticket_menu() {
    printf("\n===== Booking Form =====\n\n");

    char name[64], email[64], phone[32], seat[8];

    read_line("Name: ",         name,  sizeof(name));
    read_line("Email: ",        email, sizeof(email));
    read_line("Phone Number: ", phone, sizeof(phone));
    read_line("Seat Number: ",  seat,  sizeof(seat));

    if (strlen(name) == 0 || strlen(email) == 0 ||
        strlen(phone) == 0 || strlen(seat) == 0) {
        printf("All fields are required.\n");
        return;
    }

    // Send BOOK <seat>|<name>|<email>|<phone>
    char req[BUFFER_SIZE];
    snprintf(req, sizeof(req), "BOOK %s|%s|%s|%s\n", seat, name, email, phone);
    send_msg(req);

    char buf[BUFFER_SIZE];
    if (recv_msg(buf, sizeof(buf)) <= 0) {
        printf("Server disconnected.\n"); return;
    }

    // Parse response
    char *sp = strchr(buf, ' ');
    char seat_upper[8] = "";
    if (sp) strncpy(seat_upper, sp + 1, sizeof(seat_upper) - 1);

    if (strncmp(buf, "BOOK_OK", 7) == 0) {
        printf("\nBooking successful!\n");
        printf("Seat %s has been booked.\n", seat_upper);
        printf("Thank you for booking NackMei World Tour.\n");
    } else if (strncmp(buf, "BOOK_TAKEN", 10) == 0) {
        printf("\nSorry, seat %s is already booked.\n", seat_upper);
        printf("Please select another seat.\n");
    } else if (strncmp(buf, "BOOK_INVALID", 12) == 0) {
        printf("\nInvalid seat number.\n");
        printf("Please enter a valid seat such as A1-A10 or B1-B10.\n");
    } else {
        printf("Unexpected response from server.\n");
    }
}

// ---- Main ----
int main() {
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, HOST, &server_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Error] Cannot connect to server. Make sure server is running");
        exit(1);
    }

    while (1) {
        printf("\n===== NackMei World Tour Ticket Booking =====\n");
        printf("1. View Seats\n");
        printf("2. Book Ticket\n");
        printf("3. Exit\n");

        char choice[8];
        read_line("\nPlease select an option: ", choice, sizeof(choice));

        if (strcmp(choice, "1") == 0) {
            view_seats_menu();
        } else if (strcmp(choice, "2") == 0) {
            book_ticket_menu();
        } else if (strcmp(choice, "3") == 0) {
            printf("\nThank you. Goodbye!\n");
            send_msg("EXIT\n");
            break;
        } else {
            printf("Invalid option. Please try again.\n");
        }
    }

    close(sock_fd);
    return 0;
}

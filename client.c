#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_IP 0.0.0.0
#define SERVER_PORT 5555
#define BUFFER_SIZE 1024

// Function to handle reading messages from the server
void *read_server_messages(void *socket_desc) {
    int sock = *(int *)socket_desc;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
        buffer[bytes_read] = '\0';
        // If the message is the same as the previous one, skip printing
        printf("%s", buffer);
        fflush(stdout);
    }

    if (bytes_read == 0) {
        printf("Connection to the server was closed.\n");
    } else {
        perror("Error while receiving message");
    }

    close(sock);
    exit(0);
}

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    pthread_t reader_thread;

    // Create a socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to the server failed");
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server %s:%d\n", SERVER_IP, SERVER_PORT);

    // Create a thread to read messages from the server
    if (pthread_create(&reader_thread, NULL, read_server_messages, (void *)&sock) < 0) {
        perror("Failed to create a thread for reading messages");
        exit(EXIT_FAILURE);
    }

    // Main loop: sending messages to the server
    while (1) {
        fgets(buffer, BUFFER_SIZE, stdin);

        // Send the message to the server
        if (send(sock, buffer, strlen(buffer), 0) < 0) {
            perror("Failed to send message");
            break;
        }
    }

    // Close the socket and exit
    close(sock);
    return 0;
}

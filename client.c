#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 10000

void receive_file(int client_socket, const char *filename) {
    // Receive the file size from the server
    long file_size;
    ssize_t size_received = recv(client_socket, &file_size, sizeof(long), 0);

    if (size_received <= 0) {
        fprintf(stderr, "Error receiving file size\n");
        return;
    }

    // Allocate memory to receive the file content
    char *file_content = (char *)malloc(file_size);
    if (file_content == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        return;
    }

    // Receive the file content in chunks
    ssize_t total_received = 0;
    while (total_received < file_size) {
        ssize_t bytes_received = recv(client_socket, file_content + total_received, file_size - total_received, 0);
        if (bytes_received <= 0) {
            fprintf(stderr, "Error receiving file content\n");
            free(file_content);
            return;
        }
        total_received += bytes_received;
    }

    // Write the file content to a local file
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        perror("Error opening file for writing");
        free(file_content);
        return;
    }

    fwrite(file_content, 1, file_size, file);
    fclose(file);

    printf("File received and saved as %s\n", filename);

    free(file_content);
}

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int client_socket;
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE];

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_address.sin_addr) <= 0)
    {
        perror("Invalid server address");
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(client_socket, (struct sockaddr *)&server_address, sizeof(server_address)) == -1)
    {
        perror("Error connecting to server");
        exit(EXIT_FAILURE);
    }

    printf("Connected to server\n");

    // Send data to the server and receive the response
    while (1)
    {
        printf("Enter command: ");
        fgets(buffer, sizeof(buffer), stdin);
        // Check for exit command
        if (strncmp(buffer, "exit", 4) == 0)
        {
            break;
        }
        

        send(client_socket, buffer, strlen(buffer), 0);

        // if (strncmp(buffer, "print file", 10) == 0) {
        //     // Extract the filename from the command
        //     char filename[100];
        //     if (sscanf(buffer, "print file %s", filename) == 1) {
        //         receive_file(client_socket, filename);
        //     } else {
        //         fprintf(stderr, "Invalid print file command\n");
        //     }
        // }

        // Receive and print the response from the server
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            printf("Server disconnected\n");
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate the received data
        printf("Received from server: %s", buffer);
    }

    // Close socket
    close(client_socket);

    return 0;
}

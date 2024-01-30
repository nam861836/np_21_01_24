#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 10000
void search_flights(int client_socket, const char *search_query)
{
    send(client_socket, search_query, strlen(search_query), 0);

    char result[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_socket, result, sizeof(result), 0);

    if (bytes_received > 0)
    {
        result[bytes_received] = '\0'; // Null-terminate the received data
        printf("%s\n", result);
    }
    else
    {
        printf("Error receiving search results\n");
    }
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
        printf("Enter command (/help to list all commands): ");
        fgets(buffer, sizeof(buffer), stdin);
        // Check for exit command
        if (strncmp(buffer, "exit", 4) == 0)
        {
            break;
        }

        send(client_socket, buffer, strlen(buffer), 0);

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

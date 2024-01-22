#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <time.h>
#define BUFFER_SIZE 1024
#define DATABASE_NAME "airticket.db"

int user_id=-1;
int execute_query(sqlite3* db, const char* query);

void generate_random_string(char *random_string, size_t length) {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const size_t charset_size = sizeof(charset) - 1;

    srand((unsigned int)time(NULL));

    for (size_t i = 0; i < length; ++i) {
        random_string[i] = charset[rand() % charset_size];
    }

    random_string[length] = '\0'; // Null-terminate the string
}


int get_user_id(sqlite3* db, const char* username) {
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT id FROM users WHERE username = '%s';", username);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int user_id = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            return user_id;
        }
        sqlite3_finalize(stmt);
    }

    return -1; // Return -1 if user not found or an error occurred
}

float get_ticket_price(sqlite3* db, const char* flight_num) {
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT * FROM Flights WHERE flight_num = '%s';", flight_num);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int ticket_price = sqlite3_column_double(stmt, 4);
            sqlite3_finalize(stmt);
            return ticket_price;
        }
        sqlite3_finalize(stmt);
    }

    return -1; // Return -1 if user not found or an error occurred
}


int book_ticket(sqlite3* db, const char* ticket_code, int user_id, const char* flight_num, char *seat_class, int num_seat, float ticket_price) {
    char insert_query[200];
    snprintf(insert_query, sizeof(insert_query), "INSERT INTO Tickets (ticket_code, user_id, flight_num, seat_class, num_seat, ticket_price) VALUES ('%s', %d, '%s', '%s', %d, %f);", ticket_code, user_id, flight_num, seat_class, num_seat, ticket_price);

    return execute_query(db, insert_query);
}


int is_login(sqlite3* db, int client_socket, char* username){
    int a = get_user_id(db, username);

}

int parse_search_command(const char *search_command, char *departure_point, char *destination_point, char *departure_date, char *return_date) {
    return sscanf(search_command, "search %49s %49s %19s %19s", departure_point, destination_point, departure_date, return_date);
}

int execute_query(sqlite3* db, const char* query) {
    char* error_message = NULL;
    int result = sqlite3_exec(db, query, 0, 0, &error_message);

    if (result != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", error_message);
        sqlite3_free(error_message);
    }

    return result;
}

int register_user(sqlite3* db, const char* username, const char* password) {
    char insert_query[100];
    snprintf(insert_query, sizeof(insert_query), "INSERT INTO users (username, password) VALUES ('%s', '%s');", username, password);

    return execute_query(db, insert_query);
}

int login_user(sqlite3* db, const char* username, const char* password) {
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT * FROM users WHERE username = '%s' AND password = '%s';", username, password);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        int result = (sqlite3_step(stmt) == SQLITE_ROW) ? 1 : 0;
        sqlite3_finalize(stmt);
        return result;
    }

    return 0;
}
int logout_user() {
    return 1;  // Successful logout for demonstration purposes
}

int search_flights(const char *departure_point, const char *destination_point, const char *departure_date, const char *return_date, char *result) {
    sqlite3 *db;
    int rc;

    rc = sqlite3_open(DATABASE_NAME, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    char query[512];
    sprintf(query, "SELECT flight_num, company, departure_point, destination_point, departure_date, return_date, seat_class_A, seat_class_B, price_A, price_B FROM Flights WHERE departure_point='%s' AND destination_point='%s' AND departure_date='%s' AND return_date='%s';\n",
            departure_point, destination_point, departure_date, return_date);

    sqlite3_stmt *stmt;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    strcpy(result, "Flights found:\n");

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        for (int i = 0; i < sqlite3_column_count(stmt); i++) {
            const unsigned char *column_value = sqlite3_column_text(stmt, i);
            const char *column_name = sqlite3_column_name(stmt, i);

            strcat(result, column_name);
            strcat(result, ": ");
            strcat(result, (char *)column_value);
            strcat(result, "\n");
        }
        strcat(result, "\n");
    }

    if (strcmp(result, "Flights found:\n") == 0) {
        strcpy(result, "No flights found.\n");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_len = sizeof(client_address);
    char buffer[BUFFER_SIZE];

    sqlite3* db;
    if (sqlite3_open(DATABASE_NAME, &db) != SQLITE_OK) {
        fprintf(stderr, "Error opening database: %s\n", sqlite3_errmsg(db));
        exit(EXIT_FAILURE);
    }

    // Create a table for user data if it doesn't exist
    const char* create_table_query = "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT, password TEXT);";
    if (execute_query(db, create_table_query) != SQLITE_OK) {
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Set up server address
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    // Bind socket to address and port
    if (bind(server_socket, (struct sockaddr*)&server_address, sizeof(server_address)) == -1) {
        perror("Error binding socket");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, 5) == -1) {
        perror("Error listening for connections");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", port);

    // Accept incoming connection
    client_socket = accept(server_socket, (struct sockaddr*)&client_address, &client_address_len);
    if (client_socket == -1) {
        perror("Error accepting connection");
        exit(EXIT_FAILURE);
    }

    printf("Client connected\n");

    while (1) {
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            printf("Client disconnected\n");
            break;
        }

        buffer[bytes_received] = '\0'; // Null-terminate the received data
        printf("Received from client: %s", buffer);

        // Parse the input and handle register or login
        char command[20], username[50], password[50];
        
        if (sscanf(buffer, "%s %s %s", command, username, password) == 3) {
            if (strcmp(command, "register") == 0) {
                // Handle registration
                if (register_user(db, username, password) == SQLITE_OK) {
                    send(client_socket, "Register success\n", 17, 0);
                } else {
                    send(client_socket, "Register failed\n", 16, 0);
                }
            } else if (strcmp(command, "login") == 0) {
                // Handle login
                if (login_user(db, username, password)) {
                    user_id = get_user_id(db, username);
                    send(client_socket, "Login success\n", 14, 0);
                    
                } else {
                    send(client_socket, "Login failed\n", 13, 0);
                }
            } else if (strcmp(command, "logout") == 0) {
                if (logout_user()) {
                send(client_socket, "Logout success\n", 15, 0);
                } else {
                send(client_socket, "Logout failed\n", 14, 0);
                }
            } else if (strncmp(buffer, "search", 6) == 0) {
            // Search
            char departure_point[50], destination_point[50], departure_date[20], return_date[20];
            if (parse_search_command(buffer, departure_point, destination_point, departure_date, return_date) == 4) {
                char search_result[BUFFER_SIZE];
                search_flights(departure_point, destination_point, departure_date, return_date, search_result);
                send(client_socket, search_result, strlen(search_result), 0);
            } else {
                send(client_socket, "Invalid search command", sizeof("Invalid search command"), 0);
            }
            } else if (strncmp(buffer, "book", 4) == 0) {
                char flight_num[20], seat_class[10], ticket_code[1000];
                int num_seat;
                float ticket_price;
                // Book a ticket
                // int user_id = get_user_id(db, username);
                if (user_id != -1) {
                    if (sscanf(buffer, "book %s %s %d", flight_num, seat_class, &num_seat) == 3) {
                        char str[10];
                        ticket_price = num_seat * get_ticket_price(db, flight_num);
                        for (int i = 0; i < num_seat; i++) {
                            char str[7]; // 6 characters + 1 for null-terminator

                            // Clear ticket_code before each iteration
                            ticket_code[0] = '\0';

                            for (int j = 0; j < num_seat; j++) {
                                generate_random_string(str, 6);
                                strcat(ticket_code, str);
                                strcat(ticket_code, "-");
                            }
                        }
                        
                        if (book_ticket(db, ticket_code, user_id, flight_num, seat_class, num_seat, ticket_price) == SQLITE_OK) {
                            send(client_socket, "Booking success\n", 17, 0);
                        } else {
                            send(client_socket, "Booking failed\n", 16, 0);
                        }
                        
                    } else {
                        send(client_socket, "Invalid book command", sizeof("Invalid book command"), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                }
            } else if (strncmp(buffer, "view", 4) == 0) {
                // View booked tickets
                if (user_id != -1) {
                    char select_query[100];
                    snprintf(select_query, sizeof(select_query), "SELECT * FROM Tickets WHERE user_id = %d;", user_id);

                    sqlite3_stmt* stmt;
                    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
                        char result[BUFFER_SIZE];
                        strcpy(result, "Tickets found:\n");

                        while (sqlite3_step(stmt) == SQLITE_ROW) {
                            for (int i = 0; i < sqlite3_column_count(stmt); i++) {
                                const unsigned char *column_value = sqlite3_column_text(stmt, i);
                                const char *column_name = sqlite3_column_name(stmt, i);

                                strcat(result, column_name);
                                strcat(result, ": ");
                                strcat(result, (char *)column_value);
                                strcat(result, "\n");
                            }
                            strcat(result, "\n");
                        }

                        if (strcmp(result, "Tickets found:\n") == 0) {
                            strcpy(result, "No tickets found.\n");
                        }

                        sqlite3_finalize(stmt);
                        send(client_socket, result, strlen(result), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                }
            } else if (strncmp(buffer, "cancel", 6) == 0) {
                // Cancel a ticket
                if (user_id != -1) {
                    char ticket_code[1000];
                    if (sscanf(buffer, "cancel ticket %s", ticket_code) == 1) {
                        char search_query[100], delete_query[100];
                        snprintf(search_query, sizeof(search_query), "SELECT * FROM Tickets WHERE ticket_code = '%s';", ticket_code);
                        
                        if (execute_query(db, search_query) != SQLITE_OK) {
                            send(client_socket, "Ticket not found\n", 17, 0);
                        } else {
                            snprintf(delete_query, sizeof(delete_query), "DELETE FROM Tickets WHERE ticket_code = '%s';", ticket_code);
                            
                            if (execute_query(db, delete_query) == SQLITE_OK) {
                                send(client_socket, "Ticket canceled\n", 16, 0);
                            } else {
                                send(client_socket, "Failed to cancel ticket\n", 25, 0);
                            }
                        }
                    } else {
                        send(client_socket, "Invalid cancel command", sizeof("Invalid cancel command"), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                }
            } else {
                send(client_socket, "Invalid command\n", 16, 0);
            }
        }
        else {
            send(client_socket, "Invalid command format\n", 23, 0);
        }
    
    }
        // Close sockets
        close(client_socket);
        close(server_socket);

        return 0;
}


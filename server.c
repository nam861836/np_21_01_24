#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <time.h>
#define BUFFER_SIZE 1024
#define DATABASE_NAME "airticket.db"

int user_id = -1;
int user_type = 2;
 
char user[50]; 
int execute_query(sqlite3* db, const char* query);

void *handle_client(void *arg) {
    int client_socket = *((int *)arg);
    char buffer[BUFFER_SIZE];

    // Your existing code for handling a single client goes here...

    // Close the client socket when done
    close(client_socket);

    // Exit the thread
    pthread_exit(NULL);
}

int get_user_ids_from_query(sqlite3 *db, const char *query, int *user_ids) {
    sqlite3_stmt *stmt;
    int num_users = 0;

    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (num_users < 100) {
                user_ids[num_users++] = sqlite3_column_int(stmt, 0);
            } else {
                // Handle the case where the number of users exceeds MAX_USERS
                // You may want to adjust this based on your requirements
                break;
            }
        }
        sqlite3_finalize(stmt);
    }

    return num_users;
}

void see_notifications(sqlite3* db, int user_id, int client_socket) {
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT notification FROM Notifications WHERE user_id = %d;", user_id);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        char result[BUFFER_SIZE];
        strcpy(result, "Notifications:\n");

        while (sqlite3_step(stmt) == SQLITE_ROW) {

            for (int i = 0; i < sqlite3_column_count(stmt); i++) {
                const unsigned char *column_value = sqlite3_column_text(stmt, i);
                const char *column_name = sqlite3_column_name(stmt, i);
                sprintf(result, "\nSTT: %d\n", i + 1);
                strcat(result, (char *)column_value);
                strcat(result, "\n");
            }
            strcat(result, "\n");
        }

        if (strcmp(result, "Notifications:\n") == 0) {
            strcpy(result, "No notifications found.\n");
        }

        sqlite3_finalize(stmt);
        send(client_socket, result, strlen(result), 0);
    }
}


void print_ticket_to_file(sqlite3* db, int client_socket, const char* ticket_code, const char* filename) {
    FILE* file = fopen(filename, "w");
    
    if (file == NULL) {
        // Handle file opening error
        perror("Error opening file");
        return;
    }

    char query[100];
    snprintf(query, sizeof(query), "SELECT * FROM Tickets WHERE ticket_code = '%s';", ticket_code);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, 0) == SQLITE_OK) {
        int result = sqlite3_step(stmt);
        if (result == SQLITE_ROW) {
            char flight_num[20], seat_class[10];
            int num_seat, ticket_price, user_id;

            user_id = sqlite3_column_int(stmt, 1);
            strcpy(flight_num, sqlite3_column_text(stmt, 2));
            strcpy(seat_class, sqlite3_column_text(stmt, 3));
            num_seat = sqlite3_column_int(stmt, 4);
            ticket_price = sqlite3_column_int(stmt, 5);


            fprintf(file, "----------------------------------------\n");
            fprintf(file, "Username: %s\n", user);
            fprintf(file, "Ticket Code: %s\n", ticket_code);
            fprintf(file, "Flight Number: %s\n", flight_num);
            fprintf(file, "Seat Class: %s\n", seat_class);
            fprintf(file, "Number of Seats: %d\n", num_seat);
            fprintf(file, "Ticket Price: %d\n", ticket_price);

            // Additional flight information if needed
            // Retrieve flight information
            char flight_query[100];
            snprintf(flight_query, sizeof(flight_query), "SELECT * FROM Flights WHERE flight_num = '%s';", flight_num);

            sqlite3_stmt* flight_stmt;
            if (sqlite3_prepare_v2(db, flight_query, -1, &flight_stmt, 0) == SQLITE_OK) {
                int flight_result = sqlite3_step(flight_stmt);
                if (flight_result == SQLITE_ROW) {
                    char departure[50], destination[50], departure_date[20], return_date[20];
                    strcpy(departure, sqlite3_column_text(flight_stmt, 6));
                    strcpy(destination, sqlite3_column_text(flight_stmt, 7));
                    strcpy(departure_date, sqlite3_column_text(flight_stmt, 8));
                    strcpy(return_date, sqlite3_column_text(flight_stmt, 9));

                    fprintf(file, "Departure: %s\n", departure);
                    fprintf(file, "Destination: %s\n", destination);
                    fprintf(file, "Departure Date: %s\n", departure_date);
                    fprintf(file, "Return Date: %s\n", return_date);
                }
                sqlite3_finalize(flight_stmt);
            }
            // End of additional flight information

            send(client_socket, "Ticket printed to file\n", 24, 0);
        } else {
            send(client_socket, "Ticket not found\n", 17, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        send(client_socket, "Error querying database\n", 24, 0);
    }

    fclose(file);
}

int reduce_available_seats(sqlite3* db, const char* flight_num, const char* seat_class, int num_seat) {
    char update_query[100];
    int current_available_seats;

    // Retrieve current available seats
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT * FROM Flights WHERE flight_num = '%s';", flight_num);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (strcmp(seat_class, "A") == 0) {
                current_available_seats = sqlite3_column_int(stmt, 2);
            } else if (strcmp(seat_class, "B") == 0) {
                current_available_seats = sqlite3_column_int(stmt, 3);
            }
            sqlite3_finalize(stmt);
        } else {
            sqlite3_finalize(stmt);
            return -1;  // Flight not found
        }
    } else {
        return -2;  // SQL query error
    }

    // Check if there are enough available seats
    if (current_available_seats < num_seat) {
        return -3;  // Not enough available seats
    }

    // Update available seats
    snprintf(update_query, sizeof(update_query), "UPDATE Flights SET %s = %s - %d WHERE flight_num = '%s';",
             (strcmp(seat_class, "A") == 0) ? "seat_class_A" : "seat_class_B",
             (strcmp(seat_class, "A") == 0) ? "seat_class_A" : "seat_class_B",
             num_seat, flight_num);

    if (sqlite3_exec(db, update_query, 0, 0, 0) == SQLITE_OK) {
        return 0;  // Update successful
    } else {
        return -4;  // Update error
    }
}

int increase_available_seats(sqlite3* db, const char* flight_num, const char* seat_class, int num_seat) {
    char update_query[100];
    int current_available_seats;

    // Retrieve current available seats
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT * FROM Flights WHERE flight_num = '%s';", flight_num);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (strcmp(seat_class, "A") == 0) {
                current_available_seats = sqlite3_column_int(stmt, 2);
            } else if (strcmp(seat_class, "B") == 0) {
                current_available_seats = sqlite3_column_int(stmt, 3);
            }
            sqlite3_finalize(stmt);
        } else {
            sqlite3_finalize(stmt);
            return -1;  // Flight not found
        }
    } else {
        return -2;  // SQL query error
    }

    // Check if there are enough available seats
    if (current_available_seats < num_seat) {
        return -3;  // Not enough available seats
    }

    // Update available seats
    snprintf(update_query, sizeof(update_query), "UPDATE Flights SET %s = %s + %d WHERE flight_num = '%s';",
             (strcmp(seat_class, "A") == 0) ? "seat_class_A" : "seat_class_B",
             (strcmp(seat_class, "A") == 0) ? "seat_class_A" : "seat_class_B",
             num_seat, flight_num);

    if (sqlite3_exec(db, update_query, 0, 0, 0) == SQLITE_OK) {
        return 0;  // Update successful
    } else {
        return -4;  // Update error
    }
}

int makePayment(sqlite3* db, const char* ticket_code, int credit_card, int cvv, int user_id) {
    char search_query1[100], search_query2[100], update_query[100], insert_query[100];
    int isPaid = 0;
    int credit_card_value, cvv_value;
    sqlite3_stmt* stmt;

    snprintf(search_query1, sizeof(search_query1), "SELECT * FROM Tickets WHERE ticket_code = '%s';", ticket_code);
    if (sqlite3_prepare_v2(db, search_query1, -1, &stmt, 0) == SQLITE_OK) {
        int result = sqlite3_step(stmt);
        if (result != SQLITE_ROW) return -1;
    }

    snprintf(search_query2, sizeof(search_query2), "SELECT * FROM users WHERE id = %d;", user_id);
    if (sqlite3_prepare_v2(db, search_query2, -1, &stmt, 0) == SQLITE_OK) {
        int result = sqlite3_step(stmt);
        if (result == SQLITE_ROW) {
            credit_card_value = sqlite3_column_int(stmt, 3);
            cvv_value = sqlite3_column_int(stmt, 4);

            if (credit_card_value == 0 && cvv_value == 0) {
                snprintf(insert_query, sizeof(insert_query), "UPDATE users SET credit_card = %d, cvv = %d WHERE id = %d;", credit_card, cvv, user_id);
                if (execute_query(db, insert_query) == SQLITE_OK) {
                    snprintf(update_query, sizeof(update_query), "UPDATE Tickets SET isPaid = 1 WHERE ticket_code = '%s';", ticket_code);
                    if (execute_query(db, update_query) == SQLITE_OK) {
                    isPaid = 1; // Ticket paid successfully
                    }
                } return isPaid;
            } else if (credit_card_value != credit_card || cvv_value != cvv){
                return -2;
            } else if (credit_card_value == credit_card && cvv_value == cvv){
                snprintf(update_query, sizeof(update_query), "UPDATE Tickets SET isPaid = 1 WHERE ticket_code = '%s';", ticket_code);
                if (execute_query(db, update_query) == SQLITE_OK) {
                    isPaid = 1; // Ticket paid successfully
                } return isPaid;
            }
        }
    } 
    return 0;
}

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

int get_ticket_price(sqlite3* db, const char* flight_num, const char* seat_class) {
    char select_query[100];
    int ticket_price;
    snprintf(select_query, sizeof(select_query), "SELECT * FROM Flights WHERE flight_num = '%s';", flight_num);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (strcmp(seat_class, "A") == 0){
            ticket_price = sqlite3_column_double(stmt, 4);
            } else if (strcmp(seat_class, "B") == 0){
            ticket_price = sqlite3_column_double(stmt, 5);
            }
            sqlite3_finalize(stmt);
            return ticket_price;
        }
        sqlite3_finalize(stmt);
    }

    return -1; // Return -1 if user not found or an error occurred
}

int check_available_seats(sqlite3* db, const char* flight_num, const char* seat_class, int num_seat) {
    char select_query[100];
    snprintf(select_query, sizeof(select_query), "SELECT * FROM Flights WHERE flight_num = '%s';", flight_num);
    int available_seats;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            if (strcmp(seat_class, "A") == 0){
            available_seats = sqlite3_column_int(stmt, 2);
            } else if (strcmp(seat_class, "B") == 0){
            available_seats = sqlite3_column_int(stmt, 3);
            }
            sqlite3_finalize(stmt);
            return (available_seats >= num_seat);
        }
        sqlite3_finalize(stmt);
    }
    return 0; // Return 0 if flight or seat class not found or an error occurred
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
    return sscanf(search_command, "search_flight %49s %49s %19s %19s", departure_point, destination_point, departure_date, return_date);
}

int parse_search_company(const char *search_command, char *company, char *departure_point, char *destination_point, char *departure_date, char *return_date) {
    return sscanf(search_command, "search_airline %30s %50s %50s %19s %19s", company, departure_point, destination_point, departure_date, return_date);
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
    char date[10];
    strcpy(date, departure_date);
    sprintf(query, "SELECT flight_num, company, departure_point, destination_point, departure_time, return_time, seat_class_A, seat_class_B, price_A, price_B FROM Flights WHERE departure_point='%s' AND destination_point='%s' AND substr(departure_time, 1, 10)='%s' AND substr(return_time, 1, 10)='%s';\n",
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

int search_airline(const char *company, const char *departure_point, const char *destination_point, const char *departure_date, const char *return_date, char *result) {
    sqlite3 *db;
    int rc;

    rc = sqlite3_open(DATABASE_NAME, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    char query[512];
    char date[10];
    strcpy(date, departure_date);
    sprintf(query, "SELECT company, flight_num, departure_point, destination_point, departure_time, return_time, seat_class_A, seat_class_B, price_A, price_B FROM Flights WHERE company = '%s' AND departure_point='%s' AND destination_point='%s' AND substr(departure_time, 1, 10)='%s' AND substr(return_time, 1, 10)='%s';\n",
            company, departure_point, destination_point, departure_date, return_date);

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
                if (login_user(db, username, password) == 1) {
                    if(strcmp(username, "admin") == 0){
                        user_type = 1;
                        send(client_socket, "Login admin success\n", 21, 0);
                    } else {
                        user_type = 2;
                        send(client_socket, "Login success\n", 14, 0);
                    }
                    user_id = get_user_id(db, username);
                    strcpy(user, username);
                    
                    
                } else {
                    send(client_socket, "Login failed\n", 13, 0);
                }
            } else if (strcmp(command, "logout") == 0) {
                if (logout_user()) {
                user_id = -1;
                send(client_socket, "Logout success\n", 15, 0);
                } else {
                send(client_socket, "Logout failed\n", 14, 0);
                }
            } else if (strncmp(buffer, "display", 7) == 0){
                if(user_id != -1 && user_type == 1){
                    char select_query[100];
                    snprintf(select_query, sizeof(select_query), "SELECT * FROM Flights;");

                    sqlite3_stmt* stmt;
                    if (sqlite3_prepare_v2(db, select_query, -1, &stmt, 0) == SQLITE_OK) {
                        char result[10000];
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
                        send(client_socket, result, strlen(result), 0);
                    }
                } else {
                    send(client_socket, "Please login as admin first\n", 29, 0);
                }
            } else if (strncmp(buffer, "delete", 6) == 0){
                if (user_id != -1 && user_type == 1){
                    char flight_num[20];
                    int delay_time;
                    sscanf(buffer, "delete flight %s", flight_num);
                    char ticket_query[100];
                    snprintf(ticket_query, sizeof(ticket_query), "SELECT user_id FROM Tickets WHERE flight_num = '%s';", flight_num);
                    int user_ids[100]; // Define MAX_USERS as the maximum number of users that can book a flight
                    int num_users = get_user_ids_from_query(db, ticket_query, user_ids); // Implement this function to execute the query and retrieve user_ids

                    char noti_query[1000];
                    for (int i = 0; i < num_users; i++) {
                            int temp_user = user_ids[i];
                            snprintf(noti_query, sizeof(noti_query), "INSERT INTO Notifications(user_id, notification) VALUES (%d, 'Flight %s has been cancelled! Sorry for inconvenience. We will make a refund later!');", temp_user, flight_num);
                            execute_query(db, noti_query);

                    }

                    // Step 3: Update delayTime in Flights table
                    char delete_query[100];
                    char result1[1000] = "";
                    char result2[1000] = "";
                    snprintf(delete_query, sizeof(delete_query), "DELETE FROM Flights WHERE flight_num = '%s';", flight_num);
                    if (execute_query(db, delete_query) == SQLITE_OK) {
                        strcat(result1, "Delete from table Flights success\n");
                    } else {
                        strcat(result2, "Delete from table Flights failed\n");
                    }

                    char delete2_query[100];
                    snprintf(delete2_query, sizeof(delete2_query), "DELETE FROM Tickets WHERE flight_num = '%s';", flight_num);
                    if (execute_query(db, delete2_query) == SQLITE_OK) {
                        strcat(result1, "Delete from table Tickets success\n");
                        send(client_socket, result1, strlen(result1), 0);
                    } else {
                        strcat(result2, "Delete from table Tickets failed\n");
                        send(client_socket, result2, strlen(result2), 0);
                    }

                } else {
                    send(client_socket, "Please login as admin first\n", 29, 0);
                }
            } else if (strncmp(buffer, "modify", 6) == 0) {
                if (user_id != -1 && user_type == 1){
                    char flight_num[20], departure_time[20], departure_date[20], return_date[20], return_time[20];
                    sscanf(buffer, "modify %s %s %s %s %s", flight_num, departure_date, departure_time, return_date, return_time);
                    char ticket_query[100];
                    char depart[40] = "";
                    char ret[40] = "";

                    snprintf(ticket_query, sizeof(ticket_query), "SELECT user_id FROM Tickets WHERE flight_num = '%s';", flight_num);
                    int user_ids[100]; // Define MAX_USERS as the maximum number of users that can book a flight
                    int num_users = get_user_ids_from_query(db, ticket_query, user_ids); // Implement this function to execute the query and retrieve user_ids

                    char noti_query[1000];
                    for (int i = 0; i < num_users; i++) {
                            int temp_user = user_ids[i];
                            snprintf(noti_query, sizeof(noti_query), "INSERT INTO Notifications(user_id, notification) VALUES (%d, 'Flight schedule of %s has been changed! Sorry for inconvenience.');", temp_user, flight_num);
                            execute_query(db, noti_query);

                    }
                    char update_query[1000];
                    snprintf(update_query, sizeof(update_query), "UPDATE Flights SET departure_time = datetime('%s %s'), return_time = datetime('%s %s') WHERE flight_num = '%s';", departure_date, departure_time, return_date, return_time, flight_num);

                    if (execute_query(db, update_query) == SQLITE_OK) {
                        send(client_socket, "Change schedule success\n", 25, 0);
                    } else {
                        send(client_socket, "Change schedule failed\n", 24, 0);
                    }

                } else {
                    send(client_socket, "Please login as admin first\n", 29, 0);
                }
            } else if (strncmp(buffer, "delay", 5) == 0){
                if (user_id != -1 && user_type == 1){
                    char flight_num[20];
                    int delay_time;
                    sscanf(buffer, "delay %s %d", flight_num, &delay_time);
                    char ticket_query[100];
                    snprintf(ticket_query, sizeof(ticket_query), "SELECT user_id FROM Tickets WHERE flight_num = '%s';", flight_num);
                    int user_ids[100]; // Define MAX_USERS as the maximum number of users that can book a flight
                    int num_users = get_user_ids_from_query(db, ticket_query, user_ids); // Implement this function to execute the query and retrieve user_ids

                    // Step 3: Update delayTime in Flights table
                    char update_query[100];
                    snprintf(update_query, sizeof(update_query), "UPDATE Flights SET delayTime = %d WHERE flight_num = '%s';", delay_time, flight_num);
                    if (execute_query(db, update_query) == SQLITE_OK) {
                        send(client_socket, "Delay success\n", 14, 0);

                        // Step 4: Insert notifications for each user
                        char noti_query[1000];
                        for (int i = 0; i < num_users; i++) {
                            int temp_user = user_ids[i];
                            snprintf(noti_query, sizeof(noti_query), "INSERT INTO Notifications(user_id, notification) VALUES (%d, 'Flight %s has been delayed %d minutes! Sorry for inconvenience.');", temp_user, flight_num, delay_time);
                            execute_query(db, noti_query);

                        }
                    } else {
                        send(client_socket, "Delay failed\n", 13, 0);
                    }
                }
            } else if (strncmp(buffer, "search_flight", 13) == 0) {
            // Search
            char departure_point[50], destination_point[50], departure_date[20], return_date[20];
                if (parse_search_command(buffer, departure_point, destination_point, departure_date, return_date) == 4) {
                    char search_result[BUFFER_SIZE];
                    search_flights(departure_point, destination_point, departure_date, return_date, search_result);
                    send(client_socket, search_result, strlen(search_result), 0);
                } else {
                    send(client_socket, "Invalid search command", sizeof("Invalid search command"), 0);
                }
            } else if (strncmp(buffer, "search_airline", 14) == 0){
                char departure_point[50], destination_point[50], departure_date[20], return_date[20], company[50];
                if (parse_search_company(buffer, company, departure_point, destination_point, departure_date, return_date) == 5) {
                char search_result[BUFFER_SIZE];
                search_airline(company, departure_point, destination_point, departure_date, return_date, search_result);
                send(client_socket, search_result, strlen(search_result), 0);
                } else {
                    send(client_socket, "Invalid search command", sizeof("Invalid search command"), 0);
                }
            } else if (strncmp(buffer, "book", 4) == 0) {

                char flight_num[20], seat_class[10], ticket_code[1000];
                int num_seat;
                int ticket_price;

                if (user_id != -1 && user_type == 2) {
                    if (sscanf(buffer, "book %s %s %d", flight_num, seat_class, &num_seat) == 3) {
                        if (strcmp(seat_class, "A") == 0 || strcmp(seat_class, "B") == 0){
                        char str[10];
                        if (check_available_seats(db, flight_num, seat_class, num_seat) == 0) {
                            send(client_socket, "Not enough seats\n", 17, 0);
                        } else {
                        ticket_price = num_seat * get_ticket_price(db, flight_num, seat_class);
                        for (int i = 0; i < num_seat; i++) {
                            char str[7]; 
                            ticket_code[0] = '\0';
                            for (int j = 0; j < num_seat; j++) {
                                generate_random_string(str, 6);
                                strcat(ticket_code, str);
                                strcat(ticket_code, "-");
                            }
                        }
                            if (book_ticket(db, ticket_code, user_id, flight_num, seat_class, num_seat, ticket_price) == SQLITE_OK) {
                                char announcement[1000];
                                reduce_available_seats(db, flight_num, seat_class, num_seat);
                                snprintf(announcement, sizeof(announcement), "Booking pending: %s\n", ticket_code);
                                send(client_socket, announcement, sizeof(announcement), 0);

                            } else {
                                send(client_socket, "Booking failed\n", 16, 0);
                            }
                        }
                        } else {
                            send(client_socket, "Invalid seat class\n", 19, 0);
                        }
                    } else {
                        send(client_socket, "Invalid book command", sizeof("Invalid book command"), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                }
            } else if (strncmp(buffer, "pay", 3) == 0) {
                if (user_id != -1 && user_type == 2) {
                    char ticket_code[1000]; 
                    int credit_card, cvv;
                    if (sscanf(buffer, "pay %s %d %d", ticket_code, &credit_card, &cvv) == 3) {
                        int payment_result = makePayment(db, ticket_code, credit_card, cvv, user_id);
                        if (payment_result == 1) {
                            char noti_query[1000];

                            send(client_socket, "Ticket paid\n", 12, 0);
                            snprintf(noti_query, sizeof(noti_query), "INSERT INTO Notifications(user_id, notification) VALUES (%d, 'Booking successfully. Ticket %s has been paid!');", user_id, ticket_code);
                            execute_query(db, noti_query);

                        } else if (payment_result == 0) {
                            send(client_socket, "Failed to pay ticket\n", 22, 0);
                        } else if (payment_result == -1) {
                            send(client_socket, "Ticket not found\n", 17, 0);
                        } else if (payment_result == -2) {
                            send(client_socket, "Wrong credit card information\n", 42, 0);
                        }
                    } else {
                        send(client_socket, "Invalid pay command", sizeof("Invalid pay command"), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                }
            } else if (strncmp(buffer, "view", 4) == 0) {
                // View booked tickets
                if (user_id != -1 && user_type == 2) {
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
                if (user_id != -1 && user_type == 2) {
                    char ticket_code[1000];
                    sqlite3_stmt* stmt;

                    if (sscanf(buffer, "cancel ticket %s", ticket_code) == 1) {
                        char search_query[100], delete_query[100];
                        snprintf(search_query, sizeof(search_query), "SELECT * FROM Tickets WHERE ticket_code = '%s';", ticket_code);
                        if (sqlite3_prepare_v2(db, search_query, -1, &stmt, 0) == SQLITE_OK){
                            if (sqlite3_step(stmt) != SQLITE_ROW){
                                send(client_socket, "Ticket not found\n", 17, 0);
                            } else {
                            snprintf(delete_query, sizeof(delete_query), "DELETE FROM Tickets WHERE ticket_code = '%s';", ticket_code);
                            if (execute_query(db, delete_query) == SQLITE_OK) {
                                increase_available_seats(db, sqlite3_column_text(stmt, 2), sqlite3_column_text(stmt, 3), sqlite3_column_int(stmt, 4));
                                send(client_socket, "Ticket canceled\n", 16, 0);
                            } else {
                                send(client_socket, "Failed to cancel ticket\n", 25, 0);
                            }
                            }
                        }
                    } else {
                        send(client_socket, "Invalid cancel command", sizeof("Invalid cancel command"), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                } 
            } else if (strncmp(buffer, "print", 5) == 0){
                if (user_id != -1 && user_type == 2) {
                    char ticket_code[1000];
                    sqlite3_stmt* stmt;

                    if (sscanf(buffer, "print ticket %s", ticket_code) == 1) {
                    print_ticket_to_file(db, client_socket, ticket_code, "ticket.txt");
                    } else {
                        send(client_socket, "Invalid print command", sizeof("Invalid print command"), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                } 

            } else if (strncmp(buffer, "see", 3) == 0){
                if (user_id != -1 && user_type == 2) {
                    see_notifications(db, user_id, client_socket);
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                }
                
            } else if (strncmp(buffer, "change", 6) == 0) {
                // Change a ticket
                if (user_id != -1 && user_type == 2) {
                    char ticket_code[1000], flight_num[20], new_seat_class[10];
                    char old_seat_class[10];
                    int old_num_seat, ticket_price, result, new_num_seat;
                    sqlite3_stmt* stmt;
                    if (sscanf(buffer, "change %s %s %d", ticket_code, new_seat_class, &new_num_seat) == 3) {
                        char search_query[100], update_query[100];
                        snprintf(search_query, sizeof(search_query), "SELECT * FROM Tickets WHERE ticket_code = '%s';", ticket_code);
                        if (sqlite3_prepare_v2(db, search_query, -1, &stmt, 0) == SQLITE_OK) {
                        int result = sqlite3_step(stmt);
                        if (result != SQLITE_ROW) { 
                            send(client_socket, "Ticket not found\n", 17, 0); 
                        } else {
                            
                        // Get current ticket information
                            strcpy(old_seat_class, sqlite3_column_text(stmt, 3));

                            old_num_seat = sqlite3_column_int(stmt, 4);
                            strcpy(flight_num, sqlite3_column_text(stmt, 2));

                            sqlite3_finalize(stmt);
                            increase_available_seats(db, flight_num, old_seat_class, old_num_seat);
                            int new_ticket_price = new_num_seat * get_ticket_price(db, flight_num, new_seat_class);

                            snprintf(update_query, sizeof(update_query), "UPDATE Tickets SET seat_class = '%s', num_seat = %d, ticket_price = %d WHERE ticket_code = '%s';",
                            new_seat_class, new_num_seat, (new_num_seat * get_ticket_price(db, flight_num, new_seat_class)), ticket_code);
                            if (execute_query(db, update_query) != SQLITE_OK) {
                                send(client_socket, "Change Failed\n", 15, 0); // Error updating ticket information
                            }
                            reduce_available_seats(db, flight_num, new_seat_class, new_num_seat);
                            send(client_socket, "Ticket changed\n", 15, 0);
                        }
                        }
                    } else {
                        send(client_socket, "Invalid change command", sizeof("Invalid change command"), 0);
                    }
                } else {
                    send(client_socket, "Please login first\n", 19, 0);
                } 
            } else if (strncmp(buffer, "exit", 4) == 0) {
                // Exit
                send(client_socket, "Goodbye\n", 8, 0);
                break;
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
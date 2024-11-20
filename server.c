#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_CLIENTS 3
#define BUFFER_SIZE 1024

typedef struct {
    int socket;
    int id;
    struct sockaddr_in address;
} client_t;

client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void send_to_client(int client_socket, const char *message) {
    send(client_socket, message, strlen(message), 0);
}

void send_to_client_by_id(int recipient_id, const char *message, int sender_socket, int sender_id) {
    int found = 0;
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->id == recipient_id) {
            char formatted_message[BUFFER_SIZE];
            snprintf(formatted_message, sizeof(formatted_message), "\nMessage from Client %d: %s\n", sender_id, message);
            send_to_client(clients[i]->socket, formatted_message);
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    if (found) {
        char ack_message[BUFFER_SIZE];
        snprintf(ack_message, sizeof(ack_message), "\nMessage delivered to Client %d.\n", recipient_id);
        send_to_client(sender_socket, ack_message);
    } 
    
    else {
        char error_message[BUFFER_SIZE];
        snprintf(error_message, sizeof(error_message), "Client ID %d not found.\n", recipient_id);
        send_to_client(sender_socket, error_message);
    }
}

void *handle_client(void *arg) {
    client_t *client = (client_t *)arg;
    char buffer[BUFFER_SIZE];
    int bytes_read;

    printf("Server: Client %d connected.\n", client->id);

    char welcome_message[BUFFER_SIZE];
    snprintf(welcome_message, sizeof(welcome_message), "Welcome! You are Client ID %d\n", client->id);
    send_to_client(client->socket, welcome_message);

    while ((bytes_read = recv(client->socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';

        if (strncmp(buffer, "exit", 4) == 0) {
            printf("Server: Client %d disconnected.\n", client->id);
            pthread_mutex_lock(&clients_mutex);
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i] && clients[i]->id == client->id) {
                    free(clients[i]);
                    clients[i] = NULL;
                    break;
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            close(client->socket);
            pthread_exit(NULL);
        }

        int recipient_id;
        char message[BUFFER_SIZE];
        if (sscanf(buffer, "%d %[^\n]", &recipient_id, message) == 2) {
            printf("Server: Message from Client %d to Client %d: %s\n", client->id, recipient_id, message);
            send_to_client_by_id(recipient_id, message, client->socket, client->id);
        } 
        
        else {
            send_to_client(client->socket, "Invalid message format. Use: <RecipientID> <Message>\n");
        }
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->id == client->id) {
            free(clients[i]);
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    close(client->socket);
    free(client);
    pthread_exit(NULL);
}

int main(){
    int server_socket, new_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len=sizeof(client_addr);
 
    server_socket=socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket==0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=INADDR_ANY;
    server_addr.sin_port=htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server: Listening on port %d...\n", PORT);

    int client_id=1;

    while (1){
        new_socket=accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (new_socket < 0) {
            perror("Accept failed");
            continue;
        }

        pthread_mutex_lock(&clients_mutex);
        int added=0;
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i]) {
                client_t *client=malloc(sizeof(client_t));
                client->socket=new_socket;
                client->id=client_id++;
                client->address=client_addr;
                clients[i]=client;

                pthread_t tid;
                pthread_create(&tid, NULL, handle_client, client);
                pthread_detach(tid);

                added=1;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (!added) {
            printf("Server: Max clients reached. Connection rejected.\n");
            close(new_socket);
        }
    }

    close(server_socket);
    return 0;
}

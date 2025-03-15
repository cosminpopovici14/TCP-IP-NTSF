#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define BUFFER_SIZE 256


int sd;
pthread_mutex_t lock;

void *receive_messages(void *arg) {
    char buffer[BUFFER_SIZE];

    while (1) {
        bzero(buffer, BUFFER_SIZE);
        int n = read(sd, buffer, BUFFER_SIZE - 1);

        if (n <= 0) {
            perror("[client] Eroare la citirea de la server sau serverul s-a deconectat.");
            close(sd);
            exit(EXIT_FAILURE);
        }

        buffer[n] = '\0';
        printf("[client] Mesaj de la server: %s\n", buffer);


    }

    return NULL;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in server;
    pthread_t recv_thread;
    char buffer[BUFFER_SIZE];

    if (argc != 3) {
        printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[2]);

    
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Eroare la socket().");
        exit(EXIT_FAILURE);
    }

    
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    
    if (connect(sd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("Eroare la connect().");
        close(sd);
        exit(EXIT_FAILURE);
    }

    printf("[client] Conectat la serverul %s:%d\n", argv[1], port);

    
    pthread_mutex_init(&lock, NULL);

    
    if (pthread_create(&recv_thread, NULL, receive_messages, NULL) != 0) {
        perror("Eroare la crearea thread-ului de primire.");
        close(sd);
        exit(EXIT_FAILURE);
    }

    
    while (1) {
        

        printf("[client] Introduceti comanda: ");
        bzero(buffer, BUFFER_SIZE);
        fgets(buffer, BUFFER_SIZE - 1, stdin);

        buffer[strcspn(buffer, "\n")] = '\0'; 

        if (strlen(buffer) == 0) {
            continue;
        }

        if (write(sd, buffer, strlen(buffer) + 1) <= 0) {
            perror("Eroare la trimiterea mesajului la server.");
            break;
        }

        
        if (strcmp(buffer, "quit") == 0) {
            break;
        }
    }

    
    pthread_cancel(recv_thread);
    pthread_join(recv_thread, NULL);
    pthread_mutex_destroy(&lock);
    close(sd);

    return 0;
}

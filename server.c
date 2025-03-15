#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>

#define PORT 2024
#define MAX_PLAYERS 4
#define BOARD_SIZE 40

typedef struct {
    int client_fd;
    int player_id;
    int pioni[4];
    int poz_start;
    int poz_finala;
} Player;

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t turn_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t board_mutex = PTHREAD_MUTEX_INITIALIZER;

int players[MAX_PLAYERS];
int num_players = 0;
int current_turn = 0;
bool game_started = false;
int logged_in[MAX_PLAYERS] = {0};
int board[BOARD_SIZE];
bool can_move = false;

void send_message(int client_fd, const char *message) {
    if (write(client_fd, message, strlen(message) + 1) <= 0) {
        perror("[server] Eroare la write().");
    }
}

void initialize_board() {
    pthread_mutex_lock(&board_mutex);
    for (int i = 0; i < BOARD_SIZE; i++) {
        if (i % 10 == 0 || i % 10 == 4) {
            board[i] = 9;
        } else {
            board[i] = -1;
        }
    }
    pthread_mutex_unlock(&board_mutex);
}

int check_winner(Player *player) {
    for (int i = 0; i < 4; i++) {
        if (player->pioni[i] != player->poz_finala + 4) {
            return 0;
        }
    }
    return 1;
}

void print_board() {
    pthread_mutex_lock(&board_mutex);
    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%d ", board[i]);
    }
    printf("\n");
    pthread_mutex_unlock(&board_mutex);
}

void update_board(Player *player, int pion, int pozitie) {
    pthread_mutex_lock(&board_mutex);
    if (board[pozitie] != -1 && board[pozitie] != 9 && board[pozitie] != player->player_id) {
        printf("[server] Pionul jucatorului %d a fost scos de pe pozitia %d.\n", board[pozitie], pozitie);
    }

    board[pozitie] = player->player_id;
    player->pioni[pion] = pozitie;
    print_board();

    pthread_mutex_unlock(&board_mutex);
}

void initialize_players(Player *player) {
    for (int i = 0; i < 4; i++) {
        player->pioni[i] = -1;
    }
    player->poz_start = player->player_id * 10;
    player->poz_finala = (player->player_id == 0) ? 39 : player->poz_start - 1;
}

bool can_move_pion(Player *player, int pion, int zar) {
    pthread_mutex_lock(&board_mutex);
    if (player->pioni[pion] == -1 && zar == 6) {
        pthread_mutex_unlock(&board_mutex);
        return true;
    }
    if (player->pioni[pion] != -1) {
        int new_position = player->pioni[pion] + zar;
        if (player->player_id == 0) {
            if (new_position > player->poz_finala + 4) {
                pthread_mutex_unlock(&board_mutex);
                return false;
            }
        } else if (new_position > player->poz_finala + 4 && new_position < player->poz_start) {
            pthread_mutex_unlock(&board_mutex);
            return false;
        }
        pthread_mutex_unlock(&board_mutex);
        return true;
    }
    pthread_mutex_unlock(&board_mutex);
    return false;
}

void *client_thread(void *arg) {
    Player *player = (Player *)arg;
    int client_fd = player->client_fd;
    char response[256];
    int zar;

    initialize_players(player);

    char buffer[256];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        if (read(client_fd, buffer, sizeof(buffer) - 1) <= 0) {
            perror("[server] Client deconectat.");
            break;
        }

        buffer[strcspn(buffer, "\n")] = 0;
        printf("[server] Jucator %d a trimis: %s\n", player->player_id, buffer);

        if (strcmp(buffer, "quit") == 0) {
            send_message(client_fd, "Ati iesit din joc.");
            break;
        }

        if (!game_started) {
            if (strcmp(buffer, "login") == 0) {
                send_message(client_fd, "Va rugam sa va conectati folosind comanda 'login : <nume>'.");
            } else if (strncmp(buffer, "login : ", 8) == 0) {
                pthread_mutex_lock(&game_mutex);
                if (logged_in[player->player_id]) {
                    send_message(client_fd, "Sunteti deja conectat!");
                } else {
                    send_message(client_fd, "V-ati conectat cu succes!");
                    logged_in[player->player_id] = 1;
                }
                pthread_mutex_unlock(&game_mutex);
            } else if (strcmp(buffer, "start") == 0) {
                pthread_mutex_lock(&game_mutex);
                if (!game_started && num_players > 1) {
                    game_started = true;
                    pthread_cond_broadcast(&turn_cond);
                    send_message(client_fd, "Jocul incepe!");
                } else {
                    send_message(client_fd, "Jocul nu poate incepe! Verificati conditiile.");
                }
                pthread_mutex_unlock(&game_mutex);
            } else {
                send_message(client_fd, "Comanda necunoscuta sau jocul nu a inceput.");
            }
        } else {
            pthread_mutex_lock(&game_mutex);
            if (player->player_id == current_turn) {
                if (strcmp(buffer, "zar") == 0) {
                    zar = rand() % 6 + 1;
                    snprintf(response, sizeof(response), "Ai dat cu zarul: %d", zar);
                    send_message(client_fd, response);

                    can_move = false;
                    for (int i = 0; i < 4; i++) {
                        if (can_move_pion(player, i, zar)) {
                            can_move = true;
                            snprintf(response, sizeof(response), "Pionul %d poate fi mutat cu %d", i, zar);
                            send_message(client_fd, response);
                        }
                    }

                    if (!can_move) {
                        send_message(client_fd, "Nu se poate muta niciun pion! Tura trece la urmatorul jucator.");
                        current_turn = (current_turn + 1) % num_players;
                        pthread_cond_broadcast(&turn_cond);
                    } else {
                        send_message(client_fd, "Alege pionul pe care doresti sa-l muti (0-3):");
                        pthread_cond_broadcast(&turn_cond);
                    }
                } else if (strncmp(buffer, "mutare: ", 8) == 0) {
                    int pion = atoi(buffer + 8);
                    if (pion >= 0 && pion <= 3 && can_move_pion(player, pion, zar)) {
                        int new_position = (player->pioni[pion] == -1) ? player->poz_start : player->pioni[pion] + zar;
                        update_board(player, pion, new_position);
                        print_board();

                        snprintf(response, sizeof(response), "Ai mutat pionul %d pe pozitia %d", pion, new_position);
                        send_message(client_fd, response);

                        current_turn = (current_turn + 1) % num_players;
                        pthread_cond_broadcast(&turn_cond);

                        
                    } else {
                        send_message(client_fd, "Pionul nu poate fi mutat!");
                        current_turn = (current_turn + 1) % num_players;
                        pthread_cond_broadcast(&turn_cond);
                        
                    }
                } else {
                    send_message(client_fd, "Comanda necunoscuta.");
                }
                if (check_winner(player)) {
                            send_message(client_fd, "Felicitari! Ati castigat jocul!");
                            game_started = false;
                }
            } else {
                send_message(client_fd, "Nu este tura ta. Asteapta.");
            }
            pthread_mutex_unlock(&game_mutex);
        }
    }

    close(client_fd);
    free(player);
    return NULL;
}

int main() {
    struct sockaddr_in server, client;
    int server_fd;

    srand(time(NULL));
    initialize_board();

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("[server] Eroare la socket().");
        exit(EXIT_FAILURE);
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("[server] Eroare la bind().");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PLAYERS) == -1) {
        perror("[server] Eroare la listen().");
        exit(EXIT_FAILURE);
    }

    printf("[server] Serverul asteapta conexiuni la portul %d...\n", PORT);

    while (true) {
        socklen_t client_len = sizeof(client);
        int client_fd = accept(server_fd, (struct sockaddr *)&client, &client_len);

        if (client_fd < 0) {
            perror("[server] Eroare la accept().");
            continue;
        }

        pthread_mutex_lock(&game_mutex);
        if (num_players >= MAX_PLAYERS) {
            send_message(client_fd, "Jocul este plin.");
            close(client_fd);
            pthread_mutex_unlock(&game_mutex);
            continue;
        }

        Player *player = malloc(sizeof(Player));
        player->client_fd = client_fd;
        player->player_id = num_players;
        players[num_players++] = client_fd;

        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, player);
        pthread_detach(tid);

        printf("[server] Jucator %d s-a conectat.\n", player->player_id);
        pthread_mutex_unlock(&game_mutex);

        if (num_players > 1) {
            printf("[server] Jocul poate incepe.\n");
        }
    }

    close(server_fd);
    return 0;
}

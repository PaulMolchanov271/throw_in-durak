#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>


#ifndef INBUFSIZE
#define INBUFSIZE 4096
#endif

#ifndef LISTEN_QLEN
#define LISTEN_QLEN 32
#endif

#ifndef INIT_SESS_ARR_SIZE
#define INIT_SESS_ARR_SIZE 32
#endif

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 8
#endif

#ifndef MAX_CARDS
#define MAX_CARDS 52
#endif

#ifndef INIT_GAMES_ARR_SIZE
#define INIT_GAMES_ARR_SIZE 10
#endif

enum { port = 5555, buffer_size = 256};

enum fsm_states { fsm_start, fsm_signup_username, fsm_signup_password, fsm_login_username, fsm_login_password, fsm_interaction, fsm_game, fsm_finish, fsm_error };

struct player_str {
    int id;
    int *hand;
    int game_id;
    int card_count;
};

struct client_session {
    int sd;                            /* socket descriptor */
    char buf[buffer_size];             /* buffer */
    int bd;                            /* amount of data in the buffer */
    char *username, *password;         /* client's username and password */
    unsigned long from_ip;
    unsigned short from_port;
    int wins, losses;
    struct player_str *player_info;
    enum fsm_states state;
};

struct game_session {
    int id;
    char *name;
    int player_count;
    struct client_session **players;
    struct client_session *defending_player, *attacking_player;
    bool is_started;
    int deck[MAX_CARDS];
    int deck_size;
    int trump_card;
    int table_card_count;
    int table_cards[MAX_CARDS];
    int card_queue[25];
    int queue_len;
};

struct server_str {
    int ls;
    FILE *file_account;
    struct client_session **sess_array;
    struct game_session **games_array;
    int sess_array_size, games_array_size;
};

static void session_send_string(int sd, const char* str);
void shuffle_deck(int *deck, int size);
static void session_commit(struct client_session* sess, FILE* f);

char* center_text(const char* text, int width) {
    int text_length = strlen(text);
    
    if (text_length >= width) {
        return strdup(text);  
    }
    
    char* buffer = malloc(width + 1);  
    if (!buffer) {
        return NULL; 
    }
    int padding = (width - text_length) / 2;
    
    snprintf(buffer, width + 1, "%*s%s%*s", padding, "", text, width - padding - text_length, "");
    
    return buffer;
}

struct client_session *make_new_session(int sd, struct sockaddr_in* from) {
    struct client_session* sess = malloc(sizeof(*sess));
    sess->sd = sd;
    sess->bd = 0;
    sess->state = fsm_start;
    sess->from_ip = ntohl(from->sin_addr.s_addr);
    sess->from_port = ntohs(from->sin_port);
    sess->player_info = malloc(sizeof(struct player_str));
    sess->player_info->game_id = -1;
    session_send_string(sess->sd, "If you want to sign up enter \"1\", if you want log in enter \"2\", if you want log in as a guest enter \"3\".\n");
    return sess;
}

static void session_send_string(int sd, const char* str)
{
    int length = strlen(str);
    if (str == NULL) {
        fprintf(stderr, "Error: NULL pointer passed to session_send_string.\n");
        return;
    }
    if (sd <= 0) {
        fprintf(stderr, "Error: Invalid session descriptor.\n");
        _exit(EXIT_FAILURE);
    }
    send(sd, str, length, 0);
    return;
}

int authenticate_user(FILE* file, const char* username, const char* password) {
    char line[256];
    char file_username[50], file_password[50];
    unsigned int ip[4];
    int port;
    memset(file_username, 0, sizeof(file_username));
    memset(file_password, 0, sizeof(file_password));

    fseek(file, 0, SEEK_SET);

    while (fgets(line, sizeof(line), file) != NULL) {
        int res = sscanf(line, "From %u.%u.%u.%u:%d; Username: %49[^;]; Password: %49[^\n]",
            &ip[0], &ip[1], &ip[2], &ip[3], &port,
            file_username, file_password);

        file_username[49] = '\0';
        file_password[49] = '\0';

        if (res == 7) {
            if (strcmp(username, file_username) == 0 && ((password == NULL) || (strcmp(password, file_password) != 0))) {
                return 1;
            }
            if ((strcmp(username, file_username) == 0) && (strcmp(password, file_password) == 0)) {
                return 0;
            }
        }
    }
    return 2;
}

static void execute_list(struct server_str* serv, struct client_session *sess)
{
    int i = 0, is_game_exist = 0;
    char message[255];
    while(i < serv->games_array_size) {
        if(serv->games_array[i] != NULL) {
            is_game_exist = 1;
            break;
        }
        i++;
    }
    i = 0;
    if(!is_game_exist) {
        session_send_string(sess->sd, "No games have been created yet\n");
        return;
    } else {
        session_send_string(sess->sd, "GAME ID   |     GAME NAME     | PLAYER COUNT | IS STARTED | \n");
        session_send_string(sess->sd, "----------|-------------------|--------------|------------|\n");
        for(i = 0; i < serv->games_array_size; i++) {
            if(serv->games_array[i] == NULL) {
                continue;
            }
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "%d", serv->games_array[i]->id);

            const char *name = serv->games_array[i]->name;
            char player_count_str[16];
            snprintf(player_count_str, sizeof(player_count_str), "%d", serv->games_array[i]->player_count);

            const char *is_started_str = serv->games_array[i]->is_started ? "yes" : "no";
            char* centered_id = center_text(id_str, 8);
            char* centered_name = center_text(name, 17);
            char* centered_player_count = center_text(player_count_str, 12);
            char* centered_is_started = center_text(is_started_str, 10);

            snprintf(message, sizeof(message),
                    " %-8s | %-17s | %-12s | %-10s |\n",
                    centered_id, centered_name, centered_player_count, centered_is_started);

            free(centered_id);
            free(centered_name);
            free(centered_player_count);
            free(centered_is_started);
            session_send_string(sess->sd, message);
        }
    }
    return;
}

void game_init(struct game_session *game, int id)
{
    int i;
    game->players = malloc(MAX_PLAYERS * sizeof(*game->players));
    for(i = 0; i < MAX_PLAYERS; i++) {
        game->players[i] = NULL;
    }
    game->player_count = 0;
    game->is_started = false;

    game->deck_size = MAX_CARDS;
    for (i = 0; i < MAX_CARDS; ++i)
        game->deck[i] = i;
    shuffle_deck(game->deck, MAX_CARDS);
    game->trump_card = game->deck[game->deck_size-1];
    game->deck_size--;
    game->id = id;
    game->defending_player = NULL;
    game->attacking_player = NULL;
}

struct game_session *execute_create_game(struct server_str *serv, struct client_session *sess, char *game_name, int id)
{
    struct game_session *game = malloc(sizeof(*game));
    int i = 0;
    game_init(game, id);
    while((game_name[i] != ' ') && (game_name[i] != '\0') && (game_name[i] != '\n')) {
        i++;
    }
    game_name[i] = '\0';
    game->name = strdup(game_name);
    game->player_count += 1;
    game->players[0] = sess;
    game->players[0]->player_info->hand = NULL;
    game->players[0]->player_info->card_count = 0;
    game->players[0]->player_info->id = 0;
    return game;
}

static int execute_join_game(struct server_str *serv, struct client_session *sess, int game_id)
{
    int i = 0;
    if(serv->games_array[game_id] == NULL) {
        session_send_string(sess->sd, "There is no such game\n");
        return 1;
    }    
    if(serv->games_array[game_id]->player_count == MAX_PLAYERS) {
        session_send_string(sess->sd, "This game is full\n");
        return 1;
    }
    session_send_string(sess->sd, "You have joined the game, press \"Enter\" to start the game if at least two players have joined the game\n"); 
    sess->player_info->game_id = game_id;
    while((serv->games_array[game_id]->players[i] != NULL) && (i < MAX_PLAYERS)) {
        i++;
    }
    serv->games_array[game_id]->players[i] = sess;
    serv->games_array[game_id]->players[i]->player_info->hand = NULL;
    serv->games_array[game_id]->players[i]->player_info->id = i;
    serv->games_array[game_id]->player_count += 1; 
    return 0;
}

static int command_check(struct server_str* serv, struct client_session *sess, char *command)
{
    int i;
    char *ptr;
    if(strncmp(command, "#list", 5) == 0) {
        execute_list(serv, sess);
        return 0;
    }
    if(strncmp(command, "#create", 7) == 0) {
        ptr = command + 8;
        i = 0;
        while(serv->games_array[i] != NULL) {
            i++;
        }
        if(i >= serv->games_array_size) {
            int newlen = serv->games_array_size;
            while (i >= newlen)
                newlen += INIT_GAMES_ARR_SIZE;
            serv->sess_array =
                realloc(serv->games_array, newlen * sizeof(struct session*));
            for (i = serv->games_array_size; i < newlen; i++)
                serv->games_array[i] = NULL;
            serv->games_array_size = newlen;
        }
        serv->games_array[i] = execute_create_game(serv, sess, ptr, i);
        sess->player_info->game_id = serv->games_array[i]->id;
        sess->state = fsm_game;
        session_send_string(sess->sd, "To start the game, press \"Enter\" if at least two players have joined the game\n");
        return 0;
    }
    if(strncmp(command, "#join", 5) == 0) {
        int game_id = 0;
        ptr = command + 6;
        i = 0;
        while((ptr[i] != ' ') && (ptr[i] != '\0')) {
            game_id = game_id*10 + (ptr[i]-48);
            i++;
        }
        if(serv->games_array[game_id]->is_started == true) {
            session_send_string(sess->sd, "The game has already started, you can't connect to it.\n");
            return 0;
        }
        int res = execute_join_game(serv, sess, game_id);
        if(res == 0) {
            sess->state = fsm_game;
        }
        return 0;
    }
    return 1;
}

void shuffle_deck(int *deck, int size) {
    srand(time(NULL));
    for (int i = size - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        int temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
}

void format_card(int card, char *output) {
    const char *ranks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    const char *suits = "^v%#";
    int rank = card % 13;
    int suit = card / 13;
    snprintf(output, 6, "%s%c", ranks[rank], suits[suit]);
}

char *what_cards_can_be_placed(struct client_session *sess, struct game_session *game, const char *player) {
    int i = 0;
    int cards_len = 0;
    int max_cards = sess->player_info->card_count;

    // Allocate sufficient memory for the passed cards (up to the player's hand size + null terminator).
    char *passed_cards = malloc((max_cards + 1) * sizeof(char));
    if (!passed_cards) {
        return NULL; // Handle memory allocation failure.
    }

    // If no cards are on the table, all cards can be played.
    if (game->table_card_count == 0) {
        for (i = 0; i < max_cards; i++) {
            passed_cards[cards_len++] = (i < 26) ? ('a' + i) : ('A' + i - 26);
        }
        passed_cards[cards_len] = '\0';
        return passed_cards;
    }

    if (strcmp(player, "defense") == 0) {
        for (i = 0; i < max_cards; i++) {
            int card = sess->player_info->hand[i];
            int card_rank = card % 13;
            int card_suit = card / 13;

            int card_to_beat = game->table_cards[game->table_card_count - 1];
            int beat_rank = card_to_beat % 13;
            int beat_suit = card_to_beat / 13;

            int trump_suit = game->trump_card / 13;

            if ((card_suit == beat_suit && card_rank > beat_rank) || (card_suit == trump_suit && card_suit != beat_suit)) {
                passed_cards[cards_len++] = (i < 26) ? ('a' + i) : ('A' + i - 26);
            }
        }
    } else if (strcmp(player, "attack") == 0) {
        for (i = 0; i < max_cards; i++) {
            int card_rank = sess->player_info->hand[i] % 13;

            // Check if the card rank matches any rank on the table.
            for (int j = 0; j < game->table_card_count; j++) {
                if (card_rank == game->table_cards[j] % 13) {
                    passed_cards[cards_len++] = (i < 26) ? ('a' + i) : ('A' + i - 26);
                    break;
                }
            }
        }
    }

    passed_cards[cards_len] = '\0'; // Null-terminate the string.
    return passed_cards;
}

void send_table(struct server_str *serv, struct game_session *game, struct client_session* sess) {
    char buffer[1024], *cards = NULL; 
    int offset, i, j;

    for (i = 0; i < game->player_count; i++) {
        session_send_string(game->players[i]->sd, "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
        offset = 0;
        for (j = 0; j < game->player_count; j++) {
            if(game->players[i]->sd == game->players[j]->sd) {
                continue;
            }
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, " <%d>  ", game->players[j]->player_info->card_count);
        }

        char trump_card[4];
        format_card(game->trump_card, trump_card); 
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "[%d] %s\n", game->deck_size, trump_card);

        for (j = 0; j < game->table_card_count; j++) {
            char card_str[4];
            format_card(game->table_cards[j], card_str);
            if (j % 2 == 0) {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset, "       %s \\ ", card_str);
            } else {
                offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s\n", card_str);
            }
        }
        if (game->table_card_count % 2 != 0) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");
        }

        for (j = 0; j < game->players[i]->player_info->card_count; j++) {
            char card_str[4];
            format_card(game->players[i]->player_info->hand[j], card_str);
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%c: %s ", 'a' + j, card_str);
        }
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");

        if ((game->players[i]->sd == game->defending_player->sd) && ((game->table_card_count % 2) != 0) && (game->defending_player->player_info->card_count > 0)) {
            cards = what_cards_can_be_placed(game->players[i], game, "defense");
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s => ", cards);
        } else if((game->table_card_count == 0) && (game->players[i] == game->attacking_player)){
            cards = what_cards_can_be_placed(game->players[i], game, "attack");
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s > ", cards);
        } else if((game->table_card_count > 0) && (game->players[i] != game->defending_player)) {            
            cards = what_cards_can_be_placed(game->players[i], game, "attack");
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s > ", cards);
        } 
        if(cards != NULL) {
            free(cards);
            cards = NULL;
        }
        if (write(game->players[i]->sd, buffer, offset) < 0) {
            perror("Failed to send table state");
        }
    }
}

void place_card(struct server_str* serv, struct game_session *game, char c, struct client_session* sess)
{
    int played_card = -1, i = 0;
    if((c >= 97) && (c <= 122)) {
         played_card = c - 97;
    } else if((c >= 65) && (c <= 90)) {
         played_card = c - 39;
    }
    if((played_card == -1) || (played_card > sess->player_info->card_count)) {
        session_send_string(sess->sd, "Invalid command. Please play a valid card.\n");
        return;
    }
    if(sess->sd == game->defending_player->sd) {
        int rank = game->defending_player->player_info->hand[played_card] % 13;
        int suit = game->defending_player->player_info->hand[played_card] / 13;
        int card_to_beat_rank = game->table_cards[game->table_card_count-1] % 13;
        int card_to_beat_suit = game->table_cards[game->table_card_count-1] / 13;
        int trump_card_suit = game->trump_card / 13;
        if(((card_to_beat_suit == suit) && (card_to_beat_rank < rank)) || ((card_to_beat_suit != suit) && (suit == trump_card_suit))) {
            game->table_cards[game->table_card_count] = game->defending_player->player_info->hand[played_card];
            game->table_card_count++;
            game->table_cards[game->table_card_count] = -1;
            size_t card_size = sizeof(game->defending_player->player_info->hand[0]);
            size_t count = game->defending_player->player_info->card_count - played_card - 1;
            memmove(game->defending_player->player_info->hand + played_card,
                    game->defending_player->player_info->hand + played_card + 1,
                    count * card_size);
            game->defending_player->player_info->card_count--;
        } else {
            session_send_string(sess->sd, "You can't beat with this card.\n");
        }
    } else {
        int rank = sess->player_info->hand[played_card] % 13;
        int rank_table_cards = game->table_cards[0] % 13;
        while(i < (game->table_card_count+1)) {
            if((rank == rank_table_cards) || (game->table_card_count == 0)) {
                game->card_queue[game->queue_len] = sess->player_info->hand[played_card];
                game->queue_len++;
                game->card_queue[game->queue_len] = -1;
                size_t card_size = sizeof(sess->player_info->hand[0]);
                size_t count = sess->player_info->card_count - played_card - 1;
                memmove(sess->player_info->hand + played_card, sess->player_info->hand + played_card + 1,
                        count * card_size);
                sess->player_info->card_count--;
                break;
            }
            i++;
            rank_table_cards = game->table_cards[i] % 13;
        }
        if(i == game->table_card_count) {
            session_send_string(sess->sd, "You can't place this card on table\n");
        }
    }
}

void distribute_cards(struct player_str *player, int *deck, int *deck_size) {
    if (player->hand == NULL) {
        player->hand = calloc(6, sizeof(int));
        if (!player->hand) {
            fprintf(stderr, "Memory allocation failed for player hand.\n");
            return;
        }
    }
    while (player->card_count < 6 && *deck_size > 0) {
        player->hand[player->card_count] = deck[(*deck_size) - 1];
        (*deck_size)--;
        player->card_count++;
    }
}

void deal_cards(struct game_session* game)
{
    int n = 0;
    distribute_cards(game->attacking_player->player_info, game->deck, &game->deck_size);
    distribute_cards(game->defending_player->player_info, game->deck, &game->deck_size);

    for (int j = 0; j < game->player_count+n; j++) {
        if (game->players[j] == NULL) {
            n++;
            continue;
        }
        distribute_cards(game->players[j]->player_info, game->deck, &game->deck_size);
    }
}


void end_game(struct game_session *game, struct client_session* sess)
{
    sess->wins++;
    game->is_started = false;
    for (int i = 0; i < game->player_count; i++) {
        game->players[i]->state = fsm_interaction;
        if (game->players[i]) {
            if (game->players[i]->sd == sess->sd) {
                session_send_string(game->players[i]->sd, "GAME OVER! YOU WIN!\n");
            } else {
                session_send_string(game->players[i]->sd, "GAME OVER! YOU LOSE!\n");
            }
        }
    }
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i] && game->players[i]->player_info->hand) {
            free(game->players[i]->player_info->hand);
            free(game->players[i]->player_info);
            game->players[i]->player_info = NULL;
        }
    }
}

void update_defending_player(struct game_session* game) {
    int defending_player_index = 0;
    while(game->players[defending_player_index] != game->defending_player) {
        defending_player_index++;
    }
    defending_player_index = (defending_player_index + 1) % game->player_count;
    game->defending_player = game->players[defending_player_index];
}

void update_attacking_player(struct game_session *game)
{
    int attacking_player_index = 0;
    while(game->players[attacking_player_index] != game->attacking_player) {
        attacking_player_index++;
    }
    attacking_player_index = (attacking_player_index + 1) % game->player_count;
    if(game->players[attacking_player_index] == game->defending_player) {
        attacking_player_index = (attacking_player_index + 1) % game->player_count;
    }
    game->attacking_player = game->players[attacking_player_index];
}

void clear_table(struct game_session *game)
{
    int i = 0;
    while(i > game->table_card_count) {
        game->table_cards[i] = -1;
        i++;
    }
    game->table_card_count = 0;
}

void take_table_cards(struct game_session *game)
{
    int i = 0;
    if (!game || !game->defending_player || !game->defending_player->player_info) {
        fprintf(stderr, "Invalid game or player state.\n");
        return;
    }

    struct player_str *defender = game->defending_player->player_info;
    int new_card_count = defender->card_count + game->table_card_count;

    int *new_hand = malloc(new_card_count * sizeof(int));
    if (!new_hand) {
        fprintf(stderr, "Failed to allocate memory for defender's hand.\n");
        return;
    }
    for(i = 0; i < defender->card_count; i++) {
        new_hand[i] = defender->hand[i];
    }
    free(defender->hand);
    defender->hand = NULL;
    defender->hand = new_hand;

    for (int i = 0; i < game->table_card_count; i++) {
        defender->hand[defender->card_count + i] = game->table_cards[i];
        game->table_cards[i] = -1;
    }

    defender->card_count = new_card_count;
    game->table_card_count = 0;
}

static void place_all_queue(struct game_session *game)
{
    while(game->queue_len > 0) {
        game->table_cards[game->table_card_count] = game->card_queue[game->queue_len-1];
        game->table_card_count++;
        game->table_cards[game->table_card_count] = -1;
        game->card_queue[game->queue_len] = -1;
        game->queue_len--;
    }
    return;
}

static void session_game(struct server_str* serv, struct game_session *game, struct client_session* sess, char *line)
{ 
    int next_id_player = -1;
    int n = 0, i = 0;
    if((game->table_card_count == 0) && (sess != game->attacking_player)) {
        send_table(serv, game, sess);
        return;
    }
    while(i < game->player_count+n) {
        if(game->players[i] == game->defending_player) {
            printf("id: %d\n", i);
        }
        if(game->players[i] != NULL && game->players[i]->player_info->card_count == 0) {
            end_game(game, game->players[i]);
            return;
        }
        if(game->players[i] == NULL) {
            n++;
        }
        i++;
    }
    next_id_player = (game->attacking_player->player_info->id + 1) % game->player_count;
    send_table(serv, game, sess);
    char command = line[0];
    if (((game->queue_len < game->defending_player->player_info->card_count - 1) || (sess == game->defending_player)) && (((command >= 'a') && (command <= 'z')) || ((command >= 'A') && (command <= 'Z')))) { 
        if(command != '\0') {
            place_card(serv, game, command, sess);
        }
    } else if((command == '\0') && ((game->table_card_count % 2) == 0) && (game->table_card_count > 0) 
              && (sess == game->attacking_player) && (game->players[next_id_player] == game->defending_player)) {
        deal_cards(game);
        update_defending_player(game);
        update_attacking_player(game);
        clear_table(game);
    } else if((command == '\0') && ((game->table_card_count % 2) != 0) 
              && (game->table_card_count > 0) && (sess == game->defending_player)) {
        if(game->queue_len > 0) {
            place_all_queue(game);
        }
        take_table_cards(game);
        deal_cards(game);
        update_defending_player(game);
        update_defending_player(game);
        update_attacking_player(game);
        update_attacking_player(game);
    } else {
        session_send_string(sess->sd, "Invalid command. Please play a valid card.\n");
    }
    if((game->queue_len > 0) && ((game->table_card_count % 2) == 0)) {
        game->table_cards[game->table_card_count] = game->card_queue[0];
        game->table_card_count++;
        game->table_cards[game->table_card_count] = -1;
        memmove(&game->card_queue[0], &game->card_queue[1], (game->queue_len-1) * sizeof(int));
        game->queue_len--;
    }
    send_table(serv, game, sess);
    return;
    
}

static void session_fsm_step(struct server_str* serv, struct client_session* sess, char* line, FILE* f)
{
    int res, i;
    char message[256];
    switch (sess->state) {
    case fsm_start:
        if (line) {
            if (strcmp(line, "1") == 0) {
                session_send_string(sess->sd, "Please, enter your username?\n");
                sess->state = fsm_signup_username;
            }
            else if (strcmp(line, "2") == 0) {
                session_send_string(sess->sd, "Please, enter your username?\n");
                sess->state = fsm_login_username;
            }
            else if (strcmp(line, "3") == 0) {
                session_send_string(sess->sd, "You are logged in as a guest.\n");
                sess->state = fsm_interaction;
            }
            free(line);
        }
        else {
            session_send_string(sess->sd, "If you want to sign up enter \"1\", or if you want log in as a guest enter \"2\".\n");
        }
        break;
    case fsm_login_username:
        if (line) {
            res = authenticate_user(f, line, NULL);
            if (res == 1) {
                session_send_string(sess->sd, "User with this username already exist. Please enter another username:\n");
                break;
            }
            else {
                sess->username = line;
            }
        }
        else {
            session_send_string(sess->sd, "Please, enter your username?\n");
            break;
        }
        session_send_string(sess->sd, "Please enter your pasword:\n");
        sess->state = fsm_login_password;
        break;
    case fsm_login_password:
        if (line) {
            sess->password = line;
        }
        else {
            session_send_string(sess->sd, "Please enter your pasword:\n");
            break;
        }
        session_commit(sess, f);
        session_send_string(sess->sd, "You have been successfully logged into the system.\n");
        session_send_string(sess->sd, "If you want to see list of games enter then \"#list\"\n");
        session_send_string(sess->sd, "If you want to check your profile then enter \"#profile\"\n");
        session_send_string(sess->sd, "If you want to join game then enter \"#join <id game>\"\n");
        session_send_string(sess->sd, "If you want to create game then enter \"#create <game name>\"\n");
        snprintf(message, sizeof(message), "Welcome to the chat, you are known as <%s>\n", sess->username);
        session_send_string(sess->sd, message);
        sess->state = fsm_interaction;
        break;
    case fsm_signup_username:
        if (line) {
            sess->username = line;
        }
        else {
            session_send_string(sess->sd, "Please, enter your username?\n");
            break;
        }
        session_send_string(sess->sd, "Please enter your pasword:\n");
        sess->state = fsm_signup_password;
        break;
    case fsm_signup_password:
        if (line) {
            sess->password = line;
        }
        else {
            session_send_string(sess->sd, "Please enter your pasword:\n");
            break;
        }
        res = authenticate_user(f, sess->username, sess->password);
        if (res == 0) {
            session_send_string(sess->sd, "You have successfully signed up.\n");
            session_send_string(sess->sd, "If you want to see list of games enter then \"#list\"\n");
            session_send_string(sess->sd, "If you want to check your profile then enter \"#profile\"\n");
            session_send_string(sess->sd, "If you want to join game then enter \"#join <id game>\"\n");
            session_send_string(sess->sd, "If you want to create game then enter \"#create <game name>\"\n");
            snprintf(message, sizeof(message), "Welcome to the chat, you are known as <%s>\n", sess->username);
            session_send_string(sess->sd, message);
            sess->state = fsm_interaction;
        }
        else if (res == 1) {
            session_send_string(sess->sd, "Incorrect password\nPlease enter your pasword:\n");
            sess->state = fsm_signup_password;
        }
        else {
            session_send_string(sess->sd, "This user is not registered in the system yet.\nEnter \"2\" to log in\n");
            sess->state = fsm_start;
        }
        break;
    case fsm_interaction:
        if(line[0] == '#') {
            res = command_check(serv, sess, line);
            if(res != 0) {
                session_send_string(sess->sd, "Unknown command\n");
            }
        } else {
            snprintf(message, sizeof(message), "<%s> %s\n", sess->username, line);
            for(i = 0; i < serv->sess_array_size; i++) {
                if((serv->sess_array[i] != NULL) && (serv->sess_array[i]->player_info != NULL) && (serv->sess_array[i]->player_info->game_id == -1)) {
                    session_send_string(serv->sess_array[i]->sd, message);
                }
            }
        }
        free(line); /* because we still own it... */
        break;
    case fsm_game:
        if(serv->games_array[sess->player_info->game_id]->is_started == true) {
            session_game(serv, serv->games_array[sess->player_info->game_id], sess, line);
        } else {
            if((line[0] == 0) && (serv->games_array[sess->player_info->game_id]->player_count > 1)) {
                serv->games_array[sess->player_info->game_id]->is_started = true;
                serv->games_array[sess->player_info->game_id]->defending_player = serv->games_array[sess->player_info->game_id]->players[1];
                serv->games_array[sess->player_info->game_id]->attacking_player = serv->games_array[sess->player_info->game_id]->players[0];
                shuffle_deck(serv->games_array[sess->player_info->game_id]->deck, serv->games_array[sess->player_info->game_id]->deck_size);
                deal_cards(serv->games_array[sess->player_info->game_id]);
                for(i = 0; i < MAX_PLAYERS; i++) {
                    if(serv->games_array[sess->player_info->game_id]->players[i] != NULL) {
                        session_send_string(serv->games_array[sess->player_info->game_id]->players[i]->sd, "\n\nThe game has begun\n\n");
                    }
                }
                send_table(serv, serv->games_array[sess->player_info->game_id], sess);
            }
        }
        free(line);
        break;
    case fsm_finish:
    case fsm_error:
        free(line);   /* this should never happen */
    }
}


/* =========== server =========== */

static int server_init(struct server_str* serv, int port)
{
    int sock, i, opt;
    FILE *file;
    struct sockaddr_in addr;

    printf("port: %d\n", port);
    fflush(stdout);
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }
    opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (-1 == bind(sock, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("bind");
        return 2;
    }

    listen(sock, LISTEN_QLEN);

    serv->ls = sock;
    file = fopen("userdata.txt", "a+b");
    if (file == NULL) {
        perror("usersdata.txt");
        close(sock);
        return 3;
    }
    serv->file_account = file;

    serv->sess_array = malloc(sizeof(*serv->sess_array) * INIT_SESS_ARR_SIZE);
    serv->sess_array_size = INIT_SESS_ARR_SIZE;
    for (i = 0; i < INIT_SESS_ARR_SIZE; i++)
        serv->sess_array[i] = NULL;
    serv->games_array = malloc(sizeof(*serv->games_array) * INIT_GAMES_ARR_SIZE);
    serv->games_array_size = INIT_GAMES_ARR_SIZE;
    for(i = 0; i < INIT_GAMES_ARR_SIZE; i++)
        serv->games_array[i] = NULL;
    return 0;
}

static void server_remove_session(struct server_str* serv, int sd)
{
    printf("Client with socket %d disconect\n", serv->sess_array[sd]->sd);
    if (serv->sess_array[sd] != NULL) {
        if(serv->sess_array[sd]->player_info->game_id != -1) {

            serv->games_array[serv->sess_array[sd]->player_info->game_id]->player_count--;   // game->player_count--
            if(serv->games_array[serv->sess_array[sd]->player_info->game_id]->player_count == 0) { //if game->player_count == 0
                free(serv->games_array[serv->sess_array[sd]->player_info->game_id]->name);    // free(game->name)
                free(serv->games_array[serv->sess_array[sd]->player_info->game_id]->players); // free(game->players)
                free(serv->games_array[serv->sess_array[sd]->player_info->game_id]);          // free(game)
                serv->games_array[serv->sess_array[sd]->player_info->game_id] = NULL;         // game = NULL
            }
            if(serv->sess_array[sd]->player_info->hand != NULL) {
                free(serv->sess_array[sd]->player_info->hand);
                serv->sess_array[sd]->player_info->hand = NULL;
            }
            free(serv->sess_array[sd]->player_info);
            serv->sess_array[sd]->player_info = NULL;
        }
        free(serv->sess_array[sd]->username);
        free(serv->sess_array[sd]->password);
        free(serv->sess_array[sd]);
        serv->sess_array[sd] = NULL;
    } else {
        printf("serv->sess_array[sd] = NULL\n");
    }
    close(sd);
    return;
}

static void session_commit(struct client_session* sess, FILE* f)
{
    unsigned int ip = sess->from_ip;
    fprintf(f, "From %d.%d.%d.%d:%d; "
        "Username: %s; Password: %s\n",
        (ip >> 24 & 0xff), (ip >> 16 & 0xff), (ip >> 8 & 0xff), (ip & 0xff),
        sess->from_port,
        sess->username, sess->password);
    fflush(f);
}

static void session_check_lf(struct server_str* serv, struct client_session* sess)
{
    int pos = -1;
    int i;
    char* line;
    for (i = 0; i < sess->bd; i++) {
        if (sess->buf[i] == '\n') {
            pos = i;
            break;
        }
    }
    if (pos == -1)
        return;
    line = malloc(pos + 1);
    memcpy(line, sess->buf, pos);
    line[pos] = 0;
    sess->bd -= (pos + 1);
    memmove(sess->buf, sess->buf + pos + 1, sess->bd);
    if (line[pos - 1] == '\r')
        line[pos - 1] = 0;
    session_fsm_step(serv, sess, line, serv->file_account);  /* we transfer ownership! */
}


static int session_do_read(struct server_str* serv, struct client_session* sess, FILE* f)
{
    int rc, bufp = sess->bd;
    rc = recv(sess->sd, sess->buf + bufp, INBUFSIZE - bufp, 0);
    if (rc <= 0) {
        sess->state = fsm_error;
        return 0;   /* this means "don't continue" for the caller */
    }
    sess->bd += rc;
    session_check_lf(serv, sess);
    if (sess->bd >= INBUFSIZE) {
        /* we can't read further, no room in the buffer, no whole line yet */
        session_send_string(sess->sd, "Line too long! Good bye...\n");
        sess->state = fsm_error;
        return 0;
    }
    if (sess->state == fsm_finish)
        return 0;
    return 1;
}

static void server_accept_client(struct server_str* serv)
{
    int sd, i;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    sd = accept(serv->ls, (struct sockaddr*)&addr, &len);
    if (sd == -1) {
        perror("accept");
        return;
    }

    if (sd >= serv->sess_array_size) {  /* need to resize */
        int newlen = serv->sess_array_size;
        while (sd >= newlen)
            newlen += INIT_SESS_ARR_SIZE;
        serv->sess_array =
            realloc(serv->sess_array, newlen * sizeof(struct session*));
        for (i = serv->sess_array_size; i < newlen; i++)
            serv->sess_array[i] = NULL;
        serv->sess_array_size = newlen;
    }

    serv->sess_array[sd] = make_new_session(sd, &addr);
}


int server_go(struct server_str* serv)
{
    fd_set readfds, writefds;
    int i, sr, ssr, maxfd;
    for (;;) { /* ========= THE APPLICATION MAIN LOOP ========= */
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(serv->ls, &readfds);
        maxfd = serv->ls;
        for (i = 0; i < serv->sess_array_size; i++) {
            if (serv->sess_array[i]) {
                FD_SET(i, &readfds);
                FD_SET(i, &writefds);
                if (i > maxfd) {
                    maxfd = i;
                }
            }
        }
        sr = select(maxfd + 1, &readfds, &writefds, NULL, NULL);
        if (sr == -1) {
            perror("select");
            return 4;
        }
        if (FD_ISSET(serv->ls, &readfds))
            server_accept_client(serv);
        for (i = 0; i < serv->sess_array_size; i++) {
            if (serv->sess_array[i] && FD_ISSET(i, &readfds)) {
                ssr = session_do_read(serv, serv->sess_array[i], serv->file_account);
                if (!ssr) {
                    server_remove_session(serv, i);
                }
            }
        }
    }
    return 0;
}

// Main server function
int main(int argc, char *argv[]) {
    struct server_str server;
    
    if(argc < 2) {
        fprintf(stderr, "Usage: ./server <file_name>\n");
        return 1;
    }
    
    // Server socket initialization
    if (server_init(&server, port) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        return EXIT_FAILURE;
    }
    return server_go(&server);
} 

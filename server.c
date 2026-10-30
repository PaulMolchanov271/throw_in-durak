#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>


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

#ifndef USERNAME_LEN
#define USERNAME_LEN 64
#endif

enum { port = 5555, buffer_size = 256};

enum fsm_states { 
    fsm_start, 
    fsm_signup_username, 
    fsm_signup_password, 
    fsm_login_username, 
    fsm_login_password, 
    fsm_interaction, 
    fsm_game, 
    fsm_finish, 
    fsm_error 
};

struct player_str {
    int id;
    int *hand;
    int game_id;
    int card_count;
};

struct client_session {
    int sd;                            /* socket descriptor */
    char buf[INBUFSIZE];             /* buffer */
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
    int pairs_count;
    int table_cards[MAX_CARDS];
    int undefended_count;
    //int card_queue[25];
    //int queue_len;
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
void format_card(int card, char *output);
void send_game_start(struct game_session *game);
void send_turn_info(struct game_session *game);
void send_table_gui(struct game_session *game);
void send_player_update(struct game_session *game, int player_idx);
void send_round_end(struct game_session *game, const char *result);
void send_error(int sd, const char *message);



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
    struct client_session* sess = calloc(1, sizeof(struct client_session));
    sess->sd = sd;
    sess->bd = 0;
    sess->state = fsm_start;
    sess->from_ip = ntohl(from->sin_addr.s_addr);
    sess->from_port = ntohs(from->sin_port);
    sess->player_info = calloc(1, sizeof(struct player_str));
    sess->player_info->game_id = -1;
    printf("connected socket %d\n", sd);
    return sess;
}

static void session_send_string(int sd, const char* str)
{
    if (!str || sd < 0) return;
    size_t left = strlen(str);
    while (left) {
        ssize_t n = send(sd, str, left, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        str += n;
        left -= n;
    }
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
        for(i = 0; i < serv->games_array_size; i++) {
            if(serv->games_array[i] == NULL) {
                continue;
            }
            char* centered_name = center_text(serv->games_array[i]->name, 17);
            
            snprintf(message, sizeof(message),
                    " %d %s %d %d\n",
                    serv->games_array[i]->id, 
                    centered_name, 
                    serv->games_array[i]->player_count, 
                    serv->games_array[i]->is_started);

            free(centered_name);
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
    game->id = id;
    game->defending_player = NULL;
    game->attacking_player = NULL;
    game->pairs_count = 0;
    game->undefended_count = 0;
}

struct game_session *execute_create_game(struct server_str *serv, struct client_session *sess, char *game_name, int id)
{
    struct game_session *game = malloc(sizeof(*game));
    (void)serv;
    int i = 0;
    char buffer[256];
    game_init(game, id);
    while((game_name[i] != ' ') && (game_name[i] != '\0') && (game_name[i] != '\n')) {
        i++;
    }
    game_name[i] = '\0';
    game->name = strdup(game_name);
    sess->player_info->game_id = game->id;
    game->player_count += 1;
    game->players[0] = sess;
    game->players[0]->player_info->hand = NULL;
    game->players[0]->player_info->card_count = 0;
    game->players[0]->player_info->id = 0;

    char str_id[16];
    snprintf(str_id, sizeof(str_id), "%d\n", sess->player_info->game_id);
    printf("GAME ID: %s\n", str_id);
    session_send_string(sess->sd, str_id);

    snprintf(buffer, sizeof(buffer),
             "#player %d %s %d\n",
             0, game->players[0]->username, game->players[0]->player_info->card_count);
    session_send_string(game->players[0]->sd, buffer);
    
    return game;
}

static int execute_join_game(struct server_str *serv, struct client_session *sess, int game_id)
{
    int i = 0;
    int j = 0;
    char buffer[256];
    struct game_session *game = serv->games_array[game_id];
    if(game->player_count == MAX_PLAYERS) {
        session_send_string(sess->sd, "This game is full\n");
        return 1;
    }
    sess->player_info->game_id = game_id;
    while((i < MAX_PLAYERS) && (game->players[i] != NULL)) {
        i++;
    }
    game->players[i] = sess;
    game->players[i]->player_info->hand = NULL;
    game->players[i]->player_info->card_count = 0;
    game->players[i]->player_info->id = i;
    game->player_count += 1; 
    session_send_string(sess->sd, "#joined\n");
    char *name;

    i = j = 0;    
    while((i < MAX_PLAYERS) && (game->players[i] != NULL)) {
        j = 0;
        while((j < MAX_PLAYERS) && (game->players[j] != NULL)) {
            name = game->players[j]->username;

            snprintf(buffer, sizeof(buffer),
                 "#player %d %s %d\n",
                 j, name, game->players[j]->player_info->card_count);
            {
                session_send_string(game->players[i]->sd, buffer);
            }
            j++;
        }
        i++;
    }
    return 0;
}

static int command_check(struct server_str* serv, struct client_session *sess, char *command)
{
    int i;
    char *ptr;
    if(strncmp(command, "#list", 5) == 0) {
        execute_list(serv, sess);
        session_send_string(sess->sd, "#list_end\n");
        return 0;
    }
    if(strncmp(command, "#create", 7) == 0) {
        if (strlen(command) < 9 || command[7] != ' ') {
            send_error(sess->sd, "Provide a game name"); return 0;
        }
        ptr = command + 8;
        if (strlen(ptr) > 49 || strpbrk(ptr, " \t\r")) {
            send_error(sess->sd, "Game name: use 1-49 characters without spaces"); return 0;
        }
        i = 0;
        while(i < serv->games_array_size && serv->games_array[i] != NULL) {
            i++;
        }
        if(i >= serv->games_array_size) {
            int newlen = serv->games_array_size;
            while (i >= newlen)
                newlen += INIT_GAMES_ARR_SIZE;
            serv->games_array =
                realloc(serv->games_array, newlen * sizeof(*serv->games_array));
            for (i = serv->games_array_size; i < newlen; i++)
                serv->games_array[i] = NULL;
            serv->games_array_size = newlen;
        }
        serv->games_array[i] = execute_create_game(serv, sess, ptr, i);
        sess->player_info->game_id = serv->games_array[i]->id;
        sess->state = fsm_game;
        return 0;
    }
    if(strncmp(command, "#join", 5) == 0) {
        int game_id = -1; char extra;
        if (sscanf(command, "#join %d %c", &game_id, &extra) != 1) {
            send_error(sess->sd, "Invalid game ID"); return 0;
        }
        if (game_id < 0 || game_id >= serv->games_array_size || !serv->games_array[game_id]) {
            send_error(sess->sd, "Game does not exist"); return 0;
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

    for (int i = size - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        int temp = deck[i];
        deck[i] = deck[j];
        deck[j] = temp;
    }
}

void format_card(int card, char *output) {
    if(card != -1) {
        const char *ranks[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
        const char *suits = "^v%#";
        int rank = card % 13;
        int suit = card / 13;
        snprintf(output, 6, "%s%c", ranks[rank], suits[suit]);
    } else {
        snprintf(output, 6, " ");
    }
}


// Distribute cards to player
void distribute_cards(struct player_str *player, int *deck, int *deck_size) {
    if (player->hand == NULL) {
        player->hand = calloc(MAX_CARDS, sizeof(int));
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
    if(player->card_count > 6) {
        printf("ERROR");
    }
}

// Deal cards to all players
void deal_cards(struct game_session* game) {
    int first = game->attacking_player->player_info->id;
    for (int i = 0; i < game->player_count; ++i) {
        struct client_session *p = game->players[(first+i)%game->player_count];
        if (p != game->defending_player) distribute_cards(p->player_info, game->deck, &game->deck_size);
    }
    distribute_cards(game->defending_player->player_info, game->deck, &game->deck_size);
}

// A small append-only result journal preserves account statistics across restarts.
static void load_profile(struct client_session *sess) {
    if (!sess->password) return;
    sess->wins = sess->losses = 0;
    // Guests have session-only statistics.
    FILE *f = fopen("stats.txt", "r");
    if (!f) return;
    char name[USERNAME_LEN]; int win;
    while (fscanf(f, "%63s %d", name, &win) == 2) {
        if (!strcmp(name, sess->username)) {
            if (win) sess->wins++; else sess->losses++;
        }
    }
    fclose(f);
}
static void send_profile(struct client_session *sess) {
    char msg[160];
    snprintf(msg, sizeof(msg), "#profile %s %d %d\n", sess->username, sess->wins, sess->losses);
    session_send_string(sess->sd, msg);
}
static void record_result(struct client_session *sess, bool won) {
    if (won) sess->wins++; else sess->losses++;
    if (sess->password) {
        FILE *f = fopen("stats.txt", "a");
        if (f) { fprintf(f, "%s %d\n", sess->username, won); fclose(f); }
        else perror("Cannot persist statistics");
    }
    send_profile(sess);
}
void end_game(struct game_session *game, struct client_session *winner) {
    if (!game->is_started) return; // Exactly one result per match.
    game->is_started = false;
    char msg[160];
    snprintf(msg, sizeof(msg), "#game_end %d\n", winner->player_info->id);
    for (int i = 0; i < game->player_count; ++i) {
        record_result(game->players[i], game->players[i] == winner);
        session_send_string(game->players[i]->sd, msg);
    }
}
static void send_roster(struct game_session *game) {
    char msg[160];
    for (int i = 0; i < game->player_count; ++i) {
        snprintf(msg, sizeof(msg), "#lobby %d\n", game->player_count);
        session_send_string(game->players[i]->sd, msg);
        for (int j = 0; j < game->player_count; ++j) {
            snprintf(msg, sizeof(msg), "#player %d %s %d\n", j,
                     game->players[j]->username, game->players[j]->player_info->card_count);
            session_send_string(game->players[i]->sd, msg);
        }
    }
}
static void leave_game(struct server_str *serv, struct client_session *sess) {
    int id = sess->player_info->game_id;
    if (id < 0 || id >= serv->games_array_size || !serv->games_array[id]) return;
    struct game_session *g = serv->games_array[id];
    if (g->is_started) {
        // Leaving forfeits the match. All remaining players win.
        g->is_started = false;
        for (int i = 0; i < g->player_count; ++i) {
            record_result(g->players[i], g->players[i] != sess);
            session_send_string(g->players[i]->sd,
                g->players[i] == sess ? "#game_end -1\n" : "#game_end -2\n");
        }
    }
    int index = sess->player_info->id;
    for (int i = index; i + 1 < g->player_count; ++i) {
        g->players[i] = g->players[i+1];
        g->players[i]->player_info->id = i;
    }
    g->players[--g->player_count] = NULL;
    free(sess->player_info->hand);
    sess->player_info->hand = NULL;
    sess->player_info->card_count = 0;
    sess->player_info->game_id = -1;
    sess->state = fsm_interaction;
    session_send_string(sess->sd, "#left\n");
    g->attacking_player = g->defending_player = NULL;
    if (!g->player_count) {
        free(g->name); free(g->players); free(g); serv->games_array[id] = NULL;
    } else send_roster(g);
}
static void start_game(struct game_session *g, struct client_session *sess) {
    if (g->is_started || g->player_count < 2 || g->players[0] != sess) {
        send_error(sess->sd, "Only the host can start with at least two players"); return;
    }
    g->deck_size = MAX_CARDS;
    g->pairs_count = g->undefended_count = 0;
    for (int i = 0; i < MAX_CARDS; ++i) { g->deck[i] = i; g->table_cards[i] = -1; }
    for (int i = 0; i < g->player_count; ++i) {
        free(g->players[i]->player_info->hand);
        g->players[i]->player_info->hand = NULL;
        g->players[i]->player_info->card_count = 0;
    }
    shuffle_deck(g->deck, g->deck_size);
    g->trump_card = g->deck[0];
    g->attacking_player = g->players[0]; g->defending_player = g->players[1];
    deal_cards(g); g->is_started = true;
    send_game_start(g); send_turn_info(g); send_table_gui(g);
}



// Update defending player
void update_defending_player(struct game_session* game) {
    int defending_player_index = 0;
    while(game->players[defending_player_index] != game->defending_player) {
        defending_player_index++;
    }
    defending_player_index = (defending_player_index + 1) % game->player_count;
    game->defending_player = game->players[defending_player_index];
}

// Update attacking player
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

// Clear table
void clear_table(struct game_session *game)
{
    for (int i = 0; i < game->pairs_count*2; i++) {
        game->table_cards[i] = -1;
    }
    game->pairs_count = 0;
    game->undefended_count = 0;
}

// Take table cards
void take_table_cards(struct game_session *game)
{
    if (!game || !game->defending_player || !game->defending_player->player_info) {
        fprintf(stderr, "Invalid game or player state.\n");
        return;
    }
    if(game->undefended_count < 1) {
        return;
    }
    int table_cards_count = 0;
    for(int i = 0; i < game->pairs_count * 2; i++) {
        if(game->table_cards[i] != -1) {
            table_cards_count++;
        }
    }

    struct player_str *defender = game->defending_player->player_info;
    int new_card_count = defender->card_count + table_cards_count;

    int *new_hand = malloc((MAX_CARDS + 1) * sizeof(int));
    if (!new_hand) {
        fprintf(stderr, "Failed to allocate memory for defender's hand.\n");
        return;
    }
    
    for(int i = 0; i < defender->card_count; i++) {
        new_hand[i] = defender->hand[i];
    }
    free(defender->hand);
    defender->hand = new_hand;

    for (int i = 0; i < game->pairs_count*2; i++) {
        if(game->table_cards[i] != -1) {
            defender->hand[defender->card_count++] = game->table_cards[i];
        }
        game->table_cards[i] = -1;
    }

    defender->card_count = new_card_count;
    defender->hand[new_card_count] = -1;
    game->pairs_count = 0;
    game->undefended_count = 0;
}


/*******************************************GUI Protocol Functions************************************/


// Send game start information to all players
void send_game_start(struct game_session *game) {
    char buffer[2048];
    int i;
    
    // Format trump suit: ^, v, #, %
    const char *suits = "^v%#";
    char trump_suit = suits[game->trump_card / 13];
    
    for (i = 0; i < game->player_count; i++) {
        if (game->players[i] == NULL) continue;
        
        int offset = 0;
        
        // #game_start TRUMP_SUIT PLAYER_COUNT
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, 
                          "#game_start %c %d\n", trump_suit, game->player_count);
        
        // Information about all players
        for (int j = 0; j < game->player_count; j++) {
            if (game->players[j] == NULL) continue;
            
            const char *name = game->players[j]->username; 
            
            if((game->players[j]->player_info->card_count != 6) && (game->players[j]->player_info->card_count != 0)) {
               printf("ERROR\n");
            }
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                             "#player %d %s %d\n", 
                             j, name, game->players[j]->player_info->card_count);
        }
        
        // Send cards to current player
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "#your_cards");
        for (int k = 0; k < game->players[i]->player_info->card_count; k++) {
            char card_str[6];
            format_card(game->players[i]->player_info->hand[k], card_str);
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, " %s", card_str);
        }
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");
        
        session_send_string(game->players[i]->sd, buffer);
    }
}

// Send turn information
void send_turn_info(struct game_session *game) {
    char buffer[256];
    int attacker_idx = -1, defender_idx = -1;
    
    // Find attacker and defender indices
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i] == game->attacking_player) {
            attacker_idx = i;
        }
        if (game->players[i] == game->defending_player) {
            defender_idx = i;
        }
    }
    
    snprintf(buffer, sizeof(buffer), "#turn %d %d\n", attacker_idx, defender_idx);
    
    // Send to all players
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i] != NULL) {
            session_send_string(game->players[i]->sd, buffer);
        }
    }
}

// Send table state to all players
void send_table_gui(struct game_session *game) {
    char buffer[2048];
    int offset;
    int d = 0;
    
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i+d] == NULL) {
            d++;
            i--;
            continue;
        }
    

        
        offset = 0;

        send_player_update(game, i);
        
        // #table PAIR_COUNT
        int pair_count = game->pairs_count;
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                          "#table %d\n", pair_count);
        
        // Send card pairs
        for (int j = 0; j < game->pairs_count; j++) {
            char attack_card[6] = {0};
            char defense_card[6] = {0};
            
            format_card(game->table_cards[j*2], attack_card);
            
            if (game->table_cards[j*2+1] != -1) {
                format_card(game->table_cards[j*2+1], defense_card);
            }
            
            offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                             "#pair %s %s\n", attack_card, defense_card);
        }

        if(game->players[i]->player_info->hand != NULL) {
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "#your_cards");
            for (int k = 0; k < game->players[i]->player_info->card_count; k++) {
                char card_str[6];
                format_card(game->players[i]->player_info->hand[k], card_str);
                offset += snprintf(buffer + offset, sizeof(buffer) - offset, " %s", card_str);
            }
            offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");
        }
        session_send_string(game->players[i]->sd, buffer);

        

    }
}

// Send player update
void send_player_update(struct game_session *game, int player_idx) {
    char buffer[128];
    
    snprintf(buffer, sizeof(buffer), "#player_update %d %d\n", 
             player_idx, game->players[player_idx]->player_info->card_count);
    
    // Send to all players
    for (int i = 0; i < game->player_count; i++) {
        if ((game->players[i] != NULL) && (i != player_idx)) {
            session_send_string(game->players[i]->sd, buffer);
        }
    }
}

// Send new cards to player
void send_draw_cards(struct client_session *player, int *new_cards, int count) {
    char buffer[512];
    int offset = 0;
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "#draw_cards");
    
    for (int i = 0; i < count; i++) {
        char card_str[6];
        format_card(new_cards[i], card_str);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, " %s", card_str);
    }
    
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n");
    session_send_string(player->sd, buffer);
}

// Send round end
void send_round_end(struct game_session *game, const char *result) {
    char buffer[128];
    
    snprintf(buffer, sizeof(buffer), "#round_end %s\n", result);
    
    // Send to all players
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i] != NULL) {
            session_send_string(game->players[i]->sd, buffer);
        }
    }
}

// Send error message
void send_error(int sd, const char *message) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "#error %s\n", message);
    session_send_string(sd, buffer);
}

// Parse card command
int parse_card_command(const char *line, int *cards, int max_cards, int *pos) {
    char copy[INBUFSIZE]; snprintf(copy, sizeof(copy), "%s", line);
    char *save = NULL; char *token = strtok_r(copy, " ", &save);
    int n = 0;
    if (pos) *pos = -1;
    while ((token = strtok_r(NULL, " ", &save))) {
        int found = -1;
        for (int c = 0; c < MAX_CARDS; ++c) {
            char text[6]; format_card(c, text);
            if (!strcmp(text, token)) { found = c; break; }
        }
        if (found >= 0 && n < max_cards && (!pos || *pos == -1)) cards[n++] = found;
        else if (pos && n == 1 && *pos == -1) {
            char *end; long v = strtol(token, &end, 10);
            if (*end || v < 0 || v >= 6) return 0;
            *pos = (int)v;
        } else return 0;
    }
    return n;
}
static bool owns_card(struct client_session *s, int card) {
    for (int i = 0; i < s->player_info->card_count; ++i)
        if (s->player_info->hand[i] == card) return true;
    return false;
}
static void remove_card(struct client_session *s, int card) {
    struct player_str *p = s->player_info;
    for (int i = 0; i < p->card_count; ++i) if (p->hand[i] == card) {
        memmove(p->hand+i, p->hand+i+1, (--p->card_count-i)*sizeof(int)); return;
    }
}
int handle_attack_command(struct server_str *serv, struct game_session *g,
                          struct client_session *s, const char *line) {
    (void)serv;
    int cards[6], n = parse_card_command(line, cards, 6, NULL);
    if (!n || s == g->defending_player || (!g->pairs_count && s != g->attacking_player) ||
        g->pairs_count+n > 6 || g->undefended_count+n > g->defending_player->player_info->card_count) {
        send_error(s->sd, "Invalid attack or too many cards"); return 1;
    }
    // Validate the entire move before changing any state.
    for (int i = 0; i < n; ++i) {
        bool rank_ok = g->pairs_count == 0 && cards[i]%13 == cards[0]%13;
        for (int j = 0; j < g->pairs_count*2; ++j)
            if (g->table_cards[j] >= 0 && g->table_cards[j]%13 == cards[i]%13) rank_ok = true;
        for (int j = 0; j < i; ++j) if (cards[j] == cards[i]) rank_ok = false;
        if (!rank_ok || !owns_card(s, cards[i])) {
            send_error(s->sd, "Invalid rank, duplicate or unowned card"); return 1;
        }
    }
    for (int i = 0; i < n; ++i) {
        g->table_cards[g->pairs_count*2] = cards[i];
        g->table_cards[g->pairs_count++*2+1] = -1;
        g->undefended_count++; remove_card(s, cards[i]);
    }
    send_table_gui(g);
    if (!s->player_info->card_count) end_game(g, s);
    return 0;
}
int handle_defend_command(struct server_str *serv, struct game_session *g,
                         struct client_session *s, const char *line) {
    (void)serv;
    int cards[6], pair = -1, n = parse_card_command(line, cards, 6, &pair);
    if (n != 1 || pair < 0 || pair >= g->pairs_count || s != g->defending_player ||
        g->table_cards[pair*2+1] != -1 || !owns_card(s, cards[0])) {
        send_error(s->sd, "Specify one owned card and an undefended pair index"); return 1;
    }
    int c = cards[0], a = g->table_cards[pair*2];
    if (!((c/13 == a/13 && c%13 > a%13) || (c/13 == g->trump_card/13 && a/13 != c/13))) {
        send_error(s->sd, "Cannot defend with this card"); return 1;
    }
    g->table_cards[pair*2+1] = c; g->undefended_count--; remove_card(s, c);
    send_table_gui(g);
    if (!s->player_info->card_count) end_game(g, s);
    return 0;
}



// Handle take command
void handle_take_command(struct server_str *serv, struct game_session *game,
                        struct client_session *sess) {
    (void)serv;
    if (sess != game->defending_player) {
        send_error(sess->sd, "You are not defending");
        return;
    }
    
    if (!game->undefended_count) { send_error(sess->sd, "No cards to take"); return; }
    take_table_cards(game);
    send_round_end(game, "taken");
    
    // Update player info
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i] != NULL) {
            send_player_update(game, i);
        }
    }
    
    // Shift turn
    deal_cards(game);
    int next = (game->defending_player->player_info->id + 1) % game->player_count;
    game->attacking_player = game->players[next];
    game->defending_player = game->players[(next + 1) % game->player_count];
    
    send_turn_info(game);
    send_table_gui(game);
}

// Handle pass command
void handle_pass_command(struct server_str *serv, struct game_session *game,
                        struct client_session *sess) {
    (void)serv;
    // Check all cards are defended
    if (game->undefended_count > 0 || sess != game->attacking_player) {
        send_error(sess->sd, "Not all cards are defended");
        return;
    }

    
    if (game->pairs_count == 0) {
        send_error(sess->sd, "No cards on table");
        return;
    }
    
    send_round_end(game, "beaten");
    clear_table(game);
    deal_cards(game);
    
    // Update player info
    for (int i = 0; i < game->player_count; i++) {
        if (game->players[i] != NULL) {
            send_player_update(game, i);
        }
    }
    
    update_defending_player(game);
    update_attacking_player(game);
    
    send_turn_info(game);
    send_table_gui(game);
}




static void session_game_gui(struct server_str* serv, struct game_session *game, 
                            struct client_session* sess, char *line) {
    // Process commands
    if (strncmp(line, "#attack", 7) == 0) {
        printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\nATTACK\n%s\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n", line);
        handle_attack_command(serv, game, sess, line);
    } else if (strncmp(line, "#defend", 7) == 0) {
        handle_defend_command(serv, game, sess, line);
    } else if (strncmp(line, "#take", 5) == 0) {
        handle_take_command(serv, game, sess);
    } else if (strncmp(line, "#pass", 5) == 0) {
        handle_pass_command(serv, game, sess);
    } else {
        printf("LINE ==> <%s>\n", line);
        send_error(sess->sd, "Unknown command");
    }
}

// FSM step
static void session_fsm_step(struct server_str* serv, struct client_session* sess, char* line, FILE* f)
{
    int res, i;
    char message[256];
    
    if (sess->state == fsm_game || sess->state == fsm_interaction) {
        if (!strcmp(line, "#profile")) { load_profile(sess); send_profile(sess); free(line); return; }
        if (sess->state == fsm_game && (!strcmp(line, "#leave") || !strcmp(line, "#back"))) {
            leave_game(serv, sess); free(line); return;
        }
        if (sess->state == fsm_game && !strncmp(line, "#chat ", 6)) {
            struct game_session *g = serv->games_array[sess->player_info->game_id];
            char chat[512];
            if (strlen(line+6) > 240 || !line[6]) send_error(sess->sd, "Chat: use 1 to 240 bytes");
            else {
                for (char *c = line+6; *c; ++c) if ((unsigned char)*c < 32) *c = ' ';
                snprintf(chat, sizeof(chat), "#chat <%s> %s\n", sess->username, line+6);
                for (int j = 0; j < g->player_count; ++j) session_send_string(g->players[j]->sd, chat);
            }
            free(line); return;
        }
    }
    if (strcmp(line, "#back") && (sess->state == fsm_signup_username || sess->state == fsm_login_username)) {
        bool valid = strlen(line) > 0 && strlen(line) <= 49;
        for (const char *c = line; *c; ++c)
            if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-') valid = false;
        if (!valid) { session_send_string(sess->sd, "Username: use 1-49 letters, digits, _ or -.\n"); free(line); return; }
    }
    if (strcmp(line, "#back") && sess->state == fsm_signup_password && (!*line || strlen(line) > 49 || strchr(line, ';'))) {
        session_send_string(sess->sd, "Password: use 1-49 characters without semicolons.\n"); sess->state = fsm_signup_username; free(line); return;
    }
    if(strcmp(line, "#back") == 0) {
        free(line);
        switch (sess->state) {
            case fsm_login_username:
            case fsm_signup_username:
            case fsm_login_password:
            case fsm_signup_password:
                sess->state = fsm_start;
                return;
            case fsm_interaction:
                sess->state = fsm_start;
                return;
            case fsm_game:
                sess->state = fsm_interaction;
                return;
            default:
                return;
        }
    }
    
    switch (sess->state) {
    case fsm_start:
        if (line) {
            if (strcmp(line, "1") == 0) {
                sess->state = fsm_signup_username;
            }
            else if (strcmp(line, "2") == 0) {
                sess->state = fsm_login_username;
            }
            else if (strcmp(line, "3") == 0) {
                sess->state = fsm_interaction;
                free(sess->username); free(sess->password); sess->password = NULL;
                sess->wins = sess->losses = 0;
                sess->username = malloc(USERNAME_LEN);
                snprintf(sess->username, USERNAME_LEN, "Guest%ld_%d", time(NULL), sess->sd);
                session_send_string(sess->sd, sess->username);
                session_send_string(sess->sd, "\n");
            }
            free(line);
        }
        break;
        
    case fsm_login_username:
        if (line) {
            free(sess->username); sess->username = line;
        }
        sess->state = fsm_login_password;
        session_send_string(sess->sd, "Login username, OK\n");
        break;
        
    case fsm_login_password:
        if (line) {
            free(sess->password); sess->password = line;
        }
        res = authenticate_user(f, sess->username, sess->password);
        if (res == 0) {
            for (int j = 0; j < serv->sess_array_size; ++j) {
                struct client_session *other = serv->sess_array[j];
                if (other && other != sess && other->username &&
                    (other->state == fsm_game || other->state == fsm_interaction) &&
                    !strcmp(other->username, sess->username)) {
                    session_send_string(sess->sd, "This account is already online.\n"); sess->state = fsm_login_username; return;
                }
            }
            session_send_string(sess->sd, "You have been successfully logged into the system.\n");
            sess->state = fsm_interaction;
            load_profile(sess);
        }
        else if (res == 1) {
            sess->state = fsm_login_username;
            session_send_string(sess->sd, "Incorrect password.\n");
        }
        else {
            session_send_string(sess->sd, "This user is not registered in the system yet.\n");
            sess->state = fsm_login_username;
        }
        break;
        
    case fsm_signup_username:
        if (line) {
            res = authenticate_user(f, line, NULL);
            if (res == 1) {
                session_send_string(sess->sd, "User with this username already exist.\n");
                free(line);
                break;
            } else {
                session_send_string(sess->sd, "Sign up username, OK\n");
                free(sess->username); sess->username = line;
            }
        } else {
            break;
        }
        sess->state = fsm_signup_password;
        break;
        
    case fsm_signup_password:
        if (line) {
            free(sess->password); sess->password = line;
            session_send_string(sess->sd, "You have been successfully Sign up to the system.\n");
        } else {
            break;
        }
        session_commit(sess, f);
        sess->state = fsm_interaction;
        break;
        
    case fsm_interaction:
        if(line[0] == '#') {
            res = command_check(serv, sess, line);
            if(res != 0) {
                session_send_string(sess->sd, "Unknown command\n");
            }
        } else {
            snprintf(message, sizeof(message), "<%s> %s\n", 
                    sess->username, line);
            for(i = 0; i < serv->sess_array_size; i++) {
                if((serv->sess_array[i] != NULL) && 
                   (serv->sess_array[i]->player_info != NULL) && 
                   (serv->sess_array[i]->player_info->game_id == -1)) {
                    session_send_string(serv->sess_array[i]->sd, message);
                }
            }
        }
        free(line);
        break;
        
    case fsm_game: {
        struct game_session *g = serv->games_array[sess->player_info->game_id];
        if (!strcmp(line, "#game_start") || !strcmp(line, "#restart")) start_game(g, sess);
        else if (g->is_started) session_game_gui(serv, g, sess, line);
        else send_error(sess->sd, "The match is not active");
        free(line); break;
    }

    case fsm_finish:
    case fsm_error:
        free(line);
        break;
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

static void server_remove_session(struct server_str* serv, int sd) {
    struct client_session *client = serv->sess_array[sd];
    if (!client) return;
    leave_game(serv, client);
    close(sd);
    free(client->player_info); free(client->username); free(client->password); free(client);
    serv->sess_array[sd] = NULL;
}

static void session_commit(struct client_session* sess, FILE* f)
{
    fseek(f, 0, SEEK_END);
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
    memmove(sess->buf, sess->buf+pos+1, sess->bd);

    while ((sess->bd > 0) && (sess->buf[0] == '\n' || sess->buf[0] == '\r')) {
        memmove(sess->buf, sess->buf + 1, --sess->bd);
    }

    if (pos > 0 && line[pos - 1] == '\r')
        line[pos - 1] = 0;
    session_fsm_step(serv, sess, line, serv->file_account);  /* we transfer ownership! */
}


static int session_do_read(struct server_str* serv, struct client_session* sess, FILE* f)
{
    (void)f;
    int rc, bufp = sess->bd;
    rc = recv(sess->sd, sess->buf + bufp, INBUFSIZE - bufp, 0);
    if (rc <= 0) {
        sess->state = fsm_error;
        return 0;   /* this means "don't continue" for the caller */
    }
    sess->bd += rc;
    while (memchr(sess->buf, '\n', sess->bd)) session_check_lf(serv, sess);
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

    if (sd >= FD_SETSIZE) { close(sd); return; }
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
    fd_set readfds;
    int i, sr, ssr, maxfd;
    for (;;) { /* ========= THE APPLICATION MAIN LOOP ========= */
        FD_ZERO(&readfds);

        FD_SET(serv->ls, &readfds);
        maxfd = serv->ls;
        for (i = 0; i < serv->sess_array_size; i++) {
            if (serv->sess_array[i]) {
                FD_SET(i, &readfds);

                if (i > maxfd) {
                    maxfd = i;
                }
            }
        }
        sr = select(maxfd + 1, &readfds, NULL, NULL, NULL);
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
int main(void) {
    struct server_str server;
    
    srand((unsigned)time(NULL));
    signal(SIGPIPE, SIG_IGN);
    // Server socket initialization
    if (server_init(&server, port) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        return EXIT_FAILURE;
    }
    return server_go(&server);
} 

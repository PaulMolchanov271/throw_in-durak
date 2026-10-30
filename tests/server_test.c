#define main durak_server_main
#include "../server.c"
#undef main
#include <assert.h>

static struct client_session *player(const char *name) {
    struct sockaddr_in addr = {0};
    struct client_session *p = make_new_session(-1, &addr);
    p->username = strdup(name);
    p->state = fsm_game;
    return p;
}
static void hand(struct client_session *p, const int *cards, int n) {
    free(p->player_info->hand);
    p->player_info->hand = calloc(MAX_CARDS, sizeof(int));
    memcpy(p->player_info->hand, cards, n * sizeof(int));
    p->player_info->card_count = n;
}
int main(void) {
    struct server_str serv = {0};
    serv.games_array_size = 1;
    serv.games_array = calloc(1, sizeof(*serv.games_array));
    struct client_session *a = player("Alice"), *b = player("Bob"), *c = player("Carol");
    char name[] = "test";
    struct game_session *g = execute_create_game(&serv, a, name, 0);
    serv.games_array[0] = g;
    start_game(g, a); assert(!g->is_started); // one player cannot start
    execute_join_game(&serv, b, 0);
    start_game(g, b); assert(!g->is_started); // only host
    start_game(g, a); assert(g->is_started && g->deck_size == 40);
    assert(a->player_info->card_count == 6 && b->player_info->card_count == 6);
    int ac[] = {4}, bc[] = {5, 6}; hand(a, ac, 1); hand(b, bc, 2);
    assert(handle_attack_command(&serv, g, a, "#attack 6^ 6^") == 1);
    assert(a->player_info->card_count == 1 && g->pairs_count == 0);
    assert(handle_attack_command(&serv, g, a, "#attack 6^") == 0);
    assert(!g->is_started && g->deck_size > 0 && a->wins == 1 && b->losses == 1);
    end_game(g, a); assert(a->wins == 1 && b->losses == 1);
    start_game(g, a); assert(g->is_started && g->pairs_count == 0 && g->deck_size == 40);
    int ac2[] = {4, 17}, bc2[] = {5}; hand(a, ac2, 2); hand(b, bc2, 1);
    assert(handle_attack_command(&serv, g, a, "#attack 6^") == 0);
    assert(handle_defend_command(&serv, g, b, "#defend 7^ -1") == 1);
    assert(handle_defend_command(&serv, g, b, "#defend A^ 0") == 1);
    handle_pass_command(&serv, g, a); assert(g->pairs_count == 1);
    assert(handle_defend_command(&serv, g, b, "#defend 7^ 0") == 0);
    assert(!g->is_started && b->wins == 1 && a->losses == 1);
    execute_join_game(&serv, c, 0); start_game(g, a);
    int ac3[] = {4, 17, 30}, bc3[] = {5, 6, 7}; hand(a, ac3, 3); hand(b, bc3, 3);
    assert(handle_attack_command(&serv, g, a, "#attack 6^ 6v") == 0);
    assert(g->undefended_count == 2);
    assert(handle_defend_command(&serv, g, b, "#defend 7^ 0") == 0);
    take_table_cards(g);
    assert(b->player_info->card_count == 5);
    assert(b->player_info->hand[2] == 4 && b->player_info->hand[3] == 5 && b->player_info->hand[4] == 17);
    leave_game(&serv, b);
    assert(!g->is_started && g->player_count == 2 && g->players[1] == c);
    assert(c->player_info->id == 1 && b->player_info->game_id == -1);
    assert(a->wins == 2 && b->losses == 2 && c->wins == 1);
    start_game(g, a); assert(g->is_started && g->deck_size == 40);
    leave_game(&serv, a); assert(g->players[0] == c && c->player_info->id == 0);
    leave_game(&serv, c); assert(serv.games_array[0] == NULL);
    leave_game(&serv, c); // idempotent
    for (int i = 0; i < 3; ++i) {
        struct client_session *p = i == 0 ? a : (i == 1 ? b : c);
        free(p->username); free(p->player_info); free(p);
    }
    free(serv.games_array);
    puts("All game lifecycle tests passed");
}

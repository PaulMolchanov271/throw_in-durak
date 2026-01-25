#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Multiline_Output.H>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <iostream>
#include <vector>
#include <functional>


// Visual parameters
enum {
    spacing = 5,
    button_w = 200,
    button_h = 40,
    font_size = 20
};

#define BUFFER_SIZE 1024

struct Profile {
    int /*sd, */id = -1;
    char *name;
    int wins, loses;
};

class GameWindow;


struct AppState {
    Fl_Window *current_state = NULL;
    Fl_Window *mainWin = NULL;
    Fl_Window *signupWin = NULL;
    Fl_Window *loginWin = NULL;
    Fl_Window *mainMenuWin = NULL;
    Fl_Window *listGamesWin = NULL;
    Fl_Window *createGameWin = NULL;
    GameWindow *gameWin = NULL;
    Fl_Multiline_Output *chatOutput = NULL;
};

struct AppInput {
    Fl_Input *inp = NULL;
};

// Card structure
struct Card {
    char suit;      // ^, v, #, % (spades, hearts, diamonds, clubs)
    char rank[3];   // 6, 7, 8, 9, 10, J, Q, K, A
    
    Card() : suit('\0') { rank[0] = '\0'; }
};

// Player structure
struct Player {
    int id = -1;
    int cards_count;
    std::string name;
};

// class Game 
class Game {
private:
    int id;
    char *name = nullptr;
    int players_count;
    Player players[8];
    //Card *hand;
    std::vector<Card> hand;
    bool is_started;
    char trump_suit;
    std::vector<Card> table_attack;
    std::vector<Card> table_defense;
    int table_count;
    int current_attacker_index;
    int current_defender_index;
    
public:
    Game() : id(-1), name(nullptr), players_count(0), is_started(false), 
             trump_suit('\0'), table_count(0), current_attacker_index(-1), current_defender_index(-1) {}
    Game(int game_id, const char* game_name, int players_cnt, bool started); 
    
    ~Game();
    
    int GetGameId() const { return id; }
    char *GetGameName() const { return name; }
    int GetPlayerCardsCount(int player_index) const { return players[player_index].cards_count; }
    Player &GetPlayer(int index) { return players[index]; }
    char GetTrumpSuit() const { return trump_suit; }
    int GetPlayersCount() const { return players_count; }
    bool IsGameStarted() const { return is_started; }
    int GetCurrentAttacker() const { return current_attacker_index; }
    int GetCurrentDefender() const { return current_defender_index; }
    int GetTableCount() const { return table_count; }
    std::vector<Card> &GetHand() { return hand; }
    std::vector<Card> &GetTableAttack(/*int index*/) { return table_attack/*[index]*/; }
    std::vector<Card> &GetTableDefense(/*int index*/) { return table_defense/*[index]*/; }

    void UpdateAttacker(int attacker_index) { current_attacker_index = attacker_index; }
    void UpdateDefender(int defender_index) { current_defender_index = defender_index; }
    void UpdatePlayerCardsCount(int index, int c_c) { players[index].cards_count = c_c; }
    void SetPlayersCount(int pc); 
    void SetTrumpSuit(char tr) { trump_suit = tr; }
    void SetTableCount(int tc) { table_count = tc; } 
    void StartGame() { is_started = true; }
};

enum class GameCmd {
    GAME_START,
    PLAYER,
    YOUR_CARDS,
    TURN,
    TABLE,
    PAIR,
    PLAYER_UPDATE,
    DRAW_CARDS,
    ROUND_END,
    GAME_END,
    ERROR,
    UNKNOWN
};


// Game state structure
struct GameState {                      /*TODO: should be deleted*/
    Game *current_game;                 
    std::vector<Card> player_cards;
    bool is_my_turn_attack;
    bool is_my_turn_defend;
    
    GameState() : current_game(nullptr), is_my_turn_attack(false), 
                  is_my_turn_defend(false) {}
};

// Server class
class Server {
private:
    std::string host;
    int port;
    AppState& state;
    Game *game;
    int sock;
    std::atomic<bool> running{false};
    std::thread recv_thread;

    struct Profile prof;

    mutable std::mutex buf_mutex;
    std::string recv_buffer;
    

    void recv_loop();
    void ReceiveMessage(const std::string& msg);

    void TriggerWindowUpdate();

public:
    //Server(const std::string& host, int port, AppState& state); 
    Server(AppState& state_, Game *game_) 
            : state(state_), game(game_), sock(-1), running(false) {}

    ~Server(); 

    void start(const std::string& host_, int port_/*, AppState& state_*/); 

    void connect_to_server();
    bool is_running() const;
    bool send_string(const char *str) const;
    char *fetch_buffer();
    //void ReceiveMessage();
    bool authenticate(const char *user, const char *pass);
    void ProcessGameMessage(const char* msg);
    /*int GetSock();*/
    int GetPlayerId();
    void SetGame(Game *g);
    Game *GetGameState() { return game; }
    struct Profile &GetProfile() { return prof; }
};

void send_callback(Fl_Widget* w, void* data);

#endif

#include "client.hpp" 
#include "game_table.hpp"
#include "windows.hpp"
#include <thread>
#include <sys/socket.h> // recv


// Parse card from string
Card parse_card(const char* card_str) {
    Card card;
    int len = strlen(card_str);

    // Last character is suit
    if (len < 2 || len > 3) return card;
    card.suit = card_str[len - 1];

    // Rest is rank
    strncpy(card.rank, card_str, len - 1);
    card.rank[len - 1] = '\0';

    return card;
}

Game::Game(int game_id, const char* game_name, int players_cnt, bool started)
    : id(game_id), name(game_name ? game_name : ""), players_count(players_cnt), is_started(started),
      trump_suit('\0'), table_count(0), current_attacker_index(-1), current_defender_index(-1) {}

Game::~Game() = default;

void Game::SetPlayersCount(int pc) 
{
    if((pc >= 0) && (pc <= 8)) {
        players_count = pc;
    } 
}


Server::~Server()
{
    running = false;
    if (sock >= 0) {
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }
    if (recv_thread.joinable()) recv_thread.join();
    delete game;
}

void Server::start(const std::string& host_, int port_)
{
    host = host_;
    port = port_;
    connect_to_server();
    if (running) {
        recv_thread = std::thread(&Server::recv_loop, this);
    }
}

void Server::connect_to_server()
{
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
       perror("socket");
       return;
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr) <= 0) {
        fl_message("Invalid address / Address not supported");
        return;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        fl_message("Connection Failed");
        exit(0);
    }
    running = true;
}

bool Server::is_running() const
{
    return running;
}

bool Server::send_string(const char *str) const
{
    if (!str || !running) return false;
    std::string message = std::string(str) + "\n";
    size_t offset = 0;
    while (offset < message.size()) {
        ssize_t n = send(sock, message.data()+offset, message.size()-offset, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        offset += n;
    }
    return true;
}

void Server::SetGame(Game *g) 
{ 
    delete game;
    game = nullptr;
    game = g; 
}

char *Server::fetch_buffer()
{
    std::lock_guard<std::mutex> lock(buf_mutex);

    std::size_t pos = recv_buffer.find('\n');
    if (pos == std::string::npos) {
        return nullptr;
    }

    char* line = new char[pos + 1];
    std::memcpy(line, recv_buffer.data(), pos);
    line[pos] = '\0';

    recv_buffer.erase(0, pos + 1);

    return line;
}


// Trigger window update from main thread using FLTK's awake mechanism
void Server::TriggerWindowUpdate() {
    if (state.gameWin && state.current_state == state.gameWin) state.gameWin->UpdateUI();
}
char *Server::wait_response() {
    for (int i = 0; i < 500 && running; ++i) {
        if (char *line = fetch_buffer()) return line;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    fl_message("Server did not respond. Please reconnect.");
    return nullptr;
}
void Server::ShowProfile() {
    state.profileOutput->value("Loading profile...");
    state.profileWin->show();
    send_string("#profile");
}
void Server::PumpMessages() {
    if (state.current_state != state.mainMenuWin && state.current_state != state.gameWin) return;
    bool changed = false;
    while (char *line = fetch_buffer()) {
        changed = changed || (line[0] == '#' && strncmp(line, "#chat ", 6) != 0 && strncmp(line, "#profile ", 9) != 0);
        ReceiveMessage(line); delete[] line;
    }
    if (changed) TriggerWindowUpdate();
}

GameCmd getCommand(const char* msg) {
    if (strncmp(msg, "#game_start", 11) == 0) return GameCmd::GAME_START;
    if (strncmp(msg, "#player ", 8) == 0) return GameCmd::PLAYER;
    if (strncmp(msg, "#your_cards", 11) == 0) return GameCmd::YOUR_CARDS;
    if (strncmp(msg, "#turn", 5) == 0) return GameCmd::TURN;
    if (strncmp(msg, "#table", 6) == 0) return GameCmd::TABLE;
    if (strncmp(msg, "#pair", 5) == 0) return GameCmd::PAIR;
    if (strncmp(msg, "#player_update", 14) == 0) return GameCmd::PLAYER_UPDATE;
    if (strncmp(msg, "#draw_cards", 11) == 0) return GameCmd::DRAW_CARDS;
    if (strncmp(msg, "#round_end", 10) == 0) return GameCmd::ROUND_END;
    if (strncmp(msg, "#game_end", 9) == 0) return GameCmd::GAME_END;
    if (strncmp(msg, "#error", 6) == 0) return GameCmd::ERROR;

    return GameCmd::UNKNOWN;
}

void Server::ProcessGameMessage(const char* msg)
{
    if (!game && getCommand(msg) != GameCmd::ERROR) return;
    switch (getCommand(msg))
    {
        case GameCmd::GAME_START: {
            char trump_suit;
            int player_count;
            sscanf(msg, "#game_start %c %d", &trump_suit, &player_count);



            game->SetTrumpSuit(trump_suit);
            game->SetPlayersCount(player_count);
            game->SetTableCount(0);
            game->StartGame();
            printf("Game started! Trump: %c, Players: %d\n", trump_suit, player_count);
        } break;

        case GameCmd::PLAYER: {
            int index, cards_count;
            char name[64];
            sscanf(msg, "#player %d %63s %d", &index, name, &cards_count);

            if ((game != nullptr) && (index >= 0 && index < 8)) {
                if((game->GetPlayer(index).id == -1) && (game->IsGameStarted() == false)) {
                    game->SetPlayersCount(std::max(game->GetPlayersCount(), index + 1));
                }
                if(prof.name == name) {
                    prof.id = index;
                }
                game->GetPlayer(index).id = index;
                game->GetPlayer(index).name = name;
                game->UpdatePlayerCardsCount(index, cards_count);
            }
            printf("\033[31mPlayer id - %d: %s (%d cards)\033[0m\n", index, name, cards_count);
        } break;

        case GameCmd::YOUR_CARDS: {
            game->GetHand().clear();
            const char* ptr = msg + 12;
            char card_str[4];
            if (prof.id < 0 || prof.id >= 8) break;
            auto &player = game->GetPlayer(prof.id);
            while (sscanf(ptr, "%3s", card_str) == 1) {
                game->GetHand().push_back(parse_card(card_str));
                ptr = strchr(ptr, ' ');
                if (!ptr) break;
                ptr++;
            }
            player.cards_count = game->GetHand().size();
        } break;

        case GameCmd::TURN: {
            int attacker_idx, defender_idx;
            sscanf(msg, "#turn %d %d", &attacker_idx, &defender_idx);
            if (game != nullptr) {
                game->UpdateAttacker(attacker_idx);
                game->UpdateDefender(defender_idx);
            }
        } break;

        case GameCmd::TABLE: {
            int pair_count;
            sscanf(msg, "#table %d", &pair_count);
            if (game != nullptr) {
                game->SetTableCount(pair_count);
                game->GetTableAttack().clear(); game->GetTableDefense().clear();
            }
        } break;

        case GameCmd::PAIR: {
            char attack[6] = {}, defense[6] = {};
            int n = sscanf(msg, "#pair %5s %5s", attack, defense);
            if (n >= 1) {
                game->GetTableAttack().push_back(parse_card(attack));
                game->GetTableDefense().push_back(n == 2 ? parse_card(defense) : Card{});
            }
        } break;

        case GameCmd::PLAYER_UPDATE: {
            int index, cards_count;
            sscanf(msg, "#player_update %d %d", &index, &cards_count);
            if ((game != nullptr) && (index >= 0 && index < 8)) {
                game->UpdatePlayerCardsCount(index, cards_count); 
            }
        } break;

        case GameCmd::DRAW_CARDS: {
            const char* ptr = msg + 12;
            char card_str[6];
            while (sscanf(ptr, "%5s", card_str) == 1) {
                game->GetHand().push_back(parse_card(card_str));
                ptr = strchr(ptr, ' ');
                if (!ptr) break;
                ptr++;
            }
        } break;

        case GameCmd::ROUND_END: {
            char result[16];
            sscanf(msg, "#round_end %15s", result);
            printf("Round ended: %s\n", result);
            if (state.chatOutput) {
                std::string cur(state.chatOutput->value());
                cur += "Round ended: ";
                cur += result;
                cur += "\n";
                state.chatOutput->value(cur.c_str());
            }
            game->GetTableAttack().clear();
            game->GetTableDefense().clear();
            game->SetTableCount(0);
        } break;

        case GameCmd::GAME_END: {
            int winner = -1;
            sscanf(msg, "#game_end %d", &winner);
            bool won = winner == prof.id || winner == -2;
            game->FinishGame(won ? "You won! The host can restart the match." : "You lost. The host can restart the match.");
        } break;

        case GameCmd::ERROR: {
            const char* error_msg = msg + 7;
            printf("Server error: %s\n", error_msg);
            auto* err = new std::string(error_msg);
            Fl::awake([](void* data) {
                auto* msg = static_cast<std::string*>(data);
                fl_message("Error: %s", msg->c_str());
                delete msg;
            }, err);
        } break;

        case GameCmd::UNKNOWN:
        default:
            printf("Unknown command: %s\n", msg);
            break;
    }
    
}


void Server::ReceiveMessage(const std::string& msg)
{
    if (msg.empty()) return;

    if (msg.rfind("#profile ", 0) == 0) {
        char name[64]; int wins, losses;
        if (sscanf(msg.c_str(), "#profile %63s %d %d", name, &wins, &losses) == 3) {
            prof.wins = wins; prof.loses = losses;
            if (state.profileOutput) {
                std::string text = std::string(name) + "\nWins: " + std::to_string(wins) + "\nLosses: " + std::to_string(losses);
                state.profileOutput->value(text.c_str());
            }
        }
        return;
    }
    if (msg == "#left") {
        if (state.gameWin) state.gameWin->hide();
        state.current_state = state.mainMenuWin; state.mainMenuWin->show(); return;
    }
    if (msg.rfind("#lobby ", 0) == 0 && game) {
        game->ResetPlayers(); return;
    }
    if (msg.rfind("#chat ", 0) == 0) {
        if (state.gameWin && state.current_state == state.gameWin) state.gameWin->AppendChat(msg.substr(6));
        return;
    }
    if (msg[0] == '#') {
        ProcessGameMessage(msg.c_str());
    } else {
        if (state.chatOutput) {
            printf("AWAKE\n");
            Fl::lock();
            std::string current(state.chatOutput->value());
            current += msg + "\n";
            state.chatOutput->value(current.c_str());
            Fl::unlock();
            Fl::awake();
        }
    }
}

void Server::recv_loop()
{
    char buf[BUFFER_SIZE];
    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        struct timeval tv{};
        tv.tv_sec  = 1;
        tv.tv_usec = 0;
        int ret = select(sock + 1, &readfds, nullptr, nullptr, &tv);
        if (ret < 0) {
            perror("select");
            running = false;
            break;
        }
        else if (ret == 0) {
            continue;
        }
        if (FD_ISSET(sock, &readfds)) {
            int rc = recv(sock, buf, BUFFER_SIZE - 1, 0);
            if (rc <= 0) {
                running = false;
                break;
            }
            buf[rc] = '\0';
            {
                std::lock_guard<std::mutex> lock(buf_mutex);
                recv_buffer.append(buf);
                Fl::awake();
            }
        }
        

    }
}

bool Server::authenticate(const char *user, const char *pass)
{
    if(user[0] == '\0' || pass[0] == '\0') {
        return false;
    }
    if(!send_string(user)) {
        return false;
    }
    char *msg = nullptr;
    msg = wait_response();
    if (!msg) return false;
    if(msg == nullptr) {
        return false;
    }
    if((strcmp(msg, "User with this username already exist.") == 0) || 
       ((strcmp(msg, "Login username, OK") != 0) && (strcmp(msg, "Sign up username, OK") != 0))) {
        fl_message("%s", msg);
        delete [] msg;
        return false;
    }
    delete [] msg;
    msg = nullptr;
    if(!send_string(pass)) {
        fl_message("Failed to send password");
        return false;
    }

    msg = wait_response();
    if (!msg) return false;

    if((strcmp(msg, "You have been successfully logged into the system.") == 0)
    || (strcmp(msg, "You have been successfully Sign up to the system.") == 0)) {
        fl_message("%s", msg);
        delete [] msg;
        prof.name = user;
        return true;
    }

    if (msg != nullptr) {
        fl_message("%s", msg);
        delete [] msg;
    }

    return false;
}



int Server::GetPlayerId()
{
    return prof.id;
}

void send_callback(Fl_Widget* w, void* data) {
    Fl_Input *input = static_cast<Fl_Input*>(w);
    Server *server = static_cast<Server*>(data);
    server->send_string(((Fl_Input*)input)->value());
    ((Fl_Input*)input)->take_focus();
}

void StartServerCallback(void* arg) {
    auto pair = static_cast<std::pair<Server*, AppState*>*>(arg);
    pair->first->start("127.0.0.1", 5555);
    delete pair;
}

void update_ui_cb(void* w) {
    static_cast<GameWindow*>(w)->UpdateUI();
    Fl::repeat_timeout(0.5, update_ui_cb, w);
}

#ifndef DURAK_NO_MAIN
int main() 
{
    Fl::lock();

    AppState state;
    Game *game = nullptr;
    Server server(state, game);

    state.mainWin       = createMainWindow(state, server);
    state.signupWin     = state.loginWin = createAuthWindow(server, state);
    state.mainMenuWin   = createMainMenuWindow(state, server);
    state.listGamesWin  = createListGameWindow(server, state, nullptr, 0);
    state.createGameWin = createCreateGameWindow(server, state);
    state.gameWin       = static_cast<GameWindow*>(createGameWindow(server, state));

    state.current_state = state.mainWin;
    state.mainWin->show();


    server.start("127.0.0.1", 5555);

    Fl::add_timeout(0.05, [](void *data) {
        auto *server = static_cast<Server*>(data);
        server->PumpMessages();
    }, &server);
    while (Fl::wait()) server.PumpMessages();
    return 0;
}


#endif

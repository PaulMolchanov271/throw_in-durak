#include "client.hpp"
#include "game_table.hpp"
#include "windows.hpp"

GameTable::GameTable(int X, int Y, int W, int H) : Fl_Table(X, Y, W, H) {
    cols(4);
    col_header(1);
    col_resize(1);
    col_width_all(150);

    row_header(0);
    row_height_all(60);

    end();
}

void GameTable::set_games(const std::vector<Game>& game_list) {
    games = game_list;
    rows(games.size());
    redraw();
}

void GameTable::draw_cell(
    Fl_Table_Row::TableContext context,
    int R, int C,
    int X, int Y, int W, int H
) {
    if (context == CONTEXT_STARTPAGE) {
        fl_font(FL_HELVETICA, 14);
        return;
    }

    if (context == CONTEXT_COL_HEADER) {
        static const char* headers[] = {
            "GAME ID",
            "GAME NAME",
            "PLAYER COUNT",
            "IS STARTED"
        };

        if (C < 0 || C >= 4) return;

        fl_draw_box(FL_THIN_UP_BOX, X, Y, W, H, FL_GRAY);
        fl_color(FL_BLACK);
        fl_draw(headers[C], X + 5, Y + H - 6);
        return;
    }

    if (context == CONTEXT_CELL) {
        if (static_cast<std::size_t>(R) >= games.size()) return;

        fl_draw_box(FL_FLAT_BOX, X, Y, W, H, FL_WHITE);
        fl_color(FL_BLACK);

        std::string text;
        switch (C) {
            case 0:
                text = std::to_string(games[R].GetGameId());
                break;
            case 1:
                text = games[R].GetGameName();
                break;
            case 2:
                text = std::to_string(games[R].GetPlayersCount());
                break;
            case 3:
                text = games[R].IsGameStarted() ? "yes" : "no";
                break;
            default:
                return;
        }

        fl_draw(text.c_str(), X + 5, Y + H - 6);
    }
}


LoginButton::LoginButton(int y, const char* lb, AppState& st, Server& serv)
    : ButtonCommon(y, lb), state(st), server(serv) {}

void LoginButton::OnPress() {
    state.mainWin->hide();
    state.loginWin->label("Login");
    state.loginWin->show();
    state.current_state = state.loginWin;
    server.send_string("2");
}

SignUpButton::SignUpButton(int y, const char* lb, AppState& st, Server& serv)
    : ButtonCommon(y, lb), state(st), server(serv) {}

void SignUpButton::OnPress() {
    state.mainWin->hide();
    state.signupWin->label("Sign Up");
    state.signupWin->show();
    state.current_state = state.signupWin;
    server.send_string("1");
}

GuestButton::GuestButton(int y, const char* lb, AppState& st, Server& serv)
    : ButtonCommon(y, lb), state(st), server(serv) {}

void GuestButton::OnPress() {
    server.send_string("3");
    
    char *msg = nullptr;
    while(msg == nullptr) {
        msg = server.fetch_buffer();
    }
    server.GetProfile().name = msg;
    
    state.mainWin->hide();
    state.mainMenuWin->label("Main menu");
    state.mainMenuWin->show();
    state.current_state = state.mainMenuWin;
}

void OKButton::OnPress() 
{
    const char *name = name_field->value();
    const char *pass = password_field->value();
    int player_id = -1, cards_count = -1;
    char player_name[64];

    if(state.current_state == state.createGameWin) {
        if (name == NULL || name[0] == '\0') {
            fl_message("Please enter game name.");
            return;
        }
        std::string msg = std::string("#create ") + name;
        server.send_string(msg.c_str());
        char *recv_info = nullptr;
        while(recv_info == nullptr) {
            recv_info = server.fetch_buffer();
        }
        int game_id = 0, i = 0;
        while(recv_info[i] != '\0') {
            game_id = game_id * 10 + (recv_info[i] - 48);
            i++;
        }

        if(state.gameWin) {
            delete state.gameWin;
        }
        state.gameWin = nullptr;
        Game *game = new Game(game_id, name, 1, false);
        server.SetGame(game);

        delete[] recv_info;
        recv_info = nullptr;
        while(recv_info == nullptr) {
            recv_info = server.fetch_buffer();
        }
        sscanf(recv_info, "#player %d %s %d", &player_id, player_name, &cards_count);
        delete[] recv_info;
        recv_info = nullptr;

        server.GetProfile().id = server.GetGameState()->GetPlayer(0).id = player_id;
        server.GetGameState()->GetPlayer(0).cards_count = cards_count;
        state.gameWin = static_cast<GameWindow*>(createGameWindow(server, state/*, game*/));

        state.gameWin->label(name);
        state.current_state->hide();
        state.current_state = state.gameWin;
        state.gameWin->initUI();
        state.gameWin->show();
        return;
    }

    // Send data and check result
    bool ok = server.authenticate(name, pass);

    if (ok) {
        state.current_state->hide();
        state.mainMenuWin->label("Main menu");
        state.mainMenuWin->show();
        state.current_state = state.mainMenuWin;
    }
}


BackButton::BackButton(
    int y,
    const char* label,
    Fl_Window* fromWin,
    Fl_Window* toWin,
    Server& serv,
    AppState& st
)
    : ButtonCommon(y, label),
      from(fromWin),
      to(toWin),
      server(serv),
      state(st)
{
}

void BackButton::OnPress() {
    from->hide();
    to->show();

    if (state.current_state != state.listGamesWin &&
        state.current_state != state.createGameWin)
    {
        server.send_string("#back");
    }

    state.current_state = to;
}


JoinButton::JoinButton(
    int x, int y, int w, int h,
    const char* label,
    Server& serv,
    AppState& st,
    Game* g
)
    : ButtonCommon(y, label),
      server(serv),
      state(st),
      game(g)
{
    resize(x, y, w, h);
}

void JoinButton::OnPress() {
    // 1. Send join command
    std::string msg = "#join " + std::to_string(game->GetGameId());
    server.send_string(msg.c_str());

    // 2. Hide current window
    if (state.current_state)
        state.current_state->hide();

    // 3. Destroy previous game window
    delete state.gameWin;
    state.gameWin = nullptr;

    // 4. Update server state
    server.SetGame(game);

    auto* gs = server.GetGameState();
    if (gs && gs->GetPlayersCount() > 0) {
        server.GetProfile().id =
            gs->GetPlayer(gs->GetPlayersCount() - 1).id;
    }
    gs->SetPlayersCount(0);

    // 5. Create new game window
    state.gameWin =
        static_cast<GameWindow*>(createGameWindow(server, state));

    state.gameWin->label(game->GetGameName());

    // 6. Switch state
    state.current_state = state.gameWin;

    // 7. Init UI
    state.gameWin->clearUI();
    state.gameWin->initUI();
    state.gameWin->show();
}


//////////////////////////////////////////////
// Parse games from server response
//////////////////////////////////////////////

void parse_games(const char* msg, Game **games, int& games_count) {
    int game_size = 10;

    const char* ptr = msg;
    while (*ptr) {
        int id, player_count, is_started;
        char name[64];

        int matched = sscanf(ptr, "%d %63s %d %d\n", &id, name, &player_count, &is_started);
        if (matched == 4) {
            if (games_count == game_size) {
                int new_size = game_size * 2;
                Game **new_games = new Game*[new_size];

                for (int i = 0; i < game_size; ++i) {
                    new_games[i] = games[i];
                }

                delete[] games;
                games = new_games;
                game_size = new_size;
            }

            games[games_count] = new Game(id, name, player_count, (is_started != 0));

            games_count++;
        }

        while (*ptr && *ptr != '\n') ptr++;
        if (*ptr == '\n') ptr++;
    }
}


ListGamesButton::ListGamesButton(int x, int y, const char *lb, AppState& st, Server& serv)
    : OOFLButton(x, y, 120, 30, lb),
      state(st),
      server(serv),
      games(new Game*[10]),
      game_count(0),
      msg(nullptr),
      i(0)
{}

void ListGamesButton::OnPress() {
    server.send_string("#list");
    msg = nullptr;
    i = 0;
    game_count = 0;

    state.current_state->hide();
    state.current_state = state.listGamesWin;

    while((msg == nullptr) && (i < 50)) {
        msg = server.fetch_buffer();
        usleep(100000);
        i++;
    }
    i = 0;

    if(msg) {
        while(msg != nullptr) {
            parse_games(msg, games, game_count);
            if (state.listGamesWin) {
                delete state.listGamesWin;
            }
            state.listGamesWin = createListGameWindow(server, state, games, game_count);
            state.listGamesWin->show();
            state.current_state = state.listGamesWin;

            delete [] msg;
            msg = nullptr;
            msg = server.fetch_buffer();
        }
    } else {
        state.current_state = state.mainMenuWin;
        state.current_state->show();
    }
}


CreateGameButton::CreateGameButton(int x, int y, const char* lb, AppState& st)
    : OOFLButton(x, y, 120, 30, lb),
      state(st)
{}

void CreateGameButton::OnPress() 
{
    if(state.current_state)
        state.current_state->hide();
    if(state.createGameWin)
        state.createGameWin->show();
    state.current_state = state.createGameWin;
}


//////////////////////////////////////////////
// Window creation functions
//////////////////////////////////////////////

// Create main window with Login and SignUp buttons
Fl_Window* createMainWindow(AppState& state, Server& server) {
    int win_w = button_w + spacing * 2;
    int win_h = button_h * 4 + spacing * 5;
    Fl_Window *win = new Fl_Window(win_w, win_h, "Throw-In Durak - Menu");
    new LoginButton(spacing, "Login", state, server);
    new SignUpButton(2 * spacing + button_h, "Sign Up", state, server);
    new GuestButton(3 * spacing + 2 * button_h, "Login as Guest", state, server);
    new QuitButton(4 * spacing + 3 * button_h);
    win->end();
    return win;
}


// Send chat message callback
void send_chat_message(Fl_Widget* w, void* data)
{
    auto* input  = static_cast<Fl_Input*>(w);
    auto* server = static_cast<Server*>(data);
    const char* text = input->value();
    if (text && *text) {
        std::string message = std::string(text);
        printf("<%s>\n\n\n", message.c_str());
        server->send_string(message.c_str());
        input->value("");
    }
}


// Create main menu window
Fl_Window* createMainMenuWindow(AppState& state, Server& server)
{
    constexpr int chatHeight = 150;
    constexpr int inputHeight = 25;
    constexpr int buttonWidth = 80;
    constexpr int inputSpacing = 10;

    int winW = (button_w + spacing) * 3 + spacing;
    int winH = button_h + spacing * 3 + chatHeight;

    auto* win = new Fl_Window(winW, winH, "Throw-In Durak - Menu");

    new ListGamesButton(spacing, spacing, "List of games", state, server);
    new CreateGameButton(spacing * 2 + button_w, spacing, "Create game", state);
    new ExitButton(spacing * 3 + button_w * 2, spacing, "Exit");

    state.chatOutput = new Fl_Multiline_Output(
        spacing,
        spacing * 2 + button_h,
        winW - spacing * 2,
        chatHeight - inputHeight - inputSpacing);
    state.chatOutput->box(FL_DOWN_BOX);
    state.chatOutput->textsize(12);

    int inputWidth = winW - spacing * 3 - buttonWidth;

    auto* chatInput = new Fl_Input(
        spacing,
        winH - inputHeight - spacing,
        inputWidth,
        inputHeight);
    chatInput->callback(send_chat_message, &server);
    chatInput->when(FL_WHEN_ENTER_KEY);

    auto* sendBtn = new Fl_Button(
        spacing * 2 + inputWidth,
        winH - inputHeight - spacing,
        buttonWidth,
        inputHeight,
        "Send");
    sendBtn->callback([](Fl_Widget*, void* data) {
        auto* p = static_cast<std::pair<Fl_Input*, Server*>*>(data);
        const char* text = p->first->value();
        if (text && *text) {
            p->second->send_string(text);
            p->first->value("");
        }
    }, new std::pair<Fl_Input*, Server*>(chatInput, &server));

    win->end();
    return win;
}



// Create authentication window with username/password fields
Fl_Window* createAuthWindow(Server& server, AppState& state) {
    int win_w = 300;
    int win_h = 200;
    Fl_Window *win = new Fl_Window(win_w, win_h, "Auth");

    int x = 100;
    Fl_Input *username_input = new Fl_Input(x, 20, 150, 25, "Username:");
    Fl_Input *password_input = new Fl_Input(x, 60, 150, 25, "Password:");
    password_input->type(FL_SECRET_INPUT); // Hide password input

    new OKButton(100, "OK", server, username_input, password_input, state);
    new BackButton(100 + button_h + spacing, "Back", win, state.mainWin, server, state);

    win->end();
    return win;
}


// Create list game window
Fl_Window* createListGameWindow(Server& server, AppState& state, Game** listGames, int games_count) {
    const int row_height = 30;
    const int col_width = 120;
    const int button_width = 80;
    const int visible_rows = 10;

    int window_width = col_width * 3 + button_width + 40;
    int window_height = row_height * visible_rows + 80;

    Fl_Window* window = new Fl_Window(window_width, window_height, "Game List");

    Fl_Scroll* scroll = new Fl_Scroll(10, 10, window_width - 20, window_height - 60);
    scroll->box(FL_DOWN_BOX);

    // Headers
    new Fl_Box(20, 10, col_width, row_height, "Game Name");
    new Fl_Box(20 + col_width, 10, col_width, row_height, "Players");
    new Fl_Box(20 + col_width * 2, 10, col_width, row_height, "Status");
    new Fl_Box(20 + col_width * 3, 10, button_width, row_height, "Join");

    // Game rows
    for (int i = 0; i < games_count; ++i) {
        int y = 10 + row_height * (i + 1);
        Game *g = listGames[i];

        // Game name
        new Fl_Box(20, y, col_width, row_height, g->GetGameName());

        // Player count
        char players_count_str[4];
        snprintf(players_count_str, sizeof(players_count_str), "%d", g->GetPlayersCount());
        Fl_Box* player_box = new Fl_Box(20 + col_width, y, col_width, row_height);
        player_box->copy_label(players_count_str);

        // Status
        const char* status = g->IsGameStarted() ? "Started" : "Waiting";
        new Fl_Box(20 + col_width * 2, y, col_width, row_height, status);

        // Join button
        new JoinButton(20 + col_width * 3, y, button_width, row_height, "Join", server, state, g);
    }

    scroll->end();

    new BackButton(window_height - 40, "Back", window, state.mainMenuWin, server, state);

    window->end();
    return window;
}



// Create game creation window
Fl_Window *createCreateGameWindow(Server& server, AppState& state)
{
    int win_w = 400;
    int win_h = 250;
    Fl_Window *win = new Fl_Window(win_w, win_h, "Create Game");

    int x = 100;
    Fl_Box *username_label = new Fl_Box(x, 20, 150, 25, "Game name:");
    Fl_Input *username_input = new Fl_Input(x, 40, 150, 25);
    username_label->labelfont(FL_BOLD);
    username_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    Fl_Box *password_label = new Fl_Box(x, 70, 150, 25, "Password (optional):");
    Fl_Input *password_input = new Fl_Input(x, 90, 150, 25);
    password_label->labelfont(FL_BOLD);
    password_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    new OKButton(150, "OK", server, username_input, password_input, state);
    new BackButton(150 + button_h + spacing, "Back", win, state.mainMenuWin, server, state);

    win->end();
    return win;
}

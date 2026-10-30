#ifndef WINDOWS_HPP
#define WINDOWS_HPP

#include <FL/Fl.H>
#include <FL/Fl_Table_Row.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Table.H>
#include <vector>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_Scroll.H>

// Forward declarations
Fl_Window *createListGameWindow(Server& server, AppState& state, Game **listGames, int games_count);

struct AppState;
class Server;
class Game;

// Game table widget
class GameTable : public Fl_Table {
private:
    std::vector<Game> games;

public:
    GameTable(int X, int Y, int W, int H); 

    void set_games(const std::vector<Game>& game_list); 

protected:
    void draw_cell(
        Fl_Table_Row::TableContext context,
        int R, int C,
        int X, int Y, int W, int H
    ) override;
};

// Base button class with callback forwarding
class OOFLButton : public Fl_Button {
public:
    OOFLButton(int x, int y, int w, int h, const char *lb = 0)
        : Fl_Button(x, y, w, h, lb) {
        callback(CallbackFunction);
    }
    virtual void OnPress() {}
private:
    static void CallbackFunction(Fl_Widget *w) {
        static_cast<OOFLButton*>(w)->OnPress();
    }
};

// Common button style
class ButtonCommon : public OOFLButton {
public:
    ButtonCommon(int y, const char *lb)
        : OOFLButton(spacing, y, button_w, button_h, lb) {
        labelsize(font_size);
    }
};

class LoginButton : public ButtonCommon {
    AppState& state;
    Server& server;

public:
    LoginButton(int y, const char* lb, AppState& st, Server& serv);
    void OnPress() override;
};

class SignUpButton : public ButtonCommon {
    AppState& state;
    Server& server;

public:
    SignUpButton(int y, const char* lb, AppState& st, Server& serv);
    void OnPress() override;
};

class GuestButton : public ButtonCommon {
    AppState& state;
    Server& server;

public:
    GuestButton(int y, const char* lb, AppState& st, Server& serv);
    void OnPress() override;
};

// Exit button
class ExitButton : public OOFLButton {
public:
    ExitButton(int x, int y, const char *lb)
            : OOFLButton(x, y, 120, 30, lb) {}
    virtual void OnPress() { exit(0); }
};

// Quit button
class QuitButton : public ButtonCommon {
public:
    QuitButton(int y) : ButtonCommon(y, "Quit") {}
    virtual void OnPress() { exit(0); }
};

// OK button sends data to the server
class OKButton : public ButtonCommon {
private:
    Server& server;
    const Fl_Input *name_field;
    const Fl_Input *password_field;
    AppState& state;
public:
    OKButton(int y, const char *lb, Server& serv, const Fl_Input *n, const Fl_Input *pass, AppState& st)
        : ButtonCommon(y, lb), server(serv), name_field(n), password_field(pass), state(st) {}

    void OnPress() override;
};

// Back button returns to previous window
class BackButton : public ButtonCommon {
    Fl_Window* from;
    Fl_Window* to;
    Server& server;
    AppState& state;

public:
    BackButton(
        int y,
        const char* label,
        Fl_Window* fromWin,
        Fl_Window* toWin,
        Server& serv,
        AppState& st
    );

    void OnPress() override;
};

// Join button to join a game
class JoinButton : public ButtonCommon {
    Server& server;
    AppState& state;
    Game* game;

public:
    JoinButton(
        int x, int y, int w, int h,
        const char* label,
        Server& serv,
        AppState& st,
        Game* g
    );

    void OnPress() override;
};


//////////////////////////////////////////////
// Parse games from server response
//////////////////////////////////////////////

void parse_games(const char* msg, std::vector<Game*>& games);

//////////////////////////////////////////////
// List games button
//////////////////////////////////////////////

class ListGamesButton : public OOFLButton {
    AppState& state;
    Server& server;
    std::vector<Game*> games;
    int game_count;
    char *msg;
    int i;

public:
    ListGamesButton(int x, int y, const char *lb, AppState& st, Server& serv);
    void OnPress() override;
};


// Create game button
class CreateGameButton : public OOFLButton {
    AppState& state;

public:
    CreateGameButton(int x, int y, const char* lb, AppState& st);
    void OnPress() override;
};


//////////////////////////////////////////////
// Window creation functions
//////////////////////////////////////////////

// Create main window with Login and SignUp buttons
Fl_Window* createMainWindow(AppState& state, Server& server);

// Send chat message callback
void send_chat_message(Fl_Widget* w, void* data);

// Create main menu window
Fl_Window* createMainMenuWindow(AppState& state, Server& server);

// Create authentication window with username/password fields
Fl_Window* createAuthWindow(Server& server, AppState& state);

// Create list game window
Fl_Window* createListGameWindow(Server& server, AppState& state, Game** listGames, int games_count); 

// Create game creation window
Fl_Window *createCreateGameWindow(Server& server, AppState& state);

#endif



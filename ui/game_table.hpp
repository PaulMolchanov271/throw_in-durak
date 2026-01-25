#ifndef GAME_TABLE_HPP
#define GAME_TABLE_HPP

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Multiline_Output.H>
#include <FL/Fl_PNG_Image.H>

#include <vector>
#include <string>

// Widget for displaying a card
/*class CardWidget : public Fl_Box {
    Fl_PNG_Image* img;
    Card card_data;
    bool selected;
    Server* server;
    
public:
    CardWidget(int x, int y, const Card& card, int w, int h, Server* srv);
    
    ~CardWidget();
    
    int handle(int event) override;
    
    bool isSelected() const { return selected; }
    Card getCard() const { return card_data; }
};*/


// Enhanced CardWidget with smart attack/defense detection
class CardWidget : public Fl_Box {
    Fl_PNG_Image* img;
    Card card_data;
    bool selected;
    bool dragging;
    int drag_start_x, drag_start_y;
    int original_x, original_y;
    Server* server;
    GameWindow* game_window; // Reference to game window
    
public:
    CardWidget(int x, int y, const Card& card, int w, int h, Server* srv, GameWindow* gw = nullptr);
    
    ~CardWidget() { if (img) delete img; }
    
    int handle(int event) override; 
    
    void ReturnToOriginal();
    
    bool isSelected() const { return selected; }
    Card getCard() const { return card_data; }
    
    void setSelected(bool sel);
};


/*===================================================================================================================================*/
/*=========================================================== GameWindow ============================================================*/
/*===================================================================================================================================*/
// Game window class with game logic
class GameWindow : public Fl_Window {
    Server& server;
    AppState& state;
    Game* game;
    std::vector<CardWidget*> player_cards;
    std::vector<CardWidget*> table_cards;
    std::vector<Fl_Box*> player_boxes;
    Fl_Button* start_btn = nullptr;
    Fl_Button* attack_btn = nullptr;
    Fl_Button* defend_btn = nullptr;
    Fl_Button* take_btn = nullptr;
    Fl_Button* pass_btn = nullptr;
    Fl_Box* status_label;
    Fl_Box* trump_box;
    Fl_Box* drop_zone;
    CardWidget* highlighted_target;
    
public:
    GameWindow(Server& srv, AppState& st/*, Game *g*/);

    ~GameWindow() { delete game; }

    Game *GetGame() { return game; }
    
    void initUI();
    void clearUI();
    
    void UpdateUI();
    void updatePlayerCards();
    void updateTableCards();
    void updateGameState(Game* new_game);
    void updatePlayerInfo();
    void updateStatus();
    
    void UpdateDropZoneLabel();
    void HighlightDropTarget(int x, int y);
    void ClearDropTargetHighlight();
    bool TryPlayCard(const Card& card, int drop_x, int drop_y);
    CardWidget* FindTableCardAt(int x, int y);
    
    // Callbacks for buttons
    static void start_callback(Fl_Widget* w, void* data);
    static void attack_callback(Fl_Widget* w, void* data);
    static void defend_callback(Fl_Widget* w, void* data);
    static void take_callback(Fl_Widget* w, void* data);
    static void pass_callback(Fl_Widget* w, void* data);
    void performStart();
    void performAttack();
    void performDefend();
    void performTake();
    void performPass();
};

// Function to create game window
Fl_Window* createGameWindow(Server& server, AppState& state/*, const char *name, Game *game*/);

#endif

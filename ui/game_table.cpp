#include "client.hpp"
#include "windows.hpp"
#include "game_table.hpp"


CardWidget::CardWidget(int x, int y, const Card& card, int w, int h, Server* srv, GameWindow* gw)
        : Fl_Box(x, y, w, h, nullptr), card_data(card), selected(false),
          dragging(false), drag_start_x(0), drag_start_y(0),
          original_x(x), original_y(y), server(srv), game_window(gw)
{
    std::string filename = "ui/PNG-cards-1.3/";
    filename += card.rank;
    filename.push_back(card.suit);
    filename += ".png";

    Fl_PNG_Image* original = new Fl_PNG_Image(filename.c_str());
    if (original->w() > 0 && original->h() > 0) {
        img = (Fl_PNG_Image*)original->copy(w, h);
        delete original;
        this->image(img);
    } else {
        delete original;
        img = nullptr;
        std::string label_text = std::string(card.rank) + card.suit;
        this->copy_label(label_text.c_str());
    }

    box(FL_FLAT_BOX);
}

int CardWidget::handle(int event)  
{
    switch(event) {
        case FL_PUSH:
            if (Fl::event_button() == FL_LEFT_MOUSE) {
                dragging = true;
                drag_start_x = Fl::event_x();
                drag_start_y = Fl::event_y();
                original_x = x();
                original_y = y();

                box(FL_BORDER_BOX);
                redraw();
                return 1;
            }
            break;

        case FL_DRAG:
            if (dragging) {
                int dx = Fl::event_x() - drag_start_x;
                int dy = Fl::event_y() - drag_start_y;

                position(original_x + dx, original_y + dy);

                // Highlight drop target
                if (game_window) {
                    game_window->HighlightDropTarget(Fl::event_x_root(), Fl::event_y_root());
                }

                parent()->redraw();
                return 1;
            }
            break;

        case FL_RELEASE:
            if (dragging && Fl::event_button() == FL_LEFT_MOUSE) {
                dragging = false;
                box(FL_FLAT_BOX);

                int drop_x = Fl::event_x_root();
                int drop_y = Fl::event_y_root();

                // Try to play the card
                if (game_window) {
                    if (!game_window->TryPlayCard(card_data, drop_x, drop_y)) {
                        // Failed to play - return to original position
                        ReturnToOriginal();
                    } else {
                        // Success - hide this card
                        hide();
                    }
                    game_window->ClearDropTargetHighlight();
                } else {
                    ReturnToOriginal();
                }

                redraw();
                return 1;
            }
            break;

        case FL_ENTER:
            box(FL_THIN_UP_BOX);
            redraw();
            return 1;

        case FL_LEAVE:
            if (!selected && !dragging) {
                box(FL_FLAT_BOX);
                redraw();
            }
            return 1;
    }

    return Fl_Box::handle(event);
}


void CardWidget::ReturnToOriginal() 
{
    position(original_x, original_y);
    parent()->redraw();
}

void CardWidget::setSelected(bool sel) 
{
    selected = sel;
    if (selected) {
        box(FL_BORDER_BOX);
    } else {
        box(FL_FLAT_BOX);
    }
    redraw();
}

/*=============================================*/
/*================ GameWindow =================*/
/*=============================================*/
GameWindow::GameWindow(Server& srv, AppState& st)
        : Fl_Window(1500, 1000, srv.GetGameState() ? srv.GetGameState()->GetGameName() : "Game"),
          server(srv), state(st), game(srv.GetGameState())
{
    if(game == nullptr) {
        return;
    }
    this->begin();
    initUI();
    clearUI();
    this->end();
    this->show();
}

void GameWindow::initUI()
{
    game = server.GetGameState();
    if(game == nullptr) {
        return;
    }
    
    this->begin();
    
    // Player list on the left
    int startX = 20;
    int startY = 20;
    int boxHeight = 30;
    int n = 0;
    
    /*for (int i = 0; i < game->GetPlayersCount(); ++i) {
        if(server.GetProfile().id == game->GetPlayer(i).id) {
            continue;
        }
        n++;
        std::string player_info = std::string(game->GetPlayer(i).name) +
                                 " (" + std::to_string(game->GetPlayerCardsCount(i)) + ")";
        
        // Store the label string for later cleanup
        char* label = strdup(player_info.c_str());
        player_labels.push_back(label); // Add std::vector<char*> player_labels to class
        
        Fl_Box* player_box = new Fl_Box(startX, startY + n * (boxHeight + 5),
                                       150, boxHeight, label);
        player_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        player_boxes.push_back(player_box);
    }*/
    if (game) {
        for (int i = 0; i < game->GetPlayersCount(); ++i) {
            std::string player_info = std::string(game->GetPlayer(i).name) + 
                                     " (" + std::to_string(game->GetPlayerCardsCount(i)) + ")";
            Fl_Box* player_box = new Fl_Box(startX, startY + i * (boxHeight + 5), 
                                           150, boxHeight, strdup(player_info.c_str()));
            player_box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            player_boxes.push_back(player_box);
        }
        
        std::string trump_text = "Trump: ";
        trump_text += game->GetTrumpSuit();
        trump_box = new Fl_Box(startX, startY + game->GetPlayersCount() * (boxHeight + 5) + 20,
                              150, boxHeight, strdup(trump_text.c_str()));
        trump_box->box(FL_BORDER_BOX);
        trump_box->labelfont(FL_BOLD);
        trump_box->color(FL_RED);
    }

    /*
    // Trump suit
    std::string trump_text = "Trump: ";
    trump_text += game->GetTrumpSuit();
    trump_label_str = strdup(trump_text.c_str()); // Add char* trump_label_str to class
    
    trump_box = new Fl_Box(startX, startY + (game->GetPlayersCount()-1) * (boxHeight + 5) + 20,
                          150, boxHeight, trump_label_str);
    trump_box->box(FL_BORDER_BOX);
    trump_box->labelfont(FL_BOLD);
    trump_box->color(FL_RED)*/;
    
    // Game status
    status_label = new Fl_Box(200, 20, 500, 40, "Waiting for game to start...");
    status_label->labelfont(FL_BOLD);
    status_label->labelsize(16);
    
    if((game->GetPlayersCount() > 1) && (server.GetPlayerId() == game->GetPlayer(0).id)) {
        start_btn = new Fl_Button(200, 80, 500, 40, "Start");
        start_btn->callback(start_callback, this);
    } else {
        start_btn = nullptr;
    }
    

    // DROP ZONE
    int dropZoneW = 800;
    int dropZoneH = 350;
    int dropZoneX = (w() - dropZoneW) / 2;
    int dropZoneY = 250;
    
    drop_zone = new Fl_Box(dropZoneX, dropZoneY, dropZoneW, dropZoneH);
    drop_zone->box(FL_BORDER_BOX);
    drop_zone->labelsize(20);
    drop_zone->labelcolor(FL_DARK_GREEN);
    drop_zone->color(fl_rgb_color(240, 255, 240));
    drop_zone->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    UpdateDropZoneLabel();
        
    // Action buttons
    int btnY = 750;
    int btnW = 100;
    int btnH = 30;
    int btnSpacing = 10;
    int btnStartX = (w() - (btnW * 4 + btnSpacing * 3)) / 2;
    
    attack_btn = new Fl_Button(btnStartX, btnY, btnW, btnH, "Attack");
    attack_btn->callback(attack_callback, this);
    attack_btn->color(FL_RED);
    attack_btn->labelcolor(FL_WHITE);
    
    defend_btn = new Fl_Button(btnStartX + btnW + btnSpacing, btnY, btnW, btnH, "Defend");
    defend_btn->callback(defend_callback, this);
    defend_btn->color(FL_BLUE);
    defend_btn->labelcolor(FL_WHITE);
    
    take_btn = new Fl_Button(btnStartX + (btnW + btnSpacing) * 2, btnY, btnW, btnH, "Take");
    take_btn->callback(take_callback, this);
    take_btn->color(FL_YELLOW);
    
    pass_btn = new Fl_Button(btnStartX + (btnW + btnSpacing) * 3, btnY, btnW, btnH, "Pass");
    pass_btn->callback(pass_callback, this);
    pass_btn->color(FL_GREEN);

    this->end();
    this->redraw();
}

void GameWindow::clearUI()
{
    this->begin();
    
    // Delete player boxes
    for (auto* box : player_boxes) {
        if(box) {
            this->remove(box); // IMPORTANT: remove from parent first
            delete box;
        }
    }
    player_boxes.clear();
    
    // Delete trump box
    if (trump_box) {
        this->remove(trump_box); // IMPORTANT: remove from parent first
        delete trump_box;
        trump_box = nullptr;
    }
    
    // Delete other widgets
    if (status_label) {
        this->remove(status_label);
        delete status_label;
        status_label = nullptr;
    }
    
    if (start_btn) {
        this->remove(start_btn);
        delete start_btn;
        start_btn = nullptr;
    }
    
    if (take_btn) {
        this->remove(take_btn);
        delete take_btn;
        take_btn = nullptr;
    }
    
    if (pass_btn) {
        this->remove(pass_btn);
        delete pass_btn;
        pass_btn = nullptr;
    }
    
    this->end();
    this->redraw();
}


void GameWindow::UpdateUI() 
{
    game = server.GetGameState();
    if(game == nullptr) {
        return;
    }

    if (game->GetGameId() != -1) {
        this->begin();
        updatePlayerCards();
        updateTableCards();
        updatePlayerInfo();
        updateStatus();
        this->end();
        this->redraw();
    }
}


void GameWindow::updatePlayerCards() 
{
    // Remove old cards
    for (auto* card : player_cards) {
        remove(card);
        delete card;
    }
    player_cards.clear();
    
    Game *g = server.GetGameState();
    
    int startX = 250;
    int y = 850;
    int cardW = 70;
    int cardH = 100;
    int spacing = 10;
    
    int totalWidth = g->GetHand().size() * (cardW + spacing) - spacing;
    startX = (w() - totalWidth) / 2;
    
    
    for (size_t i = 0; i < g->GetHand().size(); ++i) {
        CardWidget* cw = new CardWidget(
            startX + i * (cardW + spacing), 
            y, 
            g->GetHand()[i], 
            cardW, cardH,
            &server,
            this  // Pass game window reference
        );
        player_cards.push_back(cw);
    }
    
    redraw();

}


void GameWindow::updateTableCards() 
{
    // Remove old cards
    for (auto* card : table_cards) {
        remove(card);
        delete card;
    }
    table_cards.clear();
    highlighted_target = nullptr;
    
    if (!game) return;
    
    int tableStartX = (w() - game->GetTableCount() * 140) / 2;
    int tableY = 400;
    int cardW = 70;
    int cardH = 100;
    
    
    for (int i = 0; i < (game->GetTableCount()); ++i) {
        int x = tableStartX + i * 140;
        
        // Attacking card
        if ((game->GetTableAttack().size() != 0) && (game->GetTableAttack()[i].rank[0] != '\0')) {
            CardWidget* attack_card = new CardWidget(
                x, tableY, 
                game->GetTableAttack()[i], 
                cardW, cardH,
                &server,
                this
            );
            
            // Highlight undefended cards
            if ((game->GetTableDefense().size() > static_cast<size_t>(i)) && (game->GetTableDefense()[i].rank[0] == '\0')) {
                attack_card->color(fl_rgb_color(255, 220, 220)); // Light red
            }
            
            table_cards.push_back(attack_card);
        }
        
        // Defending card
        if ((game->GetTableDefense().size() > static_cast<size_t>(i)) && (game->GetTableDefense()[i].rank[0] != '\0')) {
            CardWidget* defense_card = new CardWidget(
                x + 50, tableY + 30, 
                game->GetTableDefense()[i], 
                cardW, cardH,
                &server,
                this
            );
            defense_card->color(fl_rgb_color(220, 255, 220)); // Light green
            table_cards.push_back(defense_card);
        }
    }
    
    // Update drop zone visibility
    if (game->GetTableCount() > 0) {
        drop_zone->hide();
    } else {
        drop_zone->show();
    }
    
    UpdateDropZoneLabel();
    redraw();
}



void GameWindow::updatePlayerInfo() {
    if(game == nullptr) {
        return;
    }

    for (size_t i = 0; i < player_boxes.size() && i < (size_t)game->GetPlayersCount(); ++i) {
        if(server.GetProfile().id == game->GetPlayer(i).id) {
            continue;
        }
        
        std::string player_info = std::string(game->GetPlayer(i).name) + 
                                 " (" + std::to_string(game->GetPlayer(i).cards_count) + ")";
        player_boxes[i]->copy_label(player_info.c_str());
    }

}


void GameWindow::updateStatus() {
    if(game == nullptr) {
        return;
    }
    int btnY = 750;
    int btnW = 100;
    int btnH = 30;
    int btnSpacing = 10;
    int btnStartX = (w() - (btnW * 4 + btnSpacing * 3)) / 2;

    this->begin();

    std::string status = "Attacker: ";
    if (game->GetCurrentAttacker() >= 0 && game->GetCurrentAttacker() < game->GetPlayersCount()) {
        status += game->GetPlayer(game->GetCurrentAttacker()).name;
    }
    status += " | Defender: ";
    if (game->GetCurrentDefender() >= 0 && game->GetCurrentDefender() < game->GetPlayersCount()) {
        status += game->GetPlayer(game->GetCurrentDefender()).name;
    }

    if(game->GetCurrentDefender() == server.GetProfile().id) {
        if (pass_btn) {
            this->remove(pass_btn);
            delete pass_btn;
            pass_btn = nullptr;
        }

        take_btn = new Fl_Button(btnStartX + (btnW + btnSpacing) * 2, btnY, btnW, btnH, "Take");
        take_btn->callback(take_callback, this);
        take_btn->color(FL_YELLOW);
    } else {
        if (take_btn) {
            this->remove(take_btn);
            delete take_btn;
            take_btn = nullptr;
        }

        pass_btn = new Fl_Button(btnStartX + (btnW + btnSpacing) * 2, btnY, btnW, btnH, "Pass");
        pass_btn->callback(pass_callback, this);
        pass_btn->color(FL_GREEN);
    }


    status_label->copy_label(status.c_str());

    this->end();
    this->redraw();
}

/*******************************************************************************************************************************************/

void GameWindow::UpdateDropZoneLabel() {
    if (!game) {
        drop_zone->copy_label("Waiting for game...");
        return;
    }
    
    // Check if there are undefended cards on table
    int undefended_count = 0;
    if (game->GetTableAttack().size() > game->GetTableDefense().size()) {
        undefended_count = game->GetTableAttack().size() - game->GetTableDefense().size();
    }
    
    if (undefended_count > 0) {
        std::string label = "Drop card on attacking card to defend\n";
        label += "(" + std::to_string(undefended_count) + " card";
        if (undefended_count > 1) label += "s";
        label += " need defense)";
        drop_zone->copy_label(label.c_str());
    } else if (game->GetTableCount() > 0) {
        drop_zone->copy_label("Drop cards here to throw in");
    } else {
        drop_zone->copy_label("Drop cards here to attack");
    }
}

// Highlight which table card we're hovering over
void GameWindow::HighlightDropTarget(int x, int y)
{
    CardWidget* new_target = nullptr;

    for (auto* card : table_cards) {
        if (!card || !card->visible())
            continue;

        if (x >= card->x() &&
            x <= card->x() + card->w() &&
            y >= card->y() &&
            y <= card->y() + card->h()) {

            new_target = card;
            break;
        }
    }

    if (new_target == highlighted_target)
        return;

    if (highlighted_target) {
        highlighted_target->box(FL_FLAT_BOX);
        highlighted_target->color(FL_WHITE);
        highlighted_target->redraw();
    }

    if (new_target) {
        new_target->box(FL_BORDER_BOX);
        new_target->color(FL_YELLOW);
        new_target->redraw();
    }

    highlighted_target = new_target;
}


void GameWindow::ClearDropTargetHighlight() {
    if (highlighted_target) {
        highlighted_target->box(FL_FLAT_BOX);
        highlighted_target->color(FL_WHITE);
        highlighted_target->redraw();
        highlighted_target = nullptr;
    }
}

// Try to play a card - returns true if successful
bool GameWindow::TryPlayCard(const Card& card, int drop_x, int drop_y) {
    if (!game) return false;
    

    // Check if dropped in general table area
    int drop_x_local = drop_x;
    int drop_y_local = drop_y;
    
    // Is it in drop zone area?
    if (drop_x_local < drop_zone->x() || 
        drop_x_local > drop_zone->x() + drop_zone->w() ||
        drop_y_local < drop_zone->y() || 
        drop_y_local > drop_zone->y() + drop_zone->h()) {
        return false; // Outside drop zone
    }
    
    // Determine if this is attack or defense
    std::string msg;
    
    // Check if we're defending (dropped on specific attack card)
    CardWidget* target_card = FindTableCardAt(drop_x, drop_y);
    if((game->GetCurrentDefender() == server.GetProfile().id) && (target_card == nullptr)) {
        return false;
    }
    
    std::vector<Card> &attack_table = game->GetTableAttack();
    
    if ((game->GetCurrentDefender() == server.GetProfile().id)/* && (attack_table[0])*/) {
        // Defending against specific card
        Card target = target_card->getCard();
        
        // Find index of this attack card
        int target_index = -1;
        for (int i = 0; i < game->GetTableCount(); ++i) {
            if ((attack_table.size() > static_cast<size_t>(i)) &&
                (strcmp(attack_table[i].rank, target.rank) == 0) &&
                (attack_table[i].suit == target.suit)) {
                target_index = i;
                break;
            }
        }
        
        if ((static_cast<size_t>(target_index) >= 0) && (static_cast<size_t>(target_index) <= game->GetTableDefense().size()) 
            /*&& (game->GetTableDefense()[target_index].rank[0] == '\0')*/) {
            // This attack card is not defended yet
            msg = "#defend ";
            msg += card.rank;
            msg += card.suit;

            msg += " ";
            msg += std::to_string(target_index);
            msg += '\n';

            printf("Defending against %s%c with %s%c\n", 
                   target.rank, target.suit, card.rank, card.suit);
        } else {
            fl_message("This card is already defended!");
            return false;
        }
    } else {
        // Attacking or throwing in
        msg = "#attack ";
        msg += card.rank;
        msg += card.suit;
        msg += '\n';
        
        //msg += ' ';
        //msg += server.GetProfile().id;

        printf("Attacking with %s%c\n", card.rank, card.suit);
    }
    
    if(msg.c_str() != nullptr) {
        server.send_string(msg.c_str());
    }
    return true;
}

// Find which table card is at these coordinates
CardWidget* GameWindow::FindTableCardAt(int x, int y)
{
    for (auto* card : table_cards) {
        if (!card || !card->visible())
            continue;

        int cx = card->x();
        int cy = card->y();
        int cw = card->w();
        int ch = card->h();

        if (x >= cx - 20 && x <= cx + cw + 20 &&
            y >= cy - 20 && y <= cy + ch + 20) {

            Card c = card->getCard();

            for (int i = 0; i < game->GetTableCount(); ++i) {
                if ((game->GetTableAttack().size() > static_cast<size_t>(i)) &&
                    (strcmp(game->GetTableAttack()[i].rank, c.rank) == 0) &&
                    (game->GetTableAttack()[i].suit == c.suit)) {
                    return card;
                }
            }
        }
    }
    return nullptr;
}

/*******************************************************************************************************************************************/

void GameWindow::start_callback(Fl_Widget* w, void* data) 
{
    GameWindow* gw = static_cast<GameWindow*>(data);
    gw->performStart();
}

void GameWindow::attack_callback(Fl_Widget* w, void* data) 
{
    GameWindow* gw = static_cast<GameWindow*>(data);
    gw->performAttack();
}


void GameWindow::defend_callback(Fl_Widget* w, void* data) 
{
    GameWindow* gw = static_cast<GameWindow*>(data);
    gw->performDefend();
}


void GameWindow::take_callback(Fl_Widget* w, void* data) 
{
    GameWindow* gw = static_cast<GameWindow*>(data);
    gw->performTake();
}


void GameWindow::pass_callback(Fl_Widget* w, void* data) 
{
    GameWindow* gw = static_cast<GameWindow*>(data);
    gw->performPass();
}


void GameWindow::performStart() 
{
    server.send_string("#game_start");
    if (start_btn) {
        remove(start_btn);
        delete start_btn;
        start_btn = nullptr;
    }
    redraw();
    
}


void GameWindow::performAttack() 
{
   // Collect selected cards
   std::string msg = "#attack";
   bool has_cards = false;

   for (auto* card_widget : player_cards) {
       if (card_widget->isSelected()) {
           Card c = card_widget->getCard();
           msg += " ";
           msg += c.rank;
           msg += c.suit;
           has_cards = true;
       }
   }

   if (has_cards) {
       server.send_string(msg.c_str());
   } else {
       fl_message("Select cards to attack!");
   }
}

void GameWindow::performDefend() 
{
    // Collect selected cards for defense
    std::string msg = "#defend";
    bool has_cards = false;

    for (auto* card_widget : player_cards) {
        if (card_widget->isSelected()) {
            Card c = card_widget->getCard();
            msg += " ";
            msg += c.rank;
            msg += c.suit;
            has_cards = true;
        }
    }

    if (has_cards) {
        server.send_string(msg.c_str());
    } else {
        fl_message("Select cards to defend!");
    }
}


void GameWindow::performTake() 
{
    // Take cards
    server.send_string("#take");
}


void GameWindow::performPass() 
{
    // Pass turn
    server.send_string("#pass");
    server.GetGameState()->GetTableAttack().clear();

    server.GetGameState()->GetTableDefense().clear();
}

// Function to create game window
Fl_Window* createGameWindow(Server& server, AppState& state/*, Game *game*/) 
{
    return new GameWindow(server, state/*, game*/);
}


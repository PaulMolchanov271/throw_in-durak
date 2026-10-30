#include "../ui/client.hpp"
#include "../ui/game_table.hpp"
#include "../ui/windows.hpp"
#include <cassert>

static Fl_Widget* labeled(Fl_Group* group, const char* text) {
    for (int i = 0; i < group->children(); ++i) {
        Fl_Widget* widget = group->child(i);
        if (widget->label() && !strcmp(widget->label(), text)) return widget;
    }
    return nullptr;
}
int main() {
    AppState state;
    auto* game = new Game(0, "Test lobby", 0, false);
    Server server(state, game);
    char alice[] = "Alice";
    server.GetProfile().name = alice;
    state.mainMenuWin = createMainMenuWindow(state, server);
    assert(state.profileWin && state.profileOutput);
    state.gameWin = new GameWindow(server, state);
    state.current_state = state.gameWin;
    server.ProcessGameMessage("#player 0 Alice 0");
    server.ProcessGameMessage("#player 1 Bob 0");
    state.gameWin->initUI();
    state.gameWin->UpdateUI();
    assert(labeled(state.gameWin, "Start"));
    assert(labeled(state.gameWin, "Leave lobby"));
    server.ProcessGameMessage("#game_start % 2");
    server.ProcessGameMessage("#player 0 Alice 6");
    server.ProcessGameMessage("#player 1 Bob 6");
    server.ProcessGameMessage("#turn 0 1");
    server.ProcessGameMessage("#your_cards 6^ 7v 8% 9# 10^ Jv");
    server.ProcessGameMessage("#table 2");
    server.ProcessGameMessage("#pair 6^ 7^");
    // A fragmented snapshot must not index beyond received pairs.
    state.gameWin->UpdateUI();
    server.ProcessGameMessage("#pair 6v");
    state.gameWin->UpdateUI();
    assert(game->GetTableCount() == 2 && game->GetTableDefense().size() == 2);
    assert(game->GetTableDefense()[1].rank[0] == '\0');
    assert(game->GetHand().size() == 6);
    assert(labeled(state.gameWin, "Attack")->active());
    state.gameWin->AppendChat("<Alice> hello");
    int before = state.gameWin->children();
    for (int i = 0; i < 10; ++i) state.gameWin->UpdateUI();
    assert(state.gameWin->children() == before); // no accumulating widgets
    auto* chat = dynamic_cast<Fl_Multiline_Output*>(labeled(state.gameWin, "Lobby chat"));
    assert(chat && strstr(chat->value(), "hello"));
    server.ProcessGameMessage("#game_end 0");
    state.gameWin->UpdateUI();
    assert(!game->IsGameStarted() && game->Result().find("won") != std::string::npos);
    assert(!labeled(state.gameWin, "Attack")->active());
    assert(labeled(state.gameWin, "Restart"));
    server.ProcessGameMessage("#game_start # 2");
    state.gameWin->UpdateUI();
    assert(game->IsGameStarted() && game->GetHand().empty() && game->GetTableAttack().empty());
    server.ProcessGameMessage("#game_end 1");
    assert(game->Result().find("lost") != std::string::npos);
    server.ProcessGameMessage("#game_end -2");
    assert(game->Result().find("won") != std::string::npos);
    // Eight players and value copies must both be safe.
    game->SetPlayersCount(8); assert(game->GetPlayersCount() == 8);
    Game copy = *game; assert(!strcmp(copy.GetGameName(), "Test lobby"));
    delete state.gameWin; state.gameWin = nullptr;
    // Server alone owns the model (no double deletion after leaving).
    server.SetGame(new Game(1, "Next lobby", 0, false));
    delete state.mainMenuWin; delete state.profileWin;
    puts("Client lifecycle, table snapshots, chat and widget ownership tests passed");
}

# Throw-In Durak

A multiplayer card game with a C server and a C++/FLTK graphical client.
Runs on Linux; on Windows, use WSL with graphical application support (WSLg).

## Build and run

On Ubuntu / Debian:

```sh
sudo apt-get install build-essential libfltk1.3-dev libpng-dev python3
make
./server
```

In another terminal, from the project root:

```sh
./throw_in_durak
```

Launch multiple clients to play. The client connects to `127.0.0.1` on port `5555`.
Card images are loaded from `ui/PNG-cards-1.3`, so run the client from the project root.
For the terminal client, run `make client_c && ./client_c`.

## Gameplay and features

- Log in, create an account, or join as a guest.
- Create a lobby or join one through **List of games**. Lobby names support up to 49 characters without spaces. Enable **Require password** to set a password (1-64 UTF-8 bytes). Joining a protected lobby prompts for its password. An incorrect password prevents entry; you can retry or cancel.
- The lobby host (the first player) can press **Start** once at least two players have joined. A lobby supports up to eight players.
- Click a card to select it for **Attack** / **Defend**. You can also drag a card onto the table, or onto an attacking card to defend against it. The **Defend** button uses the selected card against the first undefended card.
- **This project's winning rule:** a match ends immediately when a player legally plays their last card, even if cards remain in the deck. That player receives a win, and all other players receive a loss.
- The left side displays the trump card image and **Deck: N cards**, the number of cards remaining in the deck. This includes the bottom trump card until it is drawn. The counter updates after cards are drawn.
- When a match ends, the result is displayed and gameplay actions are disabled. The host can press **Restart** to reset the deck, hands, and table while keeping the participants and chat.
- **Leave lobby**, or closing the game window, returns to the main menu. Leaving or disconnecting during a match counts as a loss for the departing player and a win for everyone else. The match ends, and the remaining players can start another one. Empty lobbies are removed; if the host leaves, the next player becomes the host.
- When a participant leaves or disconnects, the chat displays a system message with their name.
- The chat on the right is available before, during, and after a match. Press **Send** or Enter to send a message. Only participants in the same lobby receive it. Messages are limited to 240 UTF-8 bytes.
- **Profile** in the main menu shows wins and losses. Press **Close** to close the profile window. Account statistics are stored in `stats.txt` in the server's working directory; guest statistics last only for the current session. An account cannot be logged in from multiple sessions at once.

The game uses a 52-card deck and deals six cards to each player at the start. Additional attacking cards must match a rank already on the table. A round allows at most six attacking cards, and no more than the defender can cover. Only the attacker can finish a round, and only after every attacking card has been defended. A defender who takes the cards skips their next turn as attacker.

## Checks

```sh
make test
# Check for memory errors and undefined behavior:
make clean
make test SANITIZE='-fsanitize=address,undefined -fno-omit-frame-pointer'
# Graphical client state regression tests:
make test-ui
```

Server tests cover playing the last card while the deck is nonempty, invalid moves, restarting, leaving, host transfer, and taking cards. TCP tests cover chat, profile persistence across server restarts, disconnections, multiple commands in one packet, and oversized input lines. Port 5555 must be available.

## Protocol

Commands and responses are separated by newlines. Main commands after login:

| Command | Action |
| --- | --- |
| `#list` | List lobbies; the response ends with `#list_end` |
| `#create NAME [PASSWORD]`, `#join ID [PASSWORD]` | Create or join a lobby |
| `#game_start`, `#restart` | Start a match (host only, at least two participants) |
| `#attack 6^ 6v` | Attack or add attacking cards |
| `#defend 7^ 0` | Defend against the card in pair 0 |
| `#take`, `#pass` | Take the cards on the table or finish a fully defended round |
| `#chat TEXT` | Send a message to lobby participants |
| `#profile` | Request the player's name, wins, and losses |
| `#leave` | Leave the lobby while keeping the connection open |

Each lobby entry returned by `#list` includes a fifth field indicating password protection (0/1), without exposing the password. `#password_required` and `#password_incorrect` request a password or another attempt. `#deck CARD COUNT` sends the exact trump card and the remaining deck size.

`#game_end ID` contains the **winner's ID**, not the loser's. Values `-1` and `-2` indicate a loss or a win caused by a participant leaving, respectively. `#left` confirms that the player has left; `#lobby COUNT` begins an update of the lobby roster and player indices.

## Limitations

The existing `userdata.txt` format stores passwords in plain text, and the TCP connection is not encrypted. This project is intended for local or educational use; do not reuse real passwords. Account and statistics files are excluded from Git.

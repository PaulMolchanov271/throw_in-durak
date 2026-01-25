import os
import re

# Путь к папке с картами
folder = "/home/dodaz/Desktop/C/Networks_and_Protocols/throw_in_durak/ui/PNG-cards-1.3"

# Сопоставление мастей
suits_map = {
    "spades": "^",
    "hearts": "v",
    "diamonds": "%",
    "clubs": "#"
}

# Сканируем файлы
for filename in os.listdir(folder):
    old_path = os.path.join(folder, filename)
    if not os.path.isfile(old_path):
        continue

    name = filename.lower().replace("'", "").replace("2_of_", "2").replace("3_of_", "3") \
                            .replace("4_of_", "4").replace("5_of_", "5").replace("6_of_", "6") \
                            .replace("7_of_", "7").replace("8_of_", "8").replace("9_of_", "9") \
                            .replace("10_of_", "10").replace("ace_of_", "A").replace("jack_of_", "J") \
                            .replace("queen_of_", "Q").replace("king_of_", "K")

    # Определяем масть по имени
    suit = None
    for key in suits_map:
        if key in name:
            suit = suits_map[key]
            break
    if suit is None:
        # если масть уже в виде символа (^v%#), оставляем
        match = re.search(r'[\^v%#]', name)
        if match:
            suit = match.group(0)
        else:
            print(f"Не удалось определить масть: {filename}")
            continue

    # Извлекаем ранг
    rank_match = re.match(r'(2|3|4|5|6|7|8|9|10|J|Q|K|A)', name)
    if not rank_match:
        print(f"Не удалось определить ранг: {filename}")
        continue
    rank = rank_match.group(0)

    new_name = f"{rank}{suit}.png"
    new_path = os.path.join(folder, new_name)

    print(f"{filename} → {new_name}")
    os.rename(old_path, new_path)


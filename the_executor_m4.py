import socket
import struct
import time
import sys

SERVER_IP = "127.0.0.1"
SERVER_PORT = 1234

class Executioner:
    def __init__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(1.0)
        self.last_game_id = None
    
    def send_and_read(self, payload, description):
        print(f"\n[ATAK] {description}")
        self.sock.sendto(payload, (SERVER_IP, SERVER_PORT))
        try:
            data, _ = self.sock.recvfrom(1024)
            self.analyze_response(data)
        except socket.timeout:
            print("  [!] Brak odpowiedzi! (Serwer to zignorowal lub PADL MARTWY)")

    def analyze_response(self, data):
        if len(data) == 14 and data[12] == 255:
            err_idx = data[13]
            print(f"  [TARCZA] Serwer zablokowal pakiet! MSG_WRONG_MSG (Blad na indeksie: {err_idx})")
            return

        if len(data) >= 14:
            game_id, p_a, p_b, status, max_pawn = struct.unpack('!IIIBB', data[:14])
            map_bytes = data[14:]
            
            self.last_game_id = game_id # Zapisujemy ID dynamicznie!
            
            status_str = {0: "WAITING", 1: "TURN_A", 2: "TURN_B", 3: "WIN_A", 4: "WIN_B"}.get(status, "UNKNOWN")
            
            print(f"  [STAN] Gra: {game_id} | {status_str} | Bity: {map_bytes.hex()}")
        else:
            print(f"  [?] Dziwna odpowiedz: {len(data)} bajtow.")

def run_carnage():
    ex = Executioner()
    print("=== THE EXECUTIONER v2: ROZPOCZYNAM RZEZ SERWERA ===")
    
    print("\n--- FAZA 1: INFILTRACJA (Zalozenie gry) ---")
    ex.send_and_read(struct.pack('!BI', 0, 100), "Gracz A (ID 100) zaklada gre")
    
    game_id = ex.last_game_id
    if game_id is None:
        print("\n[FATAL] Serwer nie zwrocil ID gry. Koncze test.")
        return
        
    ex.send_and_read(struct.pack('!BI', 0, 200), "Gracz B (ID 200) dolacza do gry")
    
    print("\n--- FAZA 2: ZLAMANIE ZASAD LOGIKI ---")
    
    # Atak poza turą
    payload_move1_B = struct.pack('!BIIB', 1, 200, game_id, 0)
    ex.send_and_read(payload_move1_B, "Czasowy paradoks: Gracz B probuje ruszac sie w Turze A")
    
    # Hijacking przez hakera (ID 666) - powinien wrócić błąd na indeksie 1 (player_id)
    payload_hijack = struct.pack('!BII', 3, 666, game_id)
    ex.send_and_read(payload_hijack, "Haker (ID 666) wysyla MSG_KEEP_ALIVE")
    
    print("\n--- FAZA 3: ATAK NA PAMIEC RAM (SEGFAULT TESTER) ---")
    
    # Specyfikacja: zły pion nie zwraca MSG_WRONG_MSG, lecz jest ignorowany (zwraca stan gry)
    payload_out_of_bounds = struct.pack('!BIIB', 1, 100, game_id, 255)
    ex.send_and_read(payload_out_of_bounds, "Bufor Overflow: Gracz A probuje zbic piona nr 255")
    
    payload_move2_edge = struct.pack('!BIIB', 2, 100, game_id, 4)
    ex.send_and_read(payload_move2_edge, "Edge Zapper: MSG_MOVE_2 na pionie nr 4 (zahacza o pion 5)")

    print("\n--- FAZA 4: BRUTALNA ROZGRYWKA ---")
    
    payload_move_A1 = struct.pack('!BIIB', 1, 100, game_id, 0)
    ex.send_and_read(payload_move_A1, "Legalny ruch A: Zbija pion 0. Tura dla B.")
    
    payload_move_B_dead = struct.pack('!BIIB', 1, 200, game_id, 0)
    ex.send_and_read(payload_move_B_dead, "Nekromancja: Gracz B probuje zbic JUZ ZBITEGO piona 0")

    payload_move_B_legal = struct.pack('!BIIB', 2, 200, game_id, 1)
    ex.send_and_read(payload_move_B_legal, "Legalny ruch B: MSG_MOVE_2 na pionach 1 i 2. Tura dla A.")

    payload_giveup_A = struct.pack('!BII', 4, 100, game_id)
    ex.send_and_read(payload_giveup_A, "Kapitulacja: Gracz A wysyla MSG_GIVE_UP")

    payload_move_after_win = struct.pack('!BIIB', 1, 200, game_id, 4)
    ex.send_and_read(payload_move_after_win, "Kopanie lezacego: Gracz B probuje zbic piona po zakonczeniu gry")

if __name__ == "__main__":
    run_carnage()

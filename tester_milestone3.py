import socket
import struct
import time

SERVER_IP = "127.0.0.1"
SERVER_PORT = 1234

def send_join_and_listen(sock, player_id):
    print(f"\n--- Wysylam MSG_JOIN (Player ID: {player_id}) ---")
    
    # Format !BI: 1 bajt (typ) + 4 bajty (player_id)
    payload = struct.pack('!BI', 0, player_id)
    
    sock.sendto(payload, (SERVER_IP, SERVER_PORT))
    
    try:
        data, addr = sock.recvfrom(1024)
        print(f"[Odebrano] {len(data)} bajtow od serwera.")
        
        # Oczekujemy MSG_GAME_STATE (Naglowek 14 bajtow + mapa bitowa)
        if len(data) >= 14:
            # Poprawny format naglowka: !IIIBB
            # 3x unsigned int (game_id, p_a, p_b), 2x unsigned char (status, max_pawn)
            game_id, player_a, player_b, status, max_pawn = struct.unpack('!IIIBB', data[:14])
            
            print(f"Game ID: {game_id}, Player A: {player_a}, Player B: {player_b}, Status: {status}, MaxPawn: {max_pawn}")
            
            # Wypisujemy odczytaną mapę bitową
            map_bytes = data[14:]
            print(f"Bity mapy (hex): {map_bytes.hex()}")
            
    except socket.timeout:
        print("[Blad] Brak odpowiedzi od serwera w ciagu 2 sekund.")

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    
    # Krok 1: Podłącza się Gracz 10
    send_join_and_listen(sock, 10)
    
    time.sleep(1)
    
    # Krok 2: Podłącza się Gracz 99 do tej samej gry
    send_join_and_listen(sock, 99)
    
    print("\nCzekam 6 sekund, zeby sprowokowac Garbage Collector serwera...")
    time.sleep(6)
    
    # Krok 3: Próba dołączenia po usunięciu gry
    send_join_and_listen(sock, 42)

if __name__ == "__main__":
    main()

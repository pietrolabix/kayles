import socket
import struct
import time
import os
import random

SERVER_IP = "127.0.0.1"
SERVER_PORT = 1234

def send_and_forget(sock, payload, description):
    print(f"[*] Atak: {description}")
    sock.sendto(payload, (SERVER_IP, SERVER_PORT))
    time.sleep(0.1) # Dajemy serwerowi ulamek sekundy na reakcje w logach

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    
    print("=== ROZPOCZYNAM SZTURM NA SERWER ===\n")

    # 1. Pakiet za krótki (tylko 1 bajt zamiast 5)
    send_and_forget(sock, struct.pack('!B', 0), "Za krotki MSG_JOIN (1 bajt)")

    # 2. Pakiet za długi (9 bajtów zamiast 5)
    send_and_forget(sock, struct.pack('!BII', 0, 10, 9999), "Za dlugi MSG_JOIN (9 bajtow)")

    # 3. Zły typ komunikatu (np. 99 zamiast 0)
    send_and_forget(sock, struct.pack('!BI', 99, 10), "Nieznany typ komunikatu (99)")

    # 4. Nielegalny Player ID (ID = 0, specyfikacja tego zakazuje)
    send_and_forget(sock, struct.pack('!BI', 0, 0), "Player ID = 0")

    # 5. Całkowite śmieci (Fuzzing)
    print("[*] Atak: Fuzzing - 100 losowych pakietow UDP...")
    for _ in range(100):
        random_length = random.randint(1, 20)
        random_bytes = os.urandom(random_length)
        sock.sendto(random_bytes, (SERVER_IP, SERVER_PORT))
    time.sleep(0.5)

    print("\n=== SPRAWDZENIE CZY SERWER ZYJE ===")
    print("[*] Wysylam poprawny MSG_JOIN (Gracz Ratunkowy 777)...")
    valid_payload = struct.pack('!BI', 0, 777)
    sock.sendto(valid_payload, (SERVER_IP, SERVER_PORT))
    
    try:
        data, addr = sock.recvfrom(1024)
        print(f"[SUKCES] Serwer przezyl! Odebrano {len(data)} bajtow odpowiedzi.")
    except socket.timeout:
        print("[PORAZKA] Serwer nie odpowiedzial. Prawdopodobnie padl (Segfault / Zawieszenie).")

if __name__ == "__main__":
    main()

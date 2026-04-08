import subprocess
import socket
import threading
import time
import struct

CLIENT_BIN = "./kayles_client"
FAKE_SERVER_PORT = 9999

def run_client(args):
    try:
        # Uruchamiamy klienta i przechwytujemy co wypluwa na stdout i stderr
        result = subprocess.run([CLIENT_BIN] + args, capture_output=True, text=True, timeout=3)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "SKRYPT ZAWIESIL SIE NA ZAWSZE (Brak obslugi SO_RCVTIMEO?)"

# Złośliwy serwer, który odsyła spreparowane błędy
def fake_server_thread():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", FAKE_SERVER_PORT))
    sock.settimeout(3.0)
    
    try:
        data, addr = sock.recvfrom(1024)
        # Odsyłamy MSG_WRONG_MSG: 12 bajtów starych danych + status(255) + error_index(7)
        wrong_msg = bytearray(14)
        wrong_msg[0:min(12, len(data))] = data[:min(12, len(data))]
        wrong_msg[12] = 255 # Status bledu
        wrong_msg[13] = 7   # Wymyslony indeks bledu
        
        sock.sendto(wrong_msg, addr)
    except socket.timeout:
        pass
    finally:
        sock.close()

def main():
    print("=== THE CLIENT EXECUTIONER: TESTY KULOOODPORNOSCI ===")

    tests = [
        ("Brak parametrow", [], 1),
        ("Brak portu", ["-a", "127.0.0.1", "-t", "2", "-m", "0/100"], 1),
        ("Port poza zakresem (70000)", ["-a", "127.0.0.1", "-p", "70000", "-t", "2", "-m", "0/100"], 1),
        ("Zly adres IP", ["-a", "999.999.999", "-p", "1234", "-t", "2", "-m", "0/100"], 1),
        ("Litery w porcie (stoi exception)", ["-a", "127.0.0.1", "-p", "abc", "-t", "2", "-m", "0/100"], 1),
        ("Pusty komunikat", ["-a", "127.0.0.1", "-p", "1234", "-t", "2", "-m", ""], 1),
        ("Zly format komunikatu (litery)", ["-a", "127.0.0.1", "-p", "1234", "-t", "2", "-m", "a/b/c"], 1),
        ("Za malo pol dla MSG_JOIN", ["-a", "127.0.0.1", "-p", "1234", "-t", "2", "-m", "0"], 1),
        ("Nieznany typ komunikatu (99)", ["-a", "127.0.0.1", "-p", "1234", "-t", "2", "-m", "99/100"], 1),
    ]

    print("\n--- FAZA 1: ATAKI NA CLI (PARSER) ---")
    passed = 0
    for name, args, expected_code in tests:
        code, out, err = run_client(args)
        if code == expected_code:
            print(f"[OK] {name} -> Kod {code}")
            passed += 1
        else:
            print(f"[BLAD] {name} -> Oczekiwano {expected_code}, otrzymano {code}. Stderr: {err.strip()}")

    print(f"\nWynik Fazy 1: {passed}/{len(tests)} testow zdanych.")

    print("\n--- FAZA 2: TEST TIMEOUTU (GLUCHY SERWER) ---")
    # Pytamy zamkniety port, klient ma zaczekac 1 sekunde i wyjsc kodem 0 z komunikatem
    code, out, err = run_client(["-a", "127.0.0.1", "-p", "55555", "-t", "1", "-m", "0/100"])
    if code == 0 and "Timeout" in out:
        print("[OK] Klient poprawnie obsluzyl brak odpowiedzi (Kod 0 + Komunikat Timeout).")
    else:
        print(f"[BLAD] Zla obsluga timeoutu! Kod: {code}, Wyjscie: {out.strip()}")

    print("\n--- FAZA 3: TEST PARSOWANIA BLEDOW (MSG_WRONG_MSG) ---")
    server_t = threading.Thread(target=fake_server_thread)
    server_t.start()
    time.sleep(0.1) # Dajmy mu wstac
    
    code, out, err = run_client(["-a", "127.0.0.1", "-p", str(FAKE_SERVER_PORT), "-t", "2", "-m", "0/100"])
    server_t.join()
    
    if code == 0 and "BLEDNY KOMUNIKAT" in out and "indeksie: 7" in out:
        print("[OK] Klient poprawnie zdekodowal MSG_WRONG_MSG od serwera!")
    else:
        print(f"[BLAD] Zle parsowanie bledu! Kod: {code}, Wyjscie: {out.strip()}")

    print("\n=== ZAKONCZONO ===")

if __name__ == "__main__":
    main()

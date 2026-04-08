#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>

#include "protocol.hpp"
#include "bitmap.hpp"

struct GameSession {
    uint32_t game_id;
    uint32_t player_a_id;
    uint32_t player_b_id;
    uint8_t  status;
    uint8_t  max_pawn;
    std::vector<uint8_t> pawn_row;
    std::chrono::steady_clock::time_point last_activity;
    struct sockaddr_in addr_a;
    struct sockaddr_in addr_b;
};

class KaylesServer {
private:
    int sockfd;
    uint16_t port;
    std::string address;
    int server_timeout_sec;
    
    std::vector<uint8_t> initial_pawn_row;
    uint8_t initial_max_pawn;

    uint32_t next_game_id = 1;
    std::unordered_map<uint32_t, GameSession> games;
    std::optional<uint32_t> waiting_game_id;

public:
    KaylesServer(const std::string& addr, uint16_t p, int timeout_sec, const std::string& start_row) 
        : sockfd(-1), port(p), address(addr), server_timeout_sec(timeout_sec) {
        initial_pawn_row = BitmapEngine::parse_cli_row(start_row, initial_max_pawn);
    }

    ~KaylesServer() {
        if (sockfd != -1) close(sockfd);
    }

    void init() {
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) throw std::runtime_error("Blad socket");

        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            throw std::runtime_error("Blad bind");
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::cout << "Serwer v4 nasluchuje. Port: " << port << std::endl;
    }

    void run() {
        uint8_t buffer[1024];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (true) {
            std::memset(&client_addr, 0, sizeof(client_addr));
            ssize_t received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);
            auto now = std::chrono::steady_clock::now();

            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    run_garbage_collector(now);
                    continue;
                } else if (errno == EINTR) break;
                else break;
            }

            if (received < 1) continue;

            uint8_t msg_type = buffer[0];

            if (msg_type == MSG_JOIN) {
                handle_join(buffer, received, client_addr, now);
            } else if (msg_type >= MSG_MOVE_1 && msg_type <= MSG_GIVE_UP) {
                handle_game_action(buffer, received, client_addr, now, msg_type);
            } else {
                // Nieznany typ - wysyłamy MSG_WRONG_MSG (błąd na indeksie 0, czyli msg_type)
                send_wrong_msg(buffer, received, client_addr, 0);
            }
        }
    }

private:
    void send_wrong_msg(uint8_t* buffer, ssize_t len, struct sockaddr_in& target_addr, uint8_t error_index) {
        MsgWrongMsg wrong;
        std::memset(&wrong, 0, sizeof(wrong));
        std::memcpy(wrong.invalid_msg_copy, buffer, std::min((ssize_t)12, len));
        wrong.status = 255;
        wrong.error_index = error_index;
        sendto(sockfd, &wrong, sizeof(wrong), 0, (struct sockaddr*)&target_addr, sizeof(target_addr));
        std::cout << "[Odrzucono] MSG_WRONG_MSG (Blad na indeksie " << (int)error_index << ")" << std::endl;
    }

    void handle_join(uint8_t* buffer, ssize_t len, struct sockaddr_in& client_addr, std::chrono::steady_clock::time_point now) {
        if (len != sizeof(MsgJoin)) return; // Ciche ignorowanie wg specyfikacji, lub mozna dac wrong_msg
        
        MsgJoin* msg = reinterpret_cast<MsgJoin*>(buffer);
        uint32_t player_id = ntohl(msg->player_id);
        if (player_id == 0) return; // Spec: "zawiera niezerową wartość w polu player_id"

        if (!waiting_game_id.has_value()) {
            uint32_t new_id = next_game_id++;
            GameSession session;
            session.game_id = new_id;
            session.player_a_id = player_id;
            session.player_b_id = 0;
            session.status = WAITING_FOR_OPPONENT;
            session.max_pawn = initial_max_pawn;
            session.pawn_row = initial_pawn_row;
            session.last_activity = now;
            session.addr_a = client_addr;

            games[new_id] = session;
            waiting_game_id = new_id;
            std::cout << "[JOIN] Nowa gra ID: " << new_id << " Gracz A: " << player_id << std::endl;
            send_game_state(new_id, client_addr);
        } else {
            uint32_t game_id = waiting_game_id.value();
            auto& session = games[game_id];
            session.player_b_id = player_id;
            session.status = TURN_A; // SPECYFIKACJA POPRAWKA: Po dołączeniu gracza B, czekamy na ruch gracza A (Kayles to gra A -> B -> A). UWAGA: W M3 miałeś TURN_B, ale logicznie to A zaczyna!
            session.last_activity = now;
            session.addr_b = client_addr;
            waiting_game_id.reset();
            std::cout << "[JOIN] Gra ID: " << game_id << " dolaczyl Gracz B: " << player_id << std::endl;
            send_game_state(game_id, client_addr);
        }
    }

    void handle_game_action(uint8_t* buffer, ssize_t len, struct sockaddr_in& client_addr, std::chrono::steady_clock::time_point now, uint8_t msg_type) {
        // Weryfikacja dlugosci w zaleznosci od typu
        size_t expected_len = 0;
        if (msg_type == MSG_MOVE_1 || msg_type == MSG_MOVE_2) expected_len = sizeof(MsgMove1);
        else if (msg_type == MSG_KEEP_ALIVE || msg_type == MSG_GIVE_UP) expected_len = sizeof(MsgKeepAlive);

        if (len != (ssize_t)expected_len) {
            send_wrong_msg(buffer, len, client_addr, 0); 
            return;
        }

        // Wydobycie wspolnych pol (zawsze na tych samych offsetach)
        uint32_t player_id = ntohl(*(uint32_t*)(buffer + 1));
        uint32_t game_id = ntohl(*(uint32_t*)(buffer + 5));

        if (games.find(game_id) == games.end()) {
            send_wrong_msg(buffer, len, client_addr, 5); // Blad na bajcie 5 (poczatek game_id)
            return;
        }

        auto& session = games[game_id];
        if (session.player_a_id != player_id && session.player_b_id != player_id) {
            send_wrong_msg(buffer, len, client_addr, 1); // Blad na bajcie 1 (poczatek player_id)
            return;
        }

        // Zapisujemy aktywnosc - gra zyje
        session.last_activity = now;
        
        // Aktualizujemy adres zwrotny gracza (mógł zmienić port, bo NAT)
        if (player_id == session.player_a_id) session.addr_a = client_addr;
        else session.addr_b = client_addr;

        if (msg_type == MSG_KEEP_ALIVE) {
            send_game_state(game_id, client_addr);
            return;
        }

        // KTO PROBUJE ZROBIC RUCH?
        bool is_player_a = (player_id == session.player_a_id);
        uint8_t expected_status = is_player_a ? TURN_A : TURN_B;

        // Czy to jego tura? Jesli nie (lub gra sie skonczyla), ruch jest NIELEGALNY, odeslij stan gry.
        if (session.status != expected_status) {
            send_game_state(game_id, client_addr);
            return;
        }

        if (msg_type == MSG_GIVE_UP) {
            session.status = is_player_a ? WIN_B : WIN_A;
            send_game_state(game_id, client_addr);
            return;
        }

        // Logika ruchow (MOVE_1 / MOVE_2)
        uint8_t pawn = buffer[9]; // Ostatni bajt
        bool valid_move = false;

        if (msg_type == MSG_MOVE_1) {
            if (is_pawn_standing(session, pawn)) {
                knock_down_pawn(session, pawn);
                valid_move = true;
            }
        } else if (msg_type == MSG_MOVE_2) {
            if (pawn < session.max_pawn && is_pawn_standing(session, pawn) && is_pawn_standing(session, pawn + 1)) {
                knock_down_pawn(session, pawn);
                knock_down_pawn(session, pawn + 1);
                valid_move = true;
            }
        }

        if (valid_move) {
            if (check_win_condition(session)) {
                session.status = is_player_a ? WIN_A : WIN_B;
            } else {
                session.status = is_player_a ? TURN_B : TURN_A; // Przekazanie tury
            }
        }

        send_game_state(game_id, client_addr);
    }

    // --- OPERACJE BITOWE ---
    bool is_pawn_standing(const GameSession& session, uint8_t pawn) {
        if (pawn > session.max_pawn) return false;
        size_t byte_idx = pawn / 8;
        size_t bit_shift = 7 - (pawn % 8);
        return (session.pawn_row[byte_idx] & (1 << bit_shift)) != 0;
    }

    void knock_down_pawn(GameSession& session, uint8_t pawn) {
        size_t byte_idx = pawn / 8;
        size_t bit_shift = 7 - (pawn % 8);
        session.pawn_row[byte_idx] &= ~(1 << bit_shift); // Maska negacji zeruje bit
    }

    bool check_win_condition(const GameSession& session) {
        // Sprawdzamy czy cala mapa to same zera (ignorujac nadmiarowe bity na koncu, 
        // ale poniewaz nadmiarowe sa zawsze zero z definicji, wystarczy sprawdzic bajty)
        for (uint8_t b : session.pawn_row) {
            if (b != 0) return false;
        }
        return true;
    }

    void send_game_state(uint32_t game_id, struct sockaddr_in& target_addr) {
        const auto& session = games[game_id];
        size_t packet_size = sizeof(MsgGameStateHeader) + session.pawn_row.size();
        std::vector<uint8_t> buffer(packet_size);

        MsgGameStateHeader* header = reinterpret_cast<MsgGameStateHeader*>(buffer.data());
        header->game_id = htonl(session.game_id);
        header->player_a_id = htonl(session.player_a_id);
        header->player_b_id = htonl(session.player_b_id);
        header->status = session.status;
        header->max_pawn = session.max_pawn;

        std::memcpy(buffer.data() + sizeof(MsgGameStateHeader), session.pawn_row.data(), session.pawn_row.size());
        sendto(sockfd, buffer.data(), packet_size, 0, (struct sockaddr*)&target_addr, sizeof(target_addr));
    }

    void run_garbage_collector(std::chrono::steady_clock::time_point now) {
        for (auto it = games.begin(); it != games.end(); ) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_activity);
            if (duration.count() > server_timeout_sec) {
                std::cout << "[GC] Usunieto gre ID: " << it->first << std::endl;
                if (waiting_game_id.has_value() && waiting_game_id.value() == it->first) waiting_game_id.reset();
                it = games.erase(it);
            } else {
                ++it;
            }
        }
    }
};

int main() {
    try {
        KaylesServer server("0.0.0.0", 1234, 10, "11101"); // Mapa: 5 pionów, max_pawn = 4. 
        server.init();
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

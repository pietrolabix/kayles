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

// Zależności z Milestone 1
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
        if (sockfd != -1) {
            close(sockfd);
            std::cout << "Zamykam gniazdo." << std::endl;
        }
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
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error("Blad setsockopt");
        }

        std::cout << "Serwer uruchomiony. Port: " << port << ", Timeout: " << server_timeout_sec << "s" << std::endl;
    }

    void run() {
        uint8_t buffer[1024];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (true) {
            std::memset(&client_addr, 0, sizeof(client_addr));
            ssize_t received = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                                        (struct sockaddr*)&client_addr, &client_len);

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
            } else {
                std::cout << "Odebrano nieznany/nieobslugiwany typ: " << (int)msg_type << std::endl;
            }
        }
    }

private:
    void handle_join(uint8_t* buffer, ssize_t len, struct sockaddr_in& client_addr, 
                     std::chrono::steady_clock::time_point now) {
        
        if (len != sizeof(MsgJoin)) {
            std::cout << "[Odrzucono] MSG_JOIN: Zla dlugosc pakietu." << std::endl;
            return;
        }

        MsgJoin* msg = reinterpret_cast<MsgJoin*>(buffer);
        uint32_t player_id = ntohl(msg->player_id);

        if (player_id == 0) return;

        std::cout << "[Log] Otrzymano MSG_JOIN od gracza ID: " << player_id << std::endl;

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
            
            std::cout << " -> Utworzono nowa gre: " << new_id << " (Czeka na przeciwnika)" << std::endl;
            send_game_state(new_id, client_addr);

        } else {
            uint32_t game_id = waiting_game_id.value();
            auto& session = games[game_id];

            session.player_b_id = player_id;
            session.status = TURN_B;
            session.last_activity = now;
            session.addr_b = client_addr;

            waiting_game_id.reset();
            
            std::cout << " -> Gracz " << player_id << " dolaczyl do gry " << game_id << ". Tura B." << std::endl;
            send_game_state(game_id, client_addr);
        }
    }

    void send_game_state(uint32_t game_id, struct sockaddr_in& target_addr) {
        if (games.find(game_id) == games.end()) return;
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

        sendto(sockfd, buffer.data(), packet_size, 0, 
               (struct sockaddr*)&target_addr, sizeof(target_addr));
    }

    void run_garbage_collector(std::chrono::steady_clock::time_point now) {
        for (auto it = games.begin(); it != games.end(); ) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_activity);
            
            if (duration.count() > server_timeout_sec) {
                std::cout << "[GC] Limit czasu! Usuwam przeterminowana gre ID: " << it->first << std::endl;
                if (waiting_game_id.has_value() && waiting_game_id.value() == it->first) {
                    waiting_game_id.reset();
                }
                it = games.erase(it);
            } else {
                ++it;
            }
        }
    }
};

int main() {
    try {
        // Parametry: Adres, Port, Timeout (5 sekund), Mapa bitowa
        KaylesServer server("0.0.0.0", 1234, 5, "11101");
        server.init();
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

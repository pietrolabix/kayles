#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>

#include "protocol.hpp"
#include "bitmap.hpp"

// Funkcja pomocnicza do cięcia stringa po ukośnikach
std::vector<uint32_t> parse_message_args(const std::string& msg_str) {
    std::vector<uint32_t> tokens;
    std::stringstream ss(msg_str);
    std::string item;
    while (std::getline(ss, item, '/')) {
        tokens.push_back(std::stoul(item));
    }
    return tokens;
}

int main(int argc, char* argv[]) {
    std::string address = "";
    int port = -1;
    std::string msg_str = "";
    int timeout_sec = -1;

    // 1. Rygorystyczne parsowanie argumentów z użyciem getopt
    int opt;
    while ((opt = getopt(argc, argv, "a:p:m:t:")) != -1) {
        switch (opt) {
            case 'a': address = optarg; break;
            case 'p': port = std::stoi(optarg); break;
            case 'm': msg_str = optarg; break;
            case 't': timeout_sec = std::stoi(optarg); break;
            default:
                std::cerr << "Nieprawidlowy parametr." << std::endl;
                return 1;
        }
    }

    if (address.empty() || port <= 0 || port > 65535 || msg_str.empty() || timeout_sec <= 0 || timeout_sec > 99) {
        std::cerr << "Blad: Brakujace lub nieprawidlowe parametry." << std::endl;
        return 1;
    }

    try {
        // 2. Budowanie pakietu binarnego
        std::vector<uint32_t> msg_args = parse_message_args(msg_str);
        if (msg_args.empty()) throw std::runtime_error("Pusty komunikat");

        uint8_t msg_type = msg_args[0];
        std::vector<uint8_t> send_buffer;

        // Składanie struktury w zaleznosci od typu
        if (msg_type == MSG_JOIN) {
            if (msg_args.size() != 2) throw std::runtime_error("MSG_JOIN wymaga 2 pol");
            MsgJoin msg;
            msg.msg_type = msg_type;
            msg.player_id = htonl(msg_args[1]);
            send_buffer.assign((uint8_t*)&msg, (uint8_t*)&msg + sizeof(msg));
        } 
        else if (msg_type == MSG_KEEP_ALIVE || msg_type == MSG_GIVE_UP) {
            if (msg_args.size() != 3) throw std::runtime_error("Oczekiwano 3 pol");
            MsgKeepAlive msg;
            msg.msg_type = msg_type;
            msg.player_id = htonl(msg_args[1]);
            msg.game_id = htonl(msg_args[2]);
            send_buffer.assign((uint8_t*)&msg, (uint8_t*)&msg + sizeof(msg));
        }
        else if (msg_type == MSG_MOVE_1 || msg_type == MSG_MOVE_2) {
            if (msg_args.size() != 4) throw std::runtime_error("Oczekiwano 4 pol");
            MsgMove1 msg; // Obie struktury maja ten sam rozmiar i uklad
            msg.msg_type = msg_type;
            msg.player_id = htonl(msg_args[1]);
            msg.game_id = htonl(msg_args[2]);
            msg.pawn = msg_args[3];
            send_buffer.assign((uint8_t*)&msg, (uint8_t*)&msg + sizeof(msg));
        } else {
            throw std::runtime_error("Nieznany msg_type");
        }

        // 3. Konfiguracja Gniazda i Wysylanie
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) throw std::runtime_error("Blad socket");

        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0) {
            close(sockfd);
            throw std::runtime_error("Nieprawidlowy adres IP");
        }

        struct timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        sendto(sockfd, send_buffer.data(), send_buffer.size(), 0, 
               (struct sockaddr*)&server_addr, sizeof(server_addr));

        // 4. Odbieranie odpowiedzi
        uint8_t recv_buffer[1024];
        ssize_t received = recvfrom(sockfd, recv_buffer, sizeof(recv_buffer), 0, nullptr, nullptr);

        close(sockfd);

        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Zgodnie ze specka: wypisuje stosowny komunikat i konczy z kodem 0
                std::cout << "Brak odpowiedzi od serwera (Timeout)." << std::endl;
                return 0; 
            } else {
                throw std::runtime_error("Blad odbierania danych");
            }
        }

        // 5. Parsowanie i rysowanie odpowiedzi
        if (received >= 14 && recv_buffer[12] == 255) {
            // Serwer odeslal nam MSG_WRONG_MSG (sprawdzamy status 255 na odpowiednim bicie)
            MsgWrongMsg* wrong = reinterpret_cast<MsgWrongMsg*>(recv_buffer);
            std::cout << "Odpowiedz Serwera: BLEDNY KOMUNIKAT (Odrzucono bajt o indeksie: " 
                      << (int)wrong->error_index << ")" << std::endl;
        } else if (received >= 14) {
            // To musi byc MSG_GAME_STATE
            MsgGameStateHeader* header = reinterpret_cast<MsgGameStateHeader*>(recv_buffer);
            uint32_t game_id = ntohl(header->game_id);
            uint32_t p_a = ntohl(header->player_a_id);
            uint32_t p_b = ntohl(header->player_b_id);
            
            std::string status_str;
            switch(header->status) {
                case WAITING_FOR_OPPONENT: status_str = "CZEKA NA PRZECIWNIKA"; break;
                case TURN_A: status_str = "TURA GRACZA A"; break;
                case TURN_B: status_str = "TURA GRACZA B"; break;
                case WIN_A: status_str = "WYGRAL GRACZ A"; break;
                case WIN_B: status_str = "WYGRAL GRACZ B"; break;
                default: status_str = "NIEZNANY";
            }

            std::cout << "--- STAN GRY ---" << std::endl;
            std::cout << "Gra ID   : " << game_id << std::endl;
            std::cout << "Status   : " << status_str << std::endl;
            std::cout << "Gracz A  : " << p_a << std::endl;
            std::cout << "Gracz B  : " << p_b << std::endl;

            // Rysowanie planszy uzywajac naszego silnika z Milestone 1
            std::vector<uint8_t> bitmap(recv_buffer + 14, recv_buffer + received);
            std::string board = BitmapEngine::to_string(bitmap, header->max_pawn);
            
            std::cout << "\nPLANSZA: " << board << std::endl;
            
            // Prosty interfejs graficzny dla piona
            std::cout << "INDEKSY: ";
            for(size_t i=0; i<=header->max_pawn; ++i) std::cout << (i%10);
            std::cout << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Blad klienta: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

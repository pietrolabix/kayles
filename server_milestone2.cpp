#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iomanip>

class KaylesServer {
private:
    int sockfd;
    uint16_t port;
    std::string address;

public:
    // Konstruktor tylko przypisuje parametry. Gniazdo inicjujemy w init()
    KaylesServer(const std::string& addr, uint16_t p) : sockfd(-1), port(p), address(addr) {}

    // Destruktor RAII - gwarantuje zamknięcie portu, nawet jak poleci wyjątek
    ~KaylesServer() {
        if (sockfd != -1) {
            close(sockfd);
            std::cout << "Zamykam gniazdo." << std::endl;
        }
    }

    void init() {
        // 1. Otwarcie gniazda UDP (SOCK_DGRAM)
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error(std::string("Blad socket: ") + strerror(errno));
        }

        // 2. Przygotowanie struktury adresu
        struct sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port); // Konwersja na Network Endian!
        
        if (address == "0.0.0.0") {
            server_addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_pton(AF_INET, address.c_str(), &server_addr.sin_addr) <= 0) {
                throw std::runtime_error("Nieprawidlowy adres IP");
            }
        }

        // 3. Zablokowanie portu dla naszego serwera
        if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            throw std::runtime_error(std::string("Blad bind: ") + strerror(errno));
        }

        // 4. Konfiguracja Timeoutu (SO_RCVTIMEO) - nasz "wyzwalacz" logiki czasu
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000; // 500 ms = pół sekundy
        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw std::runtime_error(std::string("Blad setsockopt SO_RCVTIMEO: ") + strerror(errno));
        }

        std::cout << "Serwer uruchomiony. Nasluchuje na " << address << ":" << port << std::endl;
    }

    void run() {
        uint8_t buffer[1024]; // Bufor na przychodzące datagramy
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        while (true) {
            std::memset(&client_addr, 0, sizeof(client_addr));
            
            // recvfrom blokuje na maksymalnie 500ms
            ssize_t received = recvfrom(sockfd, buffer, sizeof(buffer), 0, 
                                        (struct sockaddr*)&client_addr, &client_len);

            if (received < 0) {
                // Jeśli błąd to EAGAIN lub EWOULDBLOCK, to znaczy, że minęło 500ms i nikt nic nie wysłał.
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // TUTAJ W MILESTONE 3 WEJDZIE GARBAGE COLLECTOR (usuwanie starych gier)
                    std::cout << "Tyk..." << std::endl;
                    continue; // Wracamy do nasłuchiwania
                } else if (errno == EINTR) {
                    break; // Przerwanie systemowe (np. Ctrl+C)
                } else {
                    std::cerr << "Krytyczny blad recvfrom: " << strerror(errno) << std::endl;
                    break; // Poważny błąd gniazda, ubijamy serwer
                }
            }

            // Sukces: Odebrano prawdziwe dane
            std::cout << "\n[!] ODEBRANO " << received << " bajtow od " 
                      << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << std::endl;
            
            // Drukujemy hex dump, żeby widzieć dokładnie jak wjechały bajty
            std::cout << "Hex: ";
            for (ssize_t i = 0; i < received; ++i) {
                std::cout << std::hex << std::setw(2) << std::setfill('0') 
                          << static_cast<int>(buffer[i]) << " ";
            }
            std::cout << std::dec << "\n" << std::endl;
        }
    }
};

int main() {
    try {
        // Na potrzeby tego milestone'a hardkodujemy adres i port.
        // Prawdziwe ładowanie argumentów (CLI) zrobimy przy integracji.
        KaylesServer server("0.0.0.0", 1234);
        server.init();
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}

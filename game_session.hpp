#include <chrono>
#include <vector>

struct GameSession {
    uint32_t game_id;
    uint32_t player_a_id;
    uint32_t player_b_id;
    uint8_t  status;      // WAITING_FOR_OPPONENT, TURN_A, TURN_B, WIN_A, WIN_B
    uint8_t  max_pawn;
    std::vector<uint8_t> pawn_row;
    
    // Zegar do Garbage Collectora
    std::chrono::steady_clock::time_point last_activity;
    
    // Zapisujemy adresy graczy, żeby wiedzieć, gdzie odsyłać odpowiedzi
    struct sockaddr_in addr_a;
    struct sockaddr_in addr_b;
};

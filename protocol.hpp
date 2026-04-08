#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>

// Upewniamy się, że kompilator nie psuje struktur
#define PACKED __attribute__((packed))

// Typy komunikatów (zgodne ze specyfikacją)
enum MsgType : uint8_t {
    MSG_JOIN = 0,
    MSG_MOVE_1 = 1,
    MSG_MOVE_2 = 2,
    MSG_KEEP_ALIVE = 3,
    MSG_GIVE_UP = 4,
    MSG_GAME_STATE = 5, // Zakładamy własny identyfikator dla odpowiedzi (brak w specyfice klienta, ale serwer musi to wysłać)
    MSG_WRONG_MSG = 255 // W statusie błędu
};

enum GameStatus : uint8_t {
    WAITING_FOR_OPPONENT = 0,
    TURN_A = 1,
    TURN_B = 2,
    WIN_A = 3,
    WIN_B = 4
};

struct PACKED MsgJoin {
    uint8_t  msg_type;   // 0
    uint32_t player_id;
};

struct PACKED MsgMove1 {
    uint8_t  msg_type;   // 1
    uint32_t player_id;
    uint32_t game_id;
    uint8_t  pawn;
};

struct PACKED MsgMove2 {
    uint8_t  msg_type;   // 2
    uint32_t player_id;
    uint32_t game_id;
    uint8_t  pawn;
};

struct PACKED MsgKeepAlive {
    uint8_t  msg_type;   // 3
    uint32_t player_id;
    uint32_t game_id;
};

// Struktura stanu gry ma zmienną długość. 
// Definiujemy tylko stały nagłówek.
struct PACKED MsgGameStateHeader {
    // uint8_t msg_type; // Opcjonalnie, jeśli serwer musi tagować odpowiedź. Specyfikacja mówi tylko "odsyła MSG_GAME_STATE zawierający strukturę...".
    uint32_t game_id;
    uint32_t player_a_id;
    uint32_t player_b_id;
    uint8_t  status;
    uint8_t  max_pawn;
    // Poniżej w pamięci muszą znaleźć się bajty pawn_row
};

struct PACKED MsgWrongMsg {
    uint8_t invalid_msg_copy[12]; // Co najwyżej 12 bajtów
    uint8_t status;               // Zawsze 255
    uint8_t error_index;
};

#endif

#include <cassert>
#include <cstring>
#include "protocol.hpp"
#include "bitmap.hpp"

void test_struct_sizes() {
    // Te asercje ochronią Cię przed paddingiem kompilatora
    assert(sizeof(MsgJoin) == 5);
    assert(sizeof(MsgMove1) == 10);
    assert(sizeof(MsgMove2) == 10);
    assert(sizeof(MsgKeepAlive) == 9);
    assert(sizeof(MsgGameStateHeader) == 14);
    assert(sizeof(MsgWrongMsg) == 14);
    std::cout << "[OK] Rozmiary struktur sa idealne." << std::endl;
}

void test_bitmap_engine() {
    uint8_t max_pawn = 0;
    
    // Test 1: Poprawne parsowanie
    std::string input = "11101";
    auto bitmap = BitmapEngine::parse_cli_row(input, max_pawn);
    assert(max_pawn == 4);
    assert(bitmap.size() == 1);
    // '11101000' binarnie to 232 w dziesiętnym, E8 w hex
    assert(bitmap[0] == 0xE8); 

    // Test 2: Dekodowanie
    std::string output = BitmapEngine::to_string(bitmap, max_pawn);
    assert(input == output);

    // Test 3: Przekroczenie bajtu (10 bitów)
    std::string input2 = "1111111101";
    auto bitmap2 = BitmapEngine::parse_cli_row(input2, max_pawn);
    assert(max_pawn == 9);
    assert(bitmap2.size() == 2);
    assert(bitmap2[0] == 0xFF); // 11111111
    assert(bitmap2[1] == 0x40); // 01000000 -> pion nr 8 to 0, nr 9 to 1, reszta pusta
    
    std::cout << "[OK] Silnik mapy bitowej dziala perfekcyjnie." << std::endl;
}

void test_bitmap_failures() {
    uint8_t max_pawn = 0;
    bool caught = false;
    
    try { BitmapEngine::parse_cli_row("0111", max_pawn); } 
    catch (...) { caught = true; }
    assert(caught); // Zły start

    caught = false;
    try { BitmapEngine::parse_cli_row("111A1", max_pawn); } 
    catch (...) { caught = true; }
    assert(caught); // Zły znak
    
    std::cout << "[OK] Silnik poprawnie odrzuca smieciowe dane." << std::endl;
}

int main() {
    test_struct_sizes();
    test_bitmap_engine();
    test_bitmap_failures();
    std::cout << "Milestone 1 ZAKONCZONY SUKCESEM." << std::endl;
    return 0;
}

#include <iostream>
#include <vector>
#include <string>
#include <stdexcept>

class BitmapEngine {
public:
    // Konwertuje ciąg z CLI (np. "11101") na wektor bajtów
    static std::vector<uint8_t> parse_cli_row(const std::string& row_str, uint8_t& out_max_pawn) {
        if (row_str.empty() || row_str.length() > 256) {
            throw std::invalid_argument("Nieprawidlowa dlugosc mapy (1-256).");
        }
        if (row_str.front() == '0' || row_str.back() == '0') {
            throw std::invalid_argument("Skrajne piony musza byc rowne 1.");
        }

        out_max_pawn = static_cast<uint8_t>(row_str.length() - 1);
        size_t bytes_needed = (out_max_pawn / 8) + 1;
        std::vector<uint8_t> bitmap(bytes_needed, 0);

        for (size_t i = 0; i <= out_max_pawn; ++i) {
            if (row_str[i] == '1') {
                size_t byte_idx = i / 8;
                size_t bit_shift = 7 - (i % 8);
                bitmap[byte_idx] |= (1 << bit_shift);
            } else if (row_str[i] != '0') {
                throw std::invalid_argument("Niedozwolony znak w mapie bitowej.");
            }
        }
        return bitmap;
    }

    // Dekoduje z powrotem do stringa (przydatne dla klienta i logów)
    static std::string to_string(const std::vector<uint8_t>& bitmap, uint8_t max_pawn) {
        std::string result;
        for (size_t i = 0; i <= max_pawn; ++i) {
            size_t byte_idx = i / 8;
            size_t bit_shift = 7 - (i % 8);
            if (bitmap[byte_idx] & (1 << bit_shift)) {
                result += '1';
            } else {
                result += '0';
            }
        }
        return result;
    }
};

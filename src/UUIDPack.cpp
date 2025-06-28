#include "UUIDPack.h"
#include <random>
#include <chrono>
using namespace std;

UUIDPack::UUIDPack() {
    // Inicializa o gerador de números aleatórios com seed baseado no tempo
    setCustomA();
    setVer();
    setCustomB();
    setVar();
    setCustomC();
}

void UUIDPack::setCustomA() {
    // Gera 48 bits aleatórios para a primeira parte customizável
    random_device rd;
    mt19937_64 gen(rd());
    uniform_int_distribution<uint64_t> dis(0, (1ULL << 48) - 1);
    
    uint64_t random_value = dis(gen);
    custom_a = bitset<48>(random_value);
}

void UUIDPack::setCustomB() {
    // Gera 12 bits aleatórios para a segunda parte customizável
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<uint32_t> dis(0, (1U << 12) - 1);
    
    uint32_t random_value = dis(gen);
    custom_b = bitset<12>(random_value);
}

void UUIDPack::setCustomC() {
    // Gera 62 bits aleatórios para a terceira parte customizável
    random_device rd;
    mt19937_64 gen(rd());
    uniform_int_distribution<uint64_t> dis(0, (1ULL << 62) - 1);
    
    uint64_t random_value = dis(gen);
    custom_c = bitset<62>(random_value);
}

void UUIDPack::setVar() {
    // Define o variant como "10" (RFC 4122)
    var = bitset<2>(2); // "10" em binário = 2 em decimal
}

void UUIDPack::setVer() {
    // Define a versão como 4 (UUID aleatório)
    ver = bitset<4>(4); // "0100" em binário = 4 em decimal
}

bitset<128> UUIDPack::getUUID() {
    // Monta o UUID de 128 bits
    // Bits 127-80: custom_a (48 bits)
    // Bits 79-76: ver (4 bits)
    // Bits 75-64: custom_b (12 bits)
    // Bits 63-62: var (2 bits)
    // Bits 61-0: custom_c (62 bits)
    
    bitset<128> uuid;
    int pos = 0;
    
    // Bits 0-61: custom_c (62 bits)
    for (int i = 0; i < 62; i++) {
        uuid[pos++] = custom_c[i];
    }
    
    // Bits 62-63: var (2 bits)
    for (int i = 0; i < 2; i++) {
        uuid[pos++] = var[i];
    }
    
    // Bits 64-75: custom_b (12 bits)
    for (int i = 0; i < 12; i++) {
        uuid[pos++] = custom_b[i];
    }
    
    // Bits 76-79: ver (4 bits)
    for (int i = 0; i < 4; i++) {
        uuid[pos++] = ver[i];
    }
    
    // Bits 80-127: custom_a (48 bits)
    for (int i = 0; i < 48; i++) {
        uuid[pos++] = custom_a[i];
    }
    
    return uuid;
}
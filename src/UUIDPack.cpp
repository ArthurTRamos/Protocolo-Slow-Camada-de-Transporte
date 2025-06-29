#include "UUIDPack.h"
#include <random>
#include <chrono>
using namespace std;

UUIDPack::UUIDPack() {
    // Inicializa o gerador de números aleatórios com seed baseado no tempo
    setCustomA(bitset<48>(0), true);
    setVer(bitset<4>(0), true);
    setCustomB(bitset<12>(0), true);
    setVar(bitset<2>(0), true);
    setCustomC(bitset<62>(0), true);
}

void UUIDPack::setAllBy16Bytes(uint8_t sid[16]) {
    // Converter SID para um bitset de 128 bits para facilitar manipulação
    bitset<128> sid_bits;
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 8; j++) {
            sid_bits[i * 8 + j] = (sid[15 - i] >> j) & 1; // Little endian byte order
        }
    }
    
    // Extrair subcampos do SID (assumindo ordem sequencial dos bits)
    bitset<48> custom_a;
    bitset<4> ver;
    bitset<12> custom_b;
    bitset<2> var;
    bitset<62> custom_c;
    
    // custom_a: bits 0-47 (48 bits)
    for (int i = 0; i < 48; i++) {
        custom_a[i] = sid_bits[i];
    }

    setCustomA(custom_a, false);
    
    // ver: bits 48-51 (4 bits)
    for (int i = 0; i < 4; i++) {
        ver[i] = sid_bits[48 + i];
    }

    setVer(ver, false);
    
    // custom_b: bits 52-63 (12 bits)
    for (int i = 0; i < 12; i++) {
        custom_b[i] = sid_bits[52 + i];
    }

    setCustomB(custom_b, false);
    
    // var: bits 64-65 (2 bits)
    for (int i = 0; i < 2; i++) {
        var[i] = sid_bits[64 + i];
    }

    setVar(var, false);
    
    // custom_c: bits 66-127 (62 bits)
    for (int i = 0; i < 62; i++) {
        custom_c[i] = sid_bits[66 + i];
    }

    setCustomC(custom_c, false);
}


// Se random, então gera o campo de forma aleatória. Senão, atribui o campo ao valor passado
void UUIDPack::setCustomA(bitset<48> newCustom_a, bool random) {
    if(random) {
         // Gera 48 bits aleatórios para a primeira parte customizável
        random_device rd;
        mt19937_64 gen(rd());
        uniform_int_distribution<uint64_t> dis(0, (1ULL << 48) - 1);
        
        uint64_t random_value = dis(gen);
        custom_a = bitset<48>(random_value);
    }
    else {
        custom_a = newCustom_a;
    }
}

void UUIDPack::setCustomB(bitset<12> newCustom_b, bool random) {
    if(random) {
        // Gera 12 bits aleatórios para a segunda parte customizável
        random_device rd;
        mt19937 gen(rd());
        uniform_int_distribution<uint32_t> dis(0, (1U << 12) - 1);
        
        uint32_t random_value = dis(gen);
        custom_b = bitset<12>(random_value);
    }
    else {
        custom_b = newCustom_b;
    }
}

void UUIDPack::setCustomC(bitset<62> newCustom_c, bool random) {
    if(random) {
        // Gera 62 bits aleatórios para a terceira parte customizável
        random_device rd;
        mt19937_64 gen(rd());
        uniform_int_distribution<uint64_t> dis(0, (1ULL << 62) - 1);
        
        uint64_t random_value = dis(gen);
        custom_c = bitset<62>(random_value);
    }
    else {
        custom_c = newCustom_c;
    }
}

void UUIDPack::setVar(bitset<2> newVar, bool random) {
    if(random) 
        // Define o variant como "10" (RFC 4122)
        var = bitset<2>(2); // "10" em binário = 2 em decimal
    else
        var = newVar;
}

void UUIDPack::setVer(bitset<4> newVer, bool random) {
    if(random)
        // Define a versão como 4 (UUID aleatório)
        ver = bitset<4>(4); // "0100" em binário = 4 em decimal
    else
        ver = newVer;
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
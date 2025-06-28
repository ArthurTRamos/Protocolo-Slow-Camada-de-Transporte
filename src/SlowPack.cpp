#include "SlowPack.h"
using namespace std;

SlowPack::SlowPack() {
    sid.reset();
    flags.reset();
    sttl.reset();
    seqnum.reset();
    acknum.reset();
    window.reset();
    fid.reset();
    fo.reset();
    data.clear();
}

bool SlowPack::setSid(bitset<128> newSid) {
    sid = newSid;
    return true;
}

bool SlowPack::setSttl(bitset<27> newSttl) {
    sttl = newSttl;
    return true;
}

bool SlowPack::setFlags(bitset<5> newFlags) {
    flags = newFlags;
    return true;
}

bool SlowPack::setSeqnum(bitset<32> newSeqnum) {
    seqnum = newSeqnum;
    return true;
}

bool SlowPack::setAcknum(bitset<32> newAcknum) {
    if(flags[2] == 0)
        return false;

    acknum = newAcknum;
    return true;
}

bool SlowPack::setWindow(bitset<16> newWindow) {
    window = newWindow;
    return true;
}

bool SlowPack::setFid(bitset<8> newFid) {
    fid = newFid;
    return true;
}

bool SlowPack::setFo(bitset<8> newFo) {
    fo = newFo;
    return true;
}

bool SlowPack::setData(vector<uint8_t> newData) {
    if(newData.size() > 1440)
        return false;

    data.insert(data.end(), newData.begin(), newData.end());
    return true;
}

vector<uint8_t> SlowPack::getSlow() {
    vector<bool> packet_bits;
    
    // SID (128 bits) - Little endian por grupos de 8 bits
    // Dividir em 16 bytes, enviar do byte 0 ao 15 (menos significativo primeiro)
    for (int byte_idx = 0; byte_idx < 16; byte_idx++) {
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            int global_bit_idx = byte_idx * 8 + bit_idx;
            packet_bits.push_back(sid[global_bit_idx]);
        }
    }
    
    // STTL (27 bits) + FLAGS (5 bits) = 32 bits total
    // Organizar em grupos de 8 bits, enviando menos significativo primeiro
    vector<bool> sttl_flags_bits(32);
    
    // Preencher STTL (27 bits) - do índice 26 ao 0 (invertido)
    for (int i = 0; i < 27; i++) {
        sttl_flags_bits[i] = sttl[26 - i];
    }
    
    // Preencher FLAGS (5 bits) - do índice 0 ao 4 (não invertido, preenchido manualmente)
    for (int i = 0; i < 5; i++) {
        sttl_flags_bits[27 + i] = flags[i];
        //cout << "\n27+" << i << flags[i] << "\n";
    }
    
    // Aplicar ordenação por grupos de 8 bits (menos significativo primeiro)
    // Grupo 0: bits 0-7, Grupo 1: bits 8-15, Grupo 2: bits 16-23, Grupo 3: bits 24-31
    // Ordem desejada: [24-31], [16-23], [8-15], [0-7]
    vector<int> bit_order = {
        24, 25, 26, 27, 28, 29, 30, 31,  // bits 24-31 (grupo 3)
        16, 17, 18, 19, 20, 21, 22, 23,  // bits 16-23 (grupo 2)
        8, 9, 10, 11, 12, 13, 14, 15,    // bits 8-15 (grupo 1)
        0, 1, 2, 3, 4, 5, 6, 7           // bits 0-7 (grupo 0)
    };
    
    for (int bit_idx : bit_order) {
        packet_bits.push_back(sttl_flags_bits[bit_idx]);
    }
    
    // SEQNUM (32 bits) - Inverter bits + Little endian por grupos de 8 bits
    for (int byte_idx = 0; byte_idx < 4; byte_idx++) { // Byte 0 primeiro (menos significativo)
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            int global_bit_idx = byte_idx * 8 + (7 - bit_idx); // Inverter bits dentro do byte
            packet_bits.push_back(seqnum[global_bit_idx]);
        }
    }
    
    // ACKNUM (32 bits) - Inverter bits + Little endian por grupos de 8 bits
    if (flags[2] == 1) {
        for (int byte_idx = 0; byte_idx < 4; byte_idx++) { // Byte 0 primeiro (menos significativo)
            for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
                int global_bit_idx = byte_idx * 8 + (7 - bit_idx); // Inverter bits dentro do byte
                packet_bits.push_back(acknum[global_bit_idx]);
            }
        }
    } else {
        // Adicionar 32 bits zero com little endian
        for (int byte_idx = 0; byte_idx < 4; byte_idx++) { // Byte 0 primeiro (menos significativo)
            for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
                packet_bits.push_back(0);
            }
        }
    }
    
    // WINDOW (16 bits) - Inverter bits + Little endian por grupos de 8 bits
    for (int byte_idx = 0; byte_idx < 2; byte_idx++) { // Byte 0 primeiro (menos significativo)
        for (int bit_idx = 0; bit_idx < 8; bit_idx++) {
            int global_bit_idx = byte_idx * 8 + (7 - bit_idx); // Inverter bits dentro do byte
            packet_bits.push_back(window[global_bit_idx]);
        }
    }
    
    // FID (8 bits) - invertido (apenas 1 byte)
    for (int i = 7; i >= 0; i--) {
        packet_bits.push_back(fid[i]);
    }
    
    // FO (8 bits) - invertido (apenas 1 byte)
    for (int i = 7; i >= 0; i--) {
        packet_bits.push_back(fo[i]);
    }
    
    // DATA - cada byte individualmente (sem little endian entre bytes)
    for (uint8_t byte : data) {
        for (int i = 0; i < 8; i++) {
            packet_bits.push_back((byte >> i) & 1);
        }
    }
    
    // Converter bits para bytes
    vector<uint8_t> packet;
    for (size_t i = 0; i < packet_bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8 && (i + j) < packet_bits.size(); j++) {
            if (packet_bits[i + j]) {
                byte |= (1 << (7 - j));
            }
        }
        packet.push_back(byte);
    }
    
    return packet;
}
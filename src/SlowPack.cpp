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

vector<uint8_t> SlowPack::getSlow(bool nullData) {
    vector<bool> packet_bits;
    
    // SID (128 bits) - LSB à esquerda
    for (int i = 0; i < 128; i++) {
        packet_bits.push_back(sid[i]);
    }
    
    // FLAGS (5 bits) + STTL (27 bits) = 32 bits total
    // Escrever FLAGS primeiro, depois STTL
    
    // FLAGS (5 bits) - na ordem que vem
    for (int i = 0; i < 5; i++) {
        packet_bits.push_back(flags[4-i]);
    }
    
    // STTL (27 bits) - na ordem que vem
    for (int i = 0; i < 27; i++) {
        packet_bits.push_back(sttl[i]);
    }
    
    // SEQNUM (32 bits) - LSB à esquerda
    for (int i = 0; i < 32; i++) {
        packet_bits.push_back(seqnum[i]);
    }
    
    // ACKNUM (32 bits) - LSB à esquerda
    if (flags[2] == 1) {
        for (int i = 0; i < 32; i++) {
            packet_bits.push_back(acknum[i]);
        }
    } else {
        // Adicionar 32 bits zero
        for (int i = 0; i < 32; i++) {
            packet_bits.push_back(0);
        }
    }
    
    // WINDOW (16 bits) - LSB à esquerda
    for (int i = 0; i < 16; i++) {
        packet_bits.push_back(window[i]);
    }
    
    // FID (8 bits) - LSB à esquerda
    for (int i = 0; i < 8; i++) {
        packet_bits.push_back(fid[i]);
    }
    
    // FO (8 bits) - LSB à esquerda
    for (int i = 0; i < 8; i++) {
        packet_bits.push_back(fo[i]);
    }
    
    // DATA - se necessário
    if (!nullData) {
        for (uint8_t byte : data) {
            for (int i = 0; i < 8; i++) {
                packet_bits.push_back((byte >> i) & 1);
            }
        }
    }
    
    // Converter bits para bytes - CORREÇÃO AQUI
    vector<uint8_t> packet;
    for (size_t i = 0; i < packet_bits.size(); i += 8) {
        uint8_t byte = 0;
        for (int j = 0; j < 8 && (i + j) < packet_bits.size(); j++) {
            if (packet_bits[i + j]) {
                byte |= (1 << j);  // MUDANÇA: usar j ao invés de (7-j)
            }
        }
        packet.push_back(byte);
    }
    
    return packet;
}
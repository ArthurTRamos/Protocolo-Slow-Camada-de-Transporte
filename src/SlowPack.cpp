#include "SlowPack.h"
using namespace std;

SlowPack::SlowPack() {
    sid.reset();
    sttl.reset();
    flags.reset();
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
    if(newData.size() > 1440 || newData.size() < 0)
        return false;

    data.insert(data.end(), newData.begin(), newData.end());
    return true;
}

vector<uint8_t> SlowPack::getSlow() {
    vector<uint8_t> packet;
    
    // Adicionar SID
    for (int i = 15; i >= 0; i--) {
        uint8_t byte = 0;
        for (int j = 0; j < 8; j++) {
            if (sid[i * 8 + j]) {
                byte |= (1 << j);
            }
        }
        packet.push_back(byte);
    }
    
    // Adicionar STTL
    uint32_t sttl_value = 0;
    for (int i = 0; i < 27; i++) {
        if (sttl[i]) {
            sttl_value |= (1U << i);
        }
    }
    // Adicionar os 4 bytes do STTL (little-endian)
    packet.push_back(sttl_value & 0xFF);
    packet.push_back((sttl_value >> 8) & 0xFF);
    packet.push_back((sttl_value >> 16) & 0xFF);
    packet.push_back((sttl_value >> 24) & 0xFF);
    
    // Adicionar FLAGS (5 bits) + 3 bits de padding = 1 byte
    uint8_t flags_byte = 0;
    for (int i = 0; i < 5; i++) {
        if (flags[i]) {
            flags_byte |= (1 << i);
        }
    }
    
    packet.push_back(flags_byte);
    
    // Adicionar SEQNUM
    uint32_t seqnum_value = 0;
    for (int i = 0; i < 32; i++) {
        if (seqnum[i]) {
            seqnum_value |= (1U << i);
        }
    }

    packet.push_back(seqnum_value & 0xFF);
    packet.push_back((seqnum_value >> 8) & 0xFF);
    packet.push_back((seqnum_value >> 16) & 0xFF);
    packet.push_back((seqnum_value >> 24) & 0xFF);
    
    // Adicionar ACKNUM (32 bits = 4 bytes) - apenas se o flag ACK estiver setado
    if (flags[2] == 1) {  // Assumindo que o bit 2 é o flag ACK
        uint32_t acknum_value = 0;
        for (int i = 0; i < 32; i++) {
            if (acknum[i]) {
                acknum_value |= (1U << i);
            }
        }
        packet.push_back(acknum_value & 0xFF);
        packet.push_back((acknum_value >> 8) & 0xFF);
        packet.push_back((acknum_value >> 16) & 0xFF);
        packet.push_back((acknum_value >> 24) & 0xFF);
    }
    
    // Adicionar WINDOW (16 bits = 2 bytes)
    uint16_t window_value = 0;
    for (int i = 0; i < 16; i++) {
        if (window[i]) {
            window_value |= (1U << i);
        }
    }
    packet.push_back(window_value & 0xFF);
    packet.push_back((window_value >> 8) & 0xFF);
    
    // Adicionar FID (8 bits = 1 byte)
    uint8_t fid_byte = 0;
    for (int i = 0; i < 8; i++) {
        if (fid[i]) {
            fid_byte |= (1 << i);
        }
    }
    packet.push_back(fid_byte);
    
    // Adicionar FO (8 bits = 1 byte)
    uint8_t fo_byte = 0;
    for (int i = 0; i < 8; i++) {
        if (fo[i]) {
            fo_byte |= (1 << i);
        }
    }
    packet.push_back(fo_byte);
    
    // Adicionar DATA (até 1440 bytes)
    packet.insert(packet.end(), data.begin(), data.end());
    
    return packet;

}
#ifndef UUIDPack_h
#define UUIDPack_h

#include "UUIDPack.h"
#include <bits/stdc++.h>

using namespace std;

class SlowPack {
private:
    bitset<128> sid;
    bitset<27> sttl;
    bitset<5> flags;
    bitset<32> seqnum;
    bitset<32> acknum;
    bitset<16> window;
    bitset<8> fid;
    bitset<8> fo;
    vector<uint8_t> data;

public:
    SlowPack();

    void setSid();
    void setSttl();
    void setFlags();
    void setSeqnum();
    void setAcknum();
    void setWindow();
    void setFid();
    void setFo();
    void setData();

    vector<uint8_t> getSlow();
};

#endif
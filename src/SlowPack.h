#ifndef SlowPack_h
#define SlowPack_h

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

    bool setSid(bitset<128> sid);
    bool setSttl(bitset<27> newSttl);
    bool setFlags(bitset<5> newFlags);
    bool setSeqnum(bitset<32> newSeqnum);
    bool setAcknum(bitset<32> newAcknum);
    bool setWindow(bitset<16> newWindow);
    bool setFid(bitset<8> newFid);
    bool setFo(bitset<8> newFo);
    bool setData(vector<uint8_t> newData);

    vector<uint8_t> getSlow(bool nullData);
};

#endif
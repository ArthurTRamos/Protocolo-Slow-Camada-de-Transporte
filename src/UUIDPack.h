#ifndef UUIDPack_h
#define UUIDPack_h

#include <bits/stdc++.h>
using namespace std;


class UUIDPack {
private:
    bitset<48> custom_a;
    bitset<4> ver;
    bitset<12> custom_b;
    bitset<2> var;
    bitset<62> custom_c;

public:
    UUIDPack();

    void setCustomA(bitset<48> newCustom_a, bool random);
    void setCustomB(bitset<12> newCustom_b, bool random);
    void setCustomC(bitset<62> newCustom_c, bool random);
    void setVar(bitset<2> newVar, bool random);
    void setVer(bitset<4> newVer, bool random);
    void setAllBy128Bits(bitset<128> sid_bits);

    bitset<128> getUUID();
};

#endif
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

    void setCustomA();
    void setCustomB();
    void setCustomC();
    void setVar();
    void setVer();

    bitset<128> getUUID();
};

#endif
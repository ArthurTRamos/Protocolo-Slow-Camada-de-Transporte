#ifndef CONNECT_H
#define CONNECT_H

#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

using namespace std;

class UDPClient {
private:
    int sockfd;
    struct sockaddr_in server_addr;
    bool connected;
    
public:
    UDPClient();
    
    bool connectToServer(const string hostname, int port);
    bool sendMessage();
    bool disconect();
};

#endif
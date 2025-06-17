#include "ConnectCentral.h"

using namespace std;

UDPClient::UDPClient() : sockfd(-1), connected(false) {}

bool UDPClient::ConnectToServer(const string hostname, int port) {
    // Criar socket UDP
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        return false;
    
    // Resolver hostname
    struct hostent* host = gethostbyname(hostname.c_str());
    if (!host) {
        close(sockfd);
        return false;
    }
    
    // Configurar endereço do servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);
    
    connected = true;
    return true;
}
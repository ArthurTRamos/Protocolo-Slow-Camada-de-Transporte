#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "UUIDPack.h"
#include "SlowPack.h"

using namespace std;

constexpr int SLOW_PORT = 7033;
constexpr size_t BUFFER_SIZE = 1472;
constexpr int NUM_PACOTES = 1;
constexpr int ACK_TIMEOUT_MS = 1000;

UUIDPack session_id;
uint32_t session_seq = 0;
uint32_t session_ack = 0;
uint32_t session_sttl = 0;
uint16_t session_window = 1024;

atomic<bool> session_established(false);
atomic<bool> ack_received(false);
mutex session_mutex;

int sockfd;
sockaddr_in central_addr;

string bits_from_bytes(const vector<uint8_t>& data, size_t bit_offset, size_t bit_length) {
    string result;
    for (size_t i = 0; i < bit_length; ++i) {
        size_t total_bit = bit_offset + i;
        size_t byte_index = total_bit / 8;
        size_t bit_index = 7 - (total_bit % 8); // MSB primeiro
        result += ((data[byte_index] >> bit_index) & 1) ? '1' : '0';
    }
    return result;
}


void print_packet_precise_bits(const vector<uint8_t>& data, const string& direction) {
    cout << "[" << direction << "] " << data.size() << " bytes\n";

    size_t bit_offset = 0;

    cout << "sid:    " << bits_from_bytes(data, bit_offset, 128) << "\n";
    bit_offset += 128;

    cout << "flags:  " << bits_from_bytes(data, bit_offset, 5) << "\n";
    bit_offset += 5;

    cout << "sttl:   " << bits_from_bytes(data, bit_offset, 27) << "\n";
    bit_offset += 27;

    cout << "seqnum: " << bits_from_bytes(data, bit_offset, 32) << "\n";
    bit_offset += 32;

    cout << "acknum: " << bits_from_bytes(data, bit_offset, 32) << "\n";
    bit_offset += 32;

    cout << "window: " << bits_from_bytes(data, bit_offset, 16) << "\n";
    bit_offset += 16;

    cout << "fid:    " << bits_from_bytes(data, bit_offset, 8) << "\n";
    bit_offset += 8;

    cout << "fo:     " << bits_from_bytes(data, bit_offset, 8) << "\n";
    bit_offset += 8;

    size_t total_bits = data.size() * 8;
    if (bit_offset < total_bits) {
        cout << "data:   " << bits_from_bytes(data, bit_offset, total_bits - bit_offset) << "\n";
    }
}


// Função para extrair campos do pacote recebido (little endian)
void parse_received_packet(const uint8_t* buffer, size_t len) {
    if (len < 32) return;
    
    // Extrair SID (128 primeiros bits = 16 bytes)
    uint8_t sid[16];
    memcpy(sid, buffer, 16);
    
    // Extrair flags (5 bits)
    uint8_t flags_byte = buffer[16];
    uint8_t flags = flags_byte & 0x1F; // Máscara para pegar apenas os 5 bits menos significativos
    
    // Extrair STTL (27 bits) - spans across multiple bytes
    uint32_t sttl = 0;
    // Os 3 bits mais significativos do flags_byte (bits 5, 6, 7) são parte do STTL
    uint8_t sttl_part1 = (flags_byte >> 5) & 0x07; // 3 bits
    // Os próximos 24 bits (3 bytes completos) completam o STTL
    uint32_t sttl_part2 = 0;
    memcpy(&sttl_part2, buffer + 17, 3); // 3 bytes
    sttl_part2 = le32toh(sttl_part2) & 0x00FFFFFF; // Garantir apenas 24 bits
    
    // Combinar as partes do STTL (3 bits + 24 bits = 27 bits)
    sttl = (sttl_part1 << 24) | sttl_part2;
    
    // Extrair outros campos
    uint32_t seqnum, acknum;
    uint16_t window;
    
    memcpy(&seqnum, buffer + 20, 4);
    memcpy(&acknum, buffer + 24, 4);
    memcpy(&window, buffer + 28, 2);
    
    // Converter de little endian para host byte order
    seqnum = le32toh(seqnum);
    acknum = le32toh(acknum);
    window = le16toh(window);
    
    // Imprimir SID em hexadecimal
    cout << "[RECV] SID: ";
    for (int i = 0; i < 16; i++) {
        cout << hex << setw(2) << setfill('0') << static_cast<int>(sid[i]);
    }
    cout << endl;
    
    cout << "[RECV] Flags: " << bitset<5>(flags) 
         << " | STTL: " << dec << sttl
         << " | Seq: " << seqnum 
         << " | Ack: " << acknum 
         << " | Win: " << window << endl;
}


// Função para criar flags como bitset<5>
bitset<5> createFlags(bool connect, bool accept, bool ack, bool failed, bool revive) {
    bitset<5> flags;
    flags[0] = connect;
    flags[1] = accept;
    flags[2] = ack;
    flags[3] = failed;
    flags[4] = revive;
    return flags;
}


void send_connect() {
    bitset<128> uuid_para_debug(0);

    SlowPack pkt;
    pkt.setSid(uuid_para_debug);
    pkt.setFlags(createFlags(true, false, false, false, false));
    pkt.setSttl(bitset<27>(0));
    pkt.setSeqnum(bitset<32>(0));
    pkt.setAcknum(bitset<32>(0));
    pkt.setWindow(bitset<16>(session_window));
    pkt.setFid(bitset<8>(0));
    pkt.setFo(bitset<8>(0));
    pkt.setData(vector<uint8_t>());

    vector<uint8_t> packet_data = pkt.getSlow(true);

    cout << "[SEND] CONNECT | Seq: 0 | Ack: 0 | Win: " << session_window << endl;
    print_packet_precise_bits(packet_data, "OUT");

    cout << endl << endl;
    
    sendto(sockfd, packet_data.data(), packet_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));
}

void send_data_loop(int num_pacotes) {
    while (!session_established) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    for (int i = 0; i < num_pacotes; ++i) {
        ack_received = false;
        int tentativas = 0;

        while (!ack_received && tentativas < 3) {
            tentativas++;
            SlowPack pkt;
            
            {
                lock_guard<mutex> lock(session_mutex);
                pkt.setSid(session_id.getUUID());
                pkt.setFlags(createFlags(false, false, true, false, false));
                pkt.setSttl(bitset<27>(session_sttl));
                pkt.setSeqnum(bitset<32>(1));
                pkt.setAcknum(bitset<32>(session_seq));
                pkt.setWindow(bitset<16>(session_window));
                pkt.setFid(bitset<8>(0));
                pkt.setFo(bitset<8>(0));

                string msg = "Pacote " + to_string(i + 1);
                vector<uint8_t> data_vec(msg.begin(), msg.end());
                pkt.setData(data_vec);
            }

            vector<uint8_t> packet_data = pkt.getSlow(false);

            print_packet_precise_bits(packet_data, "OUT");

            
            cout << "[SEND] DATA #" << (i + 1) << " (try " << tentativas 
                 << ") | Seq: " << session_seq << " | Ack: " << session_ack << " | Tamanho: " << packet_data.size() << endl;
            //print_packet_hex(packet_data, "OUT");
            
            sendto(sockfd, packet_data.data(), packet_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));

            int elapsed = 0;
            while (!ack_received && elapsed < ACK_TIMEOUT_MS) {
                this_thread::sleep_for(chrono::milliseconds(10));
                elapsed += 10;
            }
        }

        if (!ack_received) {
            cout << "[ERROR] Pacote " << (i + 1) << " falhou após 3 tentativas" << endl;
            break;
        }

        {
            lock_guard<mutex> lock(session_mutex);
            session_seq++;
        }
    }

    // Enviar desconexão
    SlowPack pkt;
    {
        lock_guard<mutex> lock(session_mutex);
        pkt.setSid(session_id.getUUID());
        pkt.setFlags(createFlags(true, false, true, false, true));
        pkt.setSttl(bitset<27>(session_sttl));
        pkt.setSeqnum(bitset<32>(session_seq++));
        pkt.setAcknum(bitset<32>(session_ack));
        pkt.setWindow(bitset<16>(0));
        pkt.setFid(bitset<8>(0));
        pkt.setFo(bitset<8>(0));
        pkt.setData(vector<uint8_t>());
    }

    vector<uint8_t> packet_data = pkt.getSlow(false);
    cout << "[SEND] DISCONNECT | Seq: " << (session_seq - 1) << " | Flags: CONNECT+ACK+REVIVE" << endl;
    //print_packet_hex(packet_data, "OUT");
    sendto(sockfd, packet_data.data(), packet_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));

    this_thread::sleep_for(chrono::milliseconds(500));

    // Tentar reviver se STTL > 0
    {
        lock_guard<mutex> lock(session_mutex);
        if (session_sttl > 0) {
            SlowPack revive_pkt;
            revive_pkt.setSid(session_id.getUUID());
            revive_pkt.setFlags(createFlags(false, false, true, false, true));
            revive_pkt.setSttl(bitset<27>(session_sttl));
            revive_pkt.setSeqnum(bitset<32>(session_seq));
            revive_pkt.setAcknum(bitset<32>(session_ack));
            revive_pkt.setWindow(bitset<16>(session_window));
            revive_pkt.setFid(bitset<8>(0));
            revive_pkt.setFo(bitset<8>(0));
            revive_pkt.setData(vector<uint8_t>());

            vector<uint8_t> revive_data = revive_pkt.getSlow(false);
            cout << "[SEND] REVIVE | Seq: " << session_seq << " | STTL: " << session_sttl << endl;
            //print_packet_hex(revive_data, "OUT");
            sendto(sockfd, revive_data.data(), revive_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));
        }
    }
}


void receive_loop() {
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(central_addr);

    cout << "[INFO] Aguardando pacotes..." << endl;

    while (true) {
        ssize_t len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (sockaddr*)&central_addr, &addr_len);
        if (len <= 0) continue;
        
        cout << "Recebeu " << len << endl;

        vector<uint8_t> received_data(buffer, buffer + len);

        for (int i = 0; i < len; ++i)
            printf("%02X ", static_cast<uint8_t>(buffer[i]));
        printf("\n");

        //print_packet_hex(received_data, "IN");
        parse_received_packet((uint8_t*)buffer, len);

        if (len >= 32) {
            // Extrair SID (128 primeiros bits = 16 bytes)
            uint8_t sid[16];
            memcpy(&sid, buffer, 16);
            
            // Extrair flags (5 bits)
            uint8_t flags_byte = buffer[16];
            uint8_t flags = flags_byte & 0x1F; // Máscara para pegar apenas os 5 bits menos significativos
            
            // Extrair STTL (27 bits) - spans across multiple bytes
            uint32_t sttl = 0;
            // Os 3 bits mais significativos do flags_byte (bits 5, 6, 7) são parte do STTL
            uint8_t sttl_part1 = (flags_byte >> 5) & 0x07; // 3 bits
            // Os próximos 24 bits (3 bytes completos) completam o STTL
            uint32_t sttl_part2 = 0;
            memcpy(&sttl_part2, buffer + 17, 3); // 3 bytes
            sttl_part2 = le32toh(sttl_part2) & 0x00FFFFFF; // Garantir apenas 24 bits
            
            // Combinar as partes do STTL (3 bits + 24 bits = 27 bits)
            sttl = (sttl_part1 << 24) | sttl_part2;
            
            // Extrair outros campos
            uint32_t seqnum, acknum;
            uint16_t window;
            
            memcpy(&seqnum, buffer + 20, 4);
            memcpy(&acknum, buffer + 24, 4);
            memcpy(&window, buffer + 28, 2);
            
            // Converter de little endian para host byte order
            seqnum = le32toh(seqnum);
            acknum = le32toh(acknum);
            window = le16toh(window);
            /*uint8_t flags = buffer[16];
            uint32_t seqnum, acknum;
            uint16_t window;
            
            memcpy(&seqnum, buffer + 17, 4);
            memcpy(&acknum, buffer + 21, 4);
            memcpy(&window, buffer + 25, 2);
            
            seqnum = le32toh(seqnum);
            acknum = le32toh(acknum);
            window = le16toh(window);*/
            
            bitset<5> recv_flags(flags);
            bool is_accept = recv_flags[1];
            bool is_ack = recv_flags[2];
            bool is_failed = recv_flags[3];
            bool is_revive = recv_flags[4];

            // Captando dados do SID
            UUIDPack uuidAux;
            uuidAux.setAllBy16Bytes(sid);
            
            if (is_accept && !session_established) {
                lock_guard<mutex> lock(session_mutex);
                session_id = uuidAux;
                session_sttl = sttl;
                session_seq = seqnum;
                session_ack = acknum;
                session_window = window;
                session_established = true;
                cout << "[CONN] Sessão estabelecida!" << endl << endl;
            }
            else if (is_ack && session_established) {
                lock_guard<mutex> lock(session_mutex);
                session_ack = acknum;
                session_window = window;
                ack_received = true;
                cout << "[ACK] Confirmado!" << endl;
            }
            else if (is_accept && is_revive) {
                cout << "[REVIVE] Sessão revivida!" << endl;
                session_established = true;
            }
            else if (is_failed && is_revive) {
                cout << "[ERROR] Falha ao reviver sessão" << endl;
            }
        }
    }
}

int main() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Erro ao criar socket");
        return 1;
    }

    struct addrinfo hints{}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int status = getaddrinfo("slow.gmelodie.com", nullptr, &hints, &res);
    if (status != 0) {
        cerr << "getaddrinfo erro: " << gai_strerror(status) << endl;
        return 1;
    }

    memcpy(&central_addr, res->ai_addr, sizeof(struct sockaddr_in));
    central_addr.sin_port = htons(SLOW_PORT);
    freeaddrinfo(res);

    cout << "[START] Conectando a slow.gmelodie.com:" << SLOW_PORT << endl;

    send_connect();

    thread recv_thread(receive_loop);
    thread send_thread(send_data_loop, NUM_PACOTES);

    send_thread.join();
    recv_thread.detach();

    cout << "[END] Transmissão finalizada. Pressione Ctrl+C para sair." << endl;

    while (true) {
        this_thread::sleep_for(chrono::seconds(60));
    }

    return 0;
}
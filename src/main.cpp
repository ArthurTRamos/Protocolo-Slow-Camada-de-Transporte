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
constexpr int NUM_PACOTES = 3;
constexpr int ACK_TIMEOUT_MS = 1000;

// Variáveis globais de sessão - usando apenas bitset para consistência
bitset<128> session_sid = 0;
bitset<32> session_seq = 0;
bitset<32> session_ack = 0;
bitset<27> session_sttl = 0;
bitset<16> session_window = 1024;

atomic<bool> session_established(false);
atomic<bool> ack_received(false);
mutex session_mutex;

int sockfd;
sockaddr_in central_addr;

// Thread para decrementar STTL
atomic<bool> sttl_running(false);

void sttl_countdown() {
    while (sttl_running) {
        this_thread::sleep_for(chrono::seconds(1));
        if (sttl_running) {
            lock_guard<mutex> lock(session_mutex);
            if (session_sttl.to_ulong() > 0) {
                session_sttl = bitset<27>(session_sttl.to_ulong() - 1);
                cout << "[STTL] Decrementado para: " << session_sttl.to_ulong() << endl;
            }
        }
    }
}

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

void imprimirBitsDoPacote(const std::vector<uint8_t>& buffer) {
    std::cout << "[BITS - ORDEM REAL DE ENVIO]" << std::endl;

    int bit_idx = 0;
    auto printBits = [&](int numBits) {
        for (int b = 0; b < numBits; ++b, ++bit_idx) {
            uint8_t byte = buffer[bit_idx / 8];
            int bit = (byte >> (bit_idx % 8)) & 1;
            std::cout << bit;
        }
        std::cout << std::endl;
    };

    // Tamanhos em bits por ordem real de envio:
    printBits(128); // SID
    printBits(5);   // FLAGS
    printBits(27);  // STTL
    printBits(32);  // SEQ
    printBits(32);  // ACK
    printBits(16);  // WINDOW
    printBits(8);   // FID
    printBits(8);   // FO

    int total_bits = buffer.size() * 8;
    int remaining = total_bits - bit_idx;
    if (remaining > 0)
        printBits(remaining);

    std::cout << "[FIM DOS BITS] Total: " << total_bits << " bits" << std::endl;
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

    cout << "[RECV] SID (binário): ";
    for (int i = 0; i < 16; i++) {
        cout << bitset<8>(sid[i]).to_string() << " "; // 8 bits por byte
    }
    cout << endl;
    
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
    pkt.setWindow(bitset<16>(1472));
    pkt.setFid(bitset<8>(0));
    pkt.setFo(bitset<8>(0));
    pkt.setData(vector<uint8_t>());

    vector<uint8_t> packet_data = pkt.getSlow(true);
    imprimirBitsDoPacote(packet_data);

    cout << "[SEND] CONNECT | Seq: 0 | Ack: 0 | Win: " << session_window.to_ulong() << endl;

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
                pkt.setSid(session_sid);
                pkt.setFlags(createFlags(false, false, true, false, false));
                pkt.setSttl(session_sttl);
                pkt.setSeqnum(bitset<32>(session_seq.to_ulong()+1));
                pkt.setAcknum(session_ack);
                pkt.setWindow(session_window);
                pkt.setFid(bitset<8>(0));
                pkt.setFo(bitset<8>(0));

                string msg = "Pacote " + to_string(i + 1);
                vector<uint8_t> data_vec(msg.begin(), msg.end());
                pkt.setData(data_vec);
            }

            vector<uint8_t> packet_data = pkt.getSlow(false);
            imprimirBitsDoPacote(packet_data);
            
            cout << "[SEND] DATA #" << (i + 1) << " (try " << tentativas 
                 << ") | Seq: " << session_seq.to_ulong() << " | Ack: " << session_ack.to_ulong() << " | Tamanho: " << packet_data.size() << endl;
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
            session_seq = bitset<32>(session_seq.to_ulong() + 1);
        }
    }

    // Enviar desconexão
    SlowPack pkt;
    {
        lock_guard<mutex> lock(session_mutex);
        pkt.setSid(session_sid);
        pkt.setFlags(createFlags(true, true, true, false, false));
        pkt.setSttl(session_sttl);
        pkt.setSeqnum(bitset<32>(session_seq.to_ulong()+1));
        pkt.setAcknum(session_ack);
        pkt.setWindow(bitset<16>(0));
        pkt.setFid(bitset<8>(0));
        pkt.setFo(bitset<8>(0));
        pkt.setData(vector<uint8_t>());
    }

    vector<uint8_t> packet_data = pkt.getSlow(false);
    imprimirBitsDoPacote(packet_data);
    cout << "[SEND] DISCONNECT | Seq: " << (session_seq.to_ulong() - 1) << " | Flags: CONNECT+ACK+REVIVE" << endl;
    //print_packet_hex(packet_data, "OUT");
    sendto(sockfd, packet_data.data(), packet_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));

    this_thread::sleep_for(chrono::milliseconds(500));

    // Tentar reviver se STTL > 0
    {
        lock_guard<mutex> lock(session_mutex);
        if (session_sttl.to_ulong() > 0) {
            SlowPack revive_pkt;
            revive_pkt.setSid(session_sid);
            revive_pkt.setFlags(createFlags(false, false, true, false, true));
            revive_pkt.setSttl(session_sttl);
            revive_pkt.setSeqnum(session_seq);
            revive_pkt.setAcknum(session_ack);
            revive_pkt.setWindow(session_window);
            revive_pkt.setFid(bitset<8>(0));
            revive_pkt.setFo(bitset<8>(0));
            revive_pkt.setData(vector<uint8_t>());

            vector<uint8_t> revive_data = revive_pkt.getSlow(false);
            cout << "[SEND] REVIVE | Seq: " << session_seq.to_ulong() << " | STTL: " << session_sttl.to_ulong() << endl;
            //print_packet_hex(revive_data, "OUT");
            sendto(sockfd, revive_data.data(), revive_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));
        }
    }
    
    // Parar o countdown do STTL
    sttl_running = false;
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


// Após printar os hexadecimais:

    int bit_idx = 0;

    // SID (128 bits)
    for (int b = 0; b < 128; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // STTL (27 bits)
    for (int b = 0; b < 27; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // FLAGS (5 bits)
    for (int b = 0; b < 5; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // SeqNum (32 bits)
    for (int b = 0; b < 32; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // AckNum (32 bits)
    for (int b = 0; b < 32; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // Window (16 bits)
    for (int b = 0; b < 16; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // Fid (8 bits)
    for (int b = 0; b < 8; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // Fo (8 bits)
    for (int b = 0; b < 8; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

    // Já foram 256 bits (32 bytes), agora imprime o restante (data)
    int total_bits = len * 8;
    int data_bits = total_bits - bit_idx;

    for (int b = 0; b < data_bits; ++b, ++bit_idx) {
        uint8_t byte = buffer[bit_idx / 8];
        int bit = (byte >> (bit_idx % 8)) & 1;
        cout << bit;
    }
    cout << endl;

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

            if (true) {
                SlowPack p;

                // --- SID ---
                bitset<128> sid_bits;
                for (int i = 0; i < 16; ++i) {
                    for (int b = 0; b < 8; ++b) {
                        bool bit_val = (buffer[i] >> b) & 1;
                        sid_bits[i * 8 + b] = bit_val;
                    }
                }

                // --- Flags e STTL ---
                bitset<8> byte16_bits(buffer[16]);
                
                // Flags: bits 3 a 7 (flag 0 = bit 3, ..., flag 4 = bit 7)
                bitset<5> flags_bits;
                for (int i = 0; i < 5; ++i) {
                    flags_bits[i] = byte16_bits[i + 3];
                }

                // STTL: bits 0,1,2 do byte[16] (lembrando: bit 2 = menos significativo)
                uint32_t sttl_lsb = 0;
                sttl_lsb |= (byte16_bits[2] << 0);
                sttl_lsb |= (byte16_bits[1] << 1);
                sttl_lsb |= (byte16_bits[0] << 2);

                uint32_t sttl_msb = 0;
                sttl_msb |= buffer[17] << 0;
                sttl_msb |= buffer[18] << 8;
                sttl_msb |= buffer[19] << 16;

                uint32_t sttl_full = (sttl_msb << 3) | sttl_lsb;
                bitset<27> sttl_bits(sttl_full);

                // --- SeqNum ---
                uint32_t raw_seq;
                memcpy(&raw_seq, buffer + 20, 4);
                bitset<32> seq_bits(le32toh(raw_seq));

                // --- AckNum ---
                uint32_t raw_ack;
                memcpy(&raw_ack, buffer + 24, 4);
                bitset<32> ack_bits(le32toh(raw_ack));

                // --- Window ---
                uint16_t raw_win;
                memcpy(&raw_win, buffer + 28, 2);
                bitset<16> window_bits(le16toh(raw_win));

                // --- Fragment ID e Offset ---
                bitset<8> fid_bits(buffer[30]);
                bitset<8> fo_bits(buffer[31]);

                // --- Payload (se existir além dos 32 bytes) ---
                vector<uint8_t> payload;
                if (len > 32)
                    payload.insert(payload.end(), buffer + 32, buffer + len);

                // --- Setar no pacote ---
                p.setSid(sid_bits);
                p.setFlags(flags_bits);
                p.setSttl(sttl_bits);
                p.setSeqnum(seq_bits);
                p.setAcknum(ack_bits);
                p.setWindow(window_bits);
                p.setFid(fid_bits);
                p.setFo(fo_bits);
                p.setData(payload);

                // Serializar e imprimir
                vector<uint8_t> packed = p.getSlow(false);
                //print_packet_precise_bits(packed, "SLOW");

                // Atualizar variáveis globais de sessão
                {
                    lock_guard<mutex> lock(session_mutex);
                    session_sid = sid_bits;
                    session_sttl = sttl_bits;  // STTL sempre atualizado pelo servidor
                    session_seq = seq_bits;
                    session_ack = ack_bits;
                    session_window = window_bits;
                }
            }
            
            
            if (is_accept && !session_established) {
                lock_guard<mutex> lock(session_mutex);
                session_established = true;
                // Iniciar countdown do STTL
                sttl_running = true;
                thread sttl_thread(sttl_countdown);
                sttl_thread.detach();
                cout << "[CONN] Sessão estabelecida!" << endl << endl;
            }
            else if (is_ack && session_established) {
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
/*
2025 - Autores:
Maicon Chaves Marques - 14593530
Arthur Trottmann Ramos - 14681052
Karl Cruz Altenhofen - 14585976
*/

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

// Bibliotecas do projeto
#include "UUIDPack.h"
#include "SlowPack.h"

using namespace std;

// Constantes de configuração
constexpr int SLOW_PORT = 7033;           // Porta UDP do servidor
constexpr size_t BUFFER_SIZE = 1472;      // Tamanho máximo do buffer de pacotes
constexpr int NUM_PACOTES = 3;            // Número de pacotes a enviar por ciclo
constexpr int ACK_TIMEOUT_MS = 1000;      // Timeout de espera por ACK em milissegundos

// Variáveis globais da sessão
UUIDPack session_sid;                     // Identificador único da sessão (SID)
bitset<32> session_seq = 0;               // Número de sequência
bitset<32> session_ack = 0;               // Número de ACK
bitset<27> session_sttl = 0;              // Tempo de vida da sessão (em segundos)
bitset<16> session_window = 1024;         // Janela de controle de fluxo

atomic<bool> session_established(false);  // Indica se a sessão foi estabelecida
atomic<bool> ack_received(false);         // Flag de confirmação de ACK
mutex session_mutex;                      // Mutex para sincronizar acesso à sessão

int sockfd;                               // Socket UDP
sockaddr_in central_addr;                // Endereço do servidor

// Controle de execução da thread de STTL
atomic<bool> sttl_running(false);

// Thread para decrementar o STTL da sessão a cada segundo
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

// Converte parte de um vetor de bytes em uma string de bits
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

// Interpreta e imprime os campos de um pacote SLOW
void imprimirPacoteInterpretado(const std::vector<uint8_t>& buffer) {
    int offset = 0;

    // Funções auxiliares para leitura dos tipos básicos
    auto readUInt = [&](int numBytes) -> uint32_t {
        uint32_t value = 0;
        for (int i = 0; i < numBytes; ++i) {
            value |= (buffer[offset++] << (i * 8));
        }
        return value;
    };

    auto readUShort = [&]() -> uint16_t {
        uint16_t value = 0;
        for (int i = 0; i < 2; ++i) {
            value |= (buffer[offset++] << (i * 8));
        }
        return value;
    };

    auto readUChar = [&]() -> uint8_t {
        return buffer[offset++];
    };

    std::cout << "========= PACOTE =========\n";

    // SID: identificador da sessão
    std::cout << "SID: ";
    for (int i = 0; i < 16; ++i)
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[offset++]) << " ";
    std::cout << std::dec << std::endl;

    // STTL e FLAGS empacotados em 4 bytes
    uint32_t sttlFlags = readUInt(4);
    uint32_t flags = sttlFlags & 0x1F;             // Últimos 5 bits: FLAGS
    uint32_t sttl = (sttlFlags >> 5) & 0x7FFFFFF;  // 27 bits restantes: STTL

    std::cout << "FLAGS (binário): ";
    for (int i = 4; i >= 0; --i)
        std::cout << ((flags >> i) & 1);
    std::cout << std::endl;

    std::cout << "STTL (decimal): " << sttl << std::endl;

    // Campos de controle e dados
    uint32_t seq = readUInt(4);           // Número de sequência
    std::cout << "SEQNUM: " << seq << std::endl;

    uint32_t ack = readUInt(4);           // Número de ACK
    std::cout << "ACKNUM: " << ack << std::endl;

    uint16_t win = readUShort();          // Tamanho da janela
    std::cout << "WINDOW: " << win << std::endl;

    uint8_t fid = readUChar();            // Fragment ID
    std::cout << "FID: " << static_cast<int>(fid) << std::endl;

    uint8_t fo = readUChar();             // Fragment Offset
    std::cout << "FO: " << static_cast<int>(fo) << std::endl;

    // Dados em ASCII
    std::cout << "DATA (ASCII): ";
    while (offset < int(buffer.size())) {
        char c = static_cast<char>(buffer[offset++]);
        std::cout << (std::isprint(c) ? c : '.');
    }
    std::cout << std::endl;

    std::cout << "========================================\n" << std::endl;
}

// Imprime os bits de um pacote em ordem de envio
void imprimirBitsDoPacote(const std::vector<uint8_t>& buffer) {
    int bit_idx = 0;
    auto printBits = [&](int numBits) {
        for (int b = 0; b < numBits; ++b, ++bit_idx) {
            uint8_t byte = buffer[bit_idx / 8];
            int bit = (byte >> (bit_idx % 8)) & 1;
            std::cout << bit;
        }
        std::cout << std::endl;
    };

    cout << "SID: \n"; printBits(128);
    cout << "FLAGS: \n"; printBits(5);
    cout << "STTL: \n"; printBits(27);
    cout << "SEQ: \n"; printBits(32);
    cout << "ACK: \n"; printBits(32);
    cout << "WINDOW: \n"; printBits(16);
    cout << "FID: \n"; printBits(8);
    cout << "FO: \n"; printBits(8);

    int total_bits = buffer.size() * 8;
    int remaining = total_bits - bit_idx;
    if (remaining > 0)
        printBits(remaining);

    std::cout << "[FIM DOS BITS] Total: " << total_bits << " bits\n\n" << std::endl;
}

// Cria conjunto de flags para o campo de controle
bitset<5> createFlags(bool connect, bool accept, bool ack, bool failed, bool revive) {
    bitset<5> flags;
    flags[0] = connect;
    flags[1] = accept;
    flags[2] = ack;
    flags[3] = failed;
    flags[4] = revive;
    return flags;
}

// Envia pacote de conexão inicial
void send_connect() {
    session_sid.setAllBy128Bits(bitset<128>(0)); // SID zerado para nova sessão

    SlowPack pkt;
    pkt.setSid(session_sid.getUUID());
    pkt.setFlags(createFlags(true, false, false, false, false)); // Flag de conexão
    pkt.setSttl(bitset<27>(0));
    pkt.setSeqnum(bitset<32>(0));
    pkt.setAcknum(bitset<32>(0));
    pkt.setWindow(bitset<16>(1472));
    pkt.setFid(bitset<8>(0));
    pkt.setFo(bitset<8>(0));
    pkt.setData(vector<uint8_t>());

    vector<uint8_t> packet_data = pkt.getSlow(true);
    std::cout << "\n[ENVIANDO PEDIDO DE CONEXÃO AO SERVIDOR]" << std::endl;
    imprimirPacoteInterpretado(packet_data);
    sendto(sockfd, packet_data.data(), packet_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));
}

// Loop de envio de pacotes e gerenciamento de desconexão/revivência
void send_data_loop(int num_pacotes) {
    while (!session_established) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    for(int a = 0; a < 2; a++){
        for (int i = 0; i < num_pacotes; ++i) {
            ack_received = false;
            int tentativas = 0;

            {
                lock_guard<mutex> lock(session_mutex);
                session_seq = bitset<32>(session_seq.to_ulong() + 1);
            }

            while (!ack_received && tentativas < 3) {
                tentativas++;
                SlowPack pkt;

                {
                    lock_guard<mutex> lock(session_mutex);
                    pkt.setSid(session_sid.getUUID());
                    pkt.setFlags(createFlags(false, false, true, false, false));
                    pkt.setSttl(session_sttl);
                    pkt.setSeqnum(bitset<32>(session_seq));
                    pkt.setAcknum(session_ack);
                    pkt.setWindow(session_window);
                    pkt.setFid(bitset<8>(0));
                    pkt.setFo(bitset<8>(0));

                    string msg = "Pacote " + to_string((NUM_PACOTES*a)+i+1);
                    vector<uint8_t> data_vec(msg.begin(), msg.end());
                    pkt.setData(data_vec);
                }

                vector<uint8_t> packet_data = pkt.getSlow(false);
                std::cout << "\n[ENVIANDO DADO " << (NUM_PACOTES*a)+i+1 << " AO SERVIDOR]" << std::endl;
                imprimirPacoteInterpretado(packet_data);
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
        }

        // Envia pacote de desconexão
        SlowPack pkt;
        {
            lock_guard<mutex> lock(session_mutex);
            pkt.setSid(session_sid.getUUID());
            pkt.setFlags(createFlags(true, true, true, false, false)); // Conecta + Aceita + ACK
            pkt.setSttl(session_sttl);
            pkt.setSeqnum(bitset<32>(session_seq.to_ulong()+1));
            pkt.setAcknum(session_ack);
            pkt.setWindow(bitset<16>(0));
            pkt.setFid(bitset<8>(0));
            pkt.setFo(bitset<8>(0));
            pkt.setData(vector<uint8_t>());
        }

        vector<uint8_t> packet_data = pkt.getSlow(false);
        std::cout << "\n[ENVIANDO PEDIDO PARA DESCONECTAR]" << std::endl;
        imprimirPacoteInterpretado(packet_data);
        sendto(sockfd, packet_data.data(), packet_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));

        this_thread::sleep_for(chrono::milliseconds(500));

        // Tenta reviver a sessão caso STTL ainda esteja válido
        {
            lock_guard<mutex> lock(session_mutex);
            if (session_sttl.to_ulong() > 0) {
                SlowPack revive_pkt;
                revive_pkt.setSid(session_sid.getUUID());
                revive_pkt.setFlags(createFlags(false, true, true, false, false)); // Aceita + ACK
                revive_pkt.setSttl(session_sttl);
                revive_pkt.setSeqnum(session_seq);
                revive_pkt.setAcknum(session_ack);
                revive_pkt.setWindow(session_window);
                revive_pkt.setFid(bitset<8>(0));
                revive_pkt.setFo(bitset<8>(0));
                revive_pkt.setData(vector<uint8_t>());

                vector<uint8_t> revive_data = revive_pkt.getSlow(false);
                std::cout << "\n[ENVIANDO PEDIDO PARA REVIVER]" << std::endl;
                imprimirPacoteInterpretado(revive_data);
                sendto(sockfd, revive_data.data(), revive_data.size(), 0, (sockaddr*)&central_addr, sizeof(central_addr));
            }
        }
        this_thread::sleep_for(chrono::milliseconds(1000));
    }

    sttl_running = false; // Encerra contagem STTL
}

// Loop de recepção e interpretação de pacotes
void receive_loop() {
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(central_addr);

    cout << "[INFO] Aguardando pacotes..." << endl;

    while (true) {
        ssize_t len = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (sockaddr*)&central_addr, &addr_len);
        if (len <= 0) continue;

        vector<uint8_t> received_data(buffer, buffer + len);
        std::cout << "\n[PACOTE RECEBIDO]" << std::endl;
        imprimirPacoteInterpretado(received_data);

        if (len >= 32) {
            // Reconstroi o pacote manualmente (bit a bit)
            SlowPack p;

            bitset<128> sid_bits;
            for (int i = 0; i < 16; ++i)
                for (int b = 0; b < 8; ++b)
                    sid_bits[i * 8 + b] = (buffer[i] >> b) & 1;

            bitset<5> flags_bits(buffer[16] & 0x1F);

            bitset<27> sttl_bits;
            int bit_idx = 0;
            for (int i = 5; i <= 7; ++i, ++bit_idx)
                sttl_bits[bit_idx] = (buffer[16] >> i) & 1;
            for (int byte_i = 17; byte_i <= 19; ++byte_i)
                for (int i = 0; i < 8; ++i, ++bit_idx)
                    sttl_bits[bit_idx] = (buffer[byte_i] >> i) & 1;

            uint32_t raw_seq; memcpy(&raw_seq, buffer + 20, 4);
            uint32_t raw_ack; memcpy(&raw_ack, buffer + 24, 4);
            uint16_t raw_win; memcpy(&raw_win, buffer + 28, 2);

            bitset<32> seq_bits(le32toh(raw_seq));
            bitset<32> ack_bits(le32toh(raw_ack));
            bitset<16> window_bits(le16toh(raw_win));
            bitset<8> fid_bits(buffer[30]);
            bitset<8> fo_bits(buffer[31]);

            vector<uint8_t> payload;
            if (len > 32)
                payload.insert(payload.end(), buffer + 32, buffer + len);

            p.setSid(sid_bits);
            p.setFlags(flags_bits);
            p.setSttl(sttl_bits);
            p.setSeqnum(seq_bits);
            p.setAcknum(ack_bits);
            p.setWindow(window_bits);
            p.setFid(fid_bits);
            p.setFo(fo_bits);
            p.setData(payload);

            // Atualiza variáveis globais da sessão
            {
                lock_guard<mutex> lock(session_mutex);
                session_sid.setAllBy128Bits(sid_bits);
                session_sttl = sttl_bits;
                session_seq = seq_bits;
                session_ack = ack_bits;
                session_window = window_bits;
            }

            // Interpretação de flags
            bool is_accept = flags_bits[1];
            bool is_ack = flags_bits[2];
            bool is_failed = flags_bits[3];
            bool is_revive = flags_bits[4];

            if (is_accept && !session_established) {
                lock_guard<mutex> lock(session_mutex);
                session_established = true;
                sttl_running = true;
                thread sttl_thread(sttl_countdown);
                sttl_thread.detach();
                cout << "[CONN] Sessão estabelecida!" << endl;
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

// Ponto de entrada principal
int main() {
    sockfd = socket(AF_INET, SOCK_DGRAM, 0); // Criação do socket UDP
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

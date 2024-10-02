//2.)client
#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <unordered_set>
#define SERVER_PORT 5011
#define RETRANSMISSION_PORT 6000
#define BUFFER_SIZE 9000
#define END_SIGNAL "END"
#define STOP_SIGNAL "STOP"

using namespace std;

struct Packet {
    int index;
    int size;
    char data[BUFFER_SIZE];
};

int create_socket(struct sockaddr_in& server_addr) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error: Could not create socket" << endl;
        exit(1);
    }

    int recv_buffer_size = 2 * 1024 * 1024;  // 2 MB Receive Buffer
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size));

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 10000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    return sockfd;
}

int main() {
    ofstream outfile("received_file.bin", ios::binary | ios::trunc);
    if (!outfile.is_open()) {
        cerr << "Error: Could not open output file" << endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int sockfd = create_socket(server_addr);

    string filename = "largefile.bin";
    sendto(sockfd, filename.c_str(), filename.size(), 0, (struct sockaddr*)&server_addr, addr_len);
    cout << "Requested file: " << filename << endl;

    int nopac;
    recvfrom(sockfd, &nopac, sizeof(int), 0, (struct sockaddr*)&server_addr, &addr_len);
    cout << "Received reply from Server: Number of packets: " << nopac << endl;

    unordered_set<int> pktset;
    for (int i = 0; i < nopac; ++i) {
        pktset.insert(i);
    }

    // Initial transmission phase
    while (true) {
        Packet packet;
        int recv_len = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, &addr_len);
        if (recv_len > 0) {
            if (strncmp(packet.data, END_SIGNAL, sizeof(END_SIGNAL)) == 0) {
                cout << "Received END signal, breaking out of 1st transmission phase." << endl;
                string clientmessage = "ACK";
                sendto(sockfd, clientmessage.c_str(), clientmessage.size(), 0, (struct sockaddr*)&server_addr, addr_len);
                break;
            }
            if (packet.index >= 0 && packet.index < nopac) {
                outfile.seekp(packet.index * BUFFER_SIZE);
                outfile.write(packet.data, packet.size);
                pktset.erase(packet.index);
            }
        } else if (recv_len == -1) {
            cout << "Timeout in first transmission phase" << endl;
              continue;
        }
    }

    close(sockfd);
    cout << "Closed the initial transmission socket" << endl;

    // Open new socket for retransmission
    server_addr.sin_port = htons(RETRANSMISSION_PORT);
    int retransmission_sockfd = create_socket(server_addr);
    cout << "Opened new socket for retransmission on port " << RETRANSMISSION_PORT << endl;

    // Retransmission phase
    while (!pktset.empty()) {
        vector<int> missing_packet_indices(pktset.begin(), pktset.end());
        cout << "Requesting retransmission for " << pktset.size() << " packets." << endl;
        
        sendto(retransmission_sockfd, missing_packet_indices.data(), missing_packet_indices.size() * sizeof(int), 0,
               (struct sockaddr*)&server_addr, addr_len);

        auto start_time = chrono::steady_clock::now();
        while (!pktset.empty()) {
            Packet packet;
            int recv_len = recvfrom(retransmission_sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, &addr_len);
            
            if (recv_len > 0) {
                outfile.seekp(packet.index * BUFFER_SIZE);
                outfile.write(packet.data, packet.size);
                pktset.erase(packet.index);
                cout << "Retransmitted packet received, index: " << packet.index << endl;
            } else if (recv_len == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    auto current_time = chrono::steady_clock::now();
                    if (chrono::duration_cast<chrono::seconds>(current_time - start_time).count() >= 5) {
                        cout << "Timeout waiting for retransmitted packets, requesting again" << endl;
                        break;
                    }
                } else {
                    cerr << "Error in recvfrom: " << strerror(errno) << endl;
                    break;
                }
            }
        }
    }

    sendto(retransmission_sockfd, STOP_SIGNAL, sizeof(STOP_SIGNAL), 0, (struct sockaddr*)&server_addr, addr_len);
    cout << "All packets received and STOP signal sent" << endl;

    outfile.close();
    cout << "File received and saved." << endl;

    close(retransmission_sockfd);
    return 0;
}
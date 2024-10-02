//1.) server
#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>

#define SERVER_PORT 8080
#define BUFFER_SIZE 9000

using namespace std;

struct Packet {
    int index;
    int size;
    char data[BUFFER_SIZE];
};

int nopac;  // To store the number of packets globally
vector<Packet> packets;  // Declare globally so that both functions can use it

// Function to send the file initially
void send_file(int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file.is_open()) {
        cerr << "Error: Could not open file " << filename << endl;
        return;
    }

    file.seekg(0, ios::end);
    long long file_size = file.tellg();
    file.seekg(0, ios::beg);
    nopac = file_size / BUFFER_SIZE + (file_size % BUFFER_SIZE != 0);
    cout << "No of packets: " << nopac << endl;
    sendto(sockfd, &nopac, sizeof(int), 0, (struct sockaddr*)&client_addr, addr_len); // sending number of packets

    char buffer[BUFFER_SIZE];
    int packet_index = 0;

    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        Packet packet;
        packet.index = packet_index++;
        packet.size = file.gcount();
        memcpy(packet.data, buffer, packet.size);
        packets.push_back(packet);
    }

    // Send the packets
    for (const auto& packet : packets) {
        sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, addr_len);
    }

    // Send "END" signal to mark the end of transmission
    char END_SIGNAL[4] = "END";
    Packet endp;
    endp.index = packet_index++;
    endp.size = sizeof(END_SIGNAL);
    strncpy(endp.data, END_SIGNAL, sizeof(END_SIGNAL));  // Copies the contents of END_SIGNAL to endp.data
    sendto(sockfd, &endp, sizeof(endp), 0, (struct sockaddr*)&client_addr, addr_len);

    cout << "First attempt of file transfer completed." << endl;

    file.close(); // close the file once done
}

// Function to handle retransmission of missing packets
void retransmit_packets(int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, vector<int>& missing_packet_indices) {
    struct timeval timeout;
    timeout.tv_sec = 0;  // Set timeout for 1000 useconds for retransmission requests
    timeout.tv_usec = 250000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int max_retrans_attempts = 5; // Maximum retransmission attempts
    int retrans_attempts = 0;

    while (retrans_attempts < max_retrans_attempts) {
        // Receive missing packet indices from the client
        int recv_len = recvfrom(sockfd, missing_packet_indices.data(), missing_packet_indices.size() * sizeof(int), 0, (struct sockaddr*)&client_addr, &addr_len);

        if (recv_len == -1) {
            // If timeout occurs
            retrans_attempts++;
            cout << "No retransmission request received. Attempt: " << retrans_attempts << endl;
            continue; // Retry receiving
        }

        // Check if STOP signal received (as a separate string)
        char STOP_SIGNAL[5] = "STOP";
        if (recv_len == sizeof(STOP_SIGNAL) && strncmp((char*)missing_packet_indices.data(), STOP_SIGNAL, sizeof(STOP_SIGNAL)) == 0) {
            cout << "Received STOP signal, ending retransmission." << endl;
            break;
        }

        // Retransmit missing packets
        for (int i = 0; i < missing_packet_indices.size(); i++) {
            int packet_index = missing_packet_indices[i];

            // Check if the packet index is valid and retransmit
            if (packet_index >= 0 && packet_index < packets.size()) {
                sendto(sockfd, &packets[packet_index], sizeof(packets[packet_index]), 0, (struct sockaddr*)&client_addr, addr_len);
                //cout << "Retransmitted packet index: " << packet_index << endl;
            }
        }
        retrans_attempts = 0; // Reset attempts if we receive valid retransmission requests
    }

    cout << "Max retransmission attempts reached, stopping retransmission." << endl;
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error: Could not create socket" << endl;
        return 1;
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Error: Could not bind socket" << endl;
        close(sockfd);
        return 1;
    }

    cout << "Server is running, waiting for file requests..." << endl;

    char filename[300];
    ssize_t recv_len = recvfrom(sockfd, filename, sizeof(filename), 0, (struct sockaddr*)&client_addr, &addr_len);
    if (recv_len < 0) {
        cerr << "Error: Could not receive filename" << endl;
        close(sockfd);
        return 1;
    }
    filename[recv_len] = '\0';  // Null-terminate the received filename

    cout << "Client requested file: " << filename << endl;

    // Send file and retransmission logic
    send_file(sockfd, client_addr, addr_len, filename);  // Call the sending function

    // Initialize missing packet indices
    vector<int> missing_packet_indices(nopac, -1);  // Fill with -1 initially (indicating no requests yet)

    retransmit_packets(sockfd, client_addr, addr_len, missing_packet_indices);  // Handle retransmissions

    close(sockfd);
    return 0;
}
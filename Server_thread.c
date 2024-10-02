#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>

#define SERVER_PORT 8090
#define BUFFER_SIZE 9000
#define NUM_THREADS 4

using namespace std;

struct Packet {
    int index;
    int size;
    char data[BUFFER_SIZE];
};

int nopac;  // To store the number of packets globally
vector<Packet> packets;  // Declare globally so that all functions can use it
mutex send_mutex;  // Mutex for thread-safe sending

// Function to send a range of packets
void send_packet_range(int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, int start, int end) {
    for (int i = start; i < end && i < packets.size(); ++i) {
        lock_guard<mutex> lock(send_mutex); //mutex locking due to shared socket.
        sendto(sockfd, &packets[i], sizeof(packets[i]), 0, (struct sockaddr*)&client_addr, addr_len);
    }
}

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
    nopac = file_size / BUFFER_SIZE + (file_size % BUFFER_SIZE != 0);// send an extra packet if filesize is not evenly divisible into buffer size.
    cout << "No of packets: " << nopac << endl;
    sendto(sockfd, &nopac, sizeof(int), 0, (struct sockaddr*)&client_addr, addr_len); // sending number of packets

    char buffer[BUFFER_SIZE];
    int packet_index = 0;

    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {             // reading the file and packetizing it, and store it in the vector - packets
        Packet packet;
        packet.index = packet_index++;
        packet.size = file.gcount();
        memcpy(packet.data, buffer, packet.size);
        packets.push_back(packet);
    }

    // Create and start threads
    vector<thread> threads;
    int packets_per_thread = nopac / NUM_THREADS;
    for (int i = 0; i < NUM_THREADS; ++i) {
        int start = i * packets_per_thread;
        int end = (i == NUM_THREADS - 1) ? nopac : (i + 1) * packets_per_thread;
        threads.emplace_back(send_packet_range, sockfd, client_addr, addr_len, start, end);  //threading
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    //sleep(1000000);
    // Send "END" signal to mark the end of transmission
    char END_SIGNAL[4] = "END";
    Packet endp;
    endp.index = packet_index++;
    endp.size = sizeof(END_SIGNAL);                                                              // sending the file 
    strncpy(endp.data, END_SIGNAL, sizeof(END_SIGNAL));
    sendto(sockfd, &endp, sizeof(endp), 0, (struct sockaddr*)&client_addr, addr_len);

    cout << "First attempt of file transfer completed." << endl;

    file.close(); // close the file once done
}

// Function to handle retransmission of missing packets
void retransmit_packets(int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, vector<int>& missing_packet_indices) {
    struct timeval timeout;
    timeout.tv_sec = 0;  // Set timeout for x useconds for retransmission requests
    timeout.tv_usec = 100000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    int max_retrans_attempts = 500; // Maximum retransmission attempts
    int retrans_attempts = 0;

    while (retrans_attempts < max_retrans_attempts) {                    // O(max_retrans_attempts*N) where N is the no of missing packets each time
        // Receive missing packet indices from the client
        int recv_len = recvfrom(sockfd, missing_packet_indices.data(), missing_packet_indices.size() * sizeof(int), 0, (struct sockaddr*)&client_addr, &addr_len);

        if (recv_len == -1) {
            // If timeout occurs
            retrans_attempts++;
            cout << "No retransmission request received. Attempt: " << retrans_attempts << endl;    //receiving missing packet indices
            continue; // Retry receiving
        }

        // Check if STOP signal received (as a separate string)
        char STOP_SIGNAL[5] = "STOP";
        if (recv_len == sizeof(STOP_SIGNAL) && strncmp((char*)missing_packet_indices.data(), STOP_SIGNAL, sizeof(STOP_SIGNAL)) == 0) {
            cout << "Received STOP signal, ending retransmission." << endl;
            break;
        }
	cout << "Retransmission request received. Attempt: " << retrans_attempts << endl; 
        // Retransmit missing packets using threads
        vector<thread> retrans_threads;
        int packets_per_thread = missing_packet_indices.size() / NUM_THREADS;                  //threading and sending
        for (int i = 0; i < NUM_THREADS; ++i) {
            int start = i * packets_per_thread;
            int end = (i == NUM_THREADS - 1) ? missing_packet_indices.size() : (i + 1) * packets_per_thread;
            retrans_threads.emplace_back([&, start, end]() {
                for (int j = start; j < end; ++j) {
                    int packet_index = missing_packet_indices[j];
                    if (packet_index >= 0 && packet_index < packets.size()) {
                        lock_guard<mutex> lock(send_mutex);    //mutex locking due to shared socket.
                        sendto(sockfd, &packets[packet_index], sizeof(packets[packet_index]), 0, (struct sockaddr*)&client_addr, addr_len);
            
                    }
                }
            });
        }

        // Wait for all retransmission threads to complete
        for (auto& t : retrans_threads) {
            t.join();
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
    int bufsize = 4194304; // 4 MB buffer size at the UDP layer.
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));

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
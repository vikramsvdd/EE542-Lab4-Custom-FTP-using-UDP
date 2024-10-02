#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#define SERVER_PORT 5011
#define RETRANSMISSION_PORT 6000
#define BUFFER_SIZE 9000

using namespace std;

struct Packet {
    int index;
    int size;
    char data[BUFFER_SIZE];
};

int nopac;
vector<Packet> packets;

int create_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        cerr << "Error: Could not create socket" << endl;
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "Error: Could not bind socket" << endl;
        close(sockfd);
        exit(1);
    }

    int send_buffer_size = 2 * 1024 * 1024;  // 2 MB Send Buffer
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size));

    return sockfd;
}

void send_file(int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file.is_open()) {
        cerr << "Error: Could not open file " << filename << endl;
        return;
    }

    file.seekg(0, ios::end);
    long long file_size = file.tellg();
    file.seekg(0, ios::beg);
    nopac = (file_size + BUFFER_SIZE - 1) / BUFFER_SIZE;
    cout << "No of packets: " << nopac << endl;
    sendto(sockfd, &nopac, sizeof(int), 0, (struct sockaddr*)&client_addr, addr_len);

    char buffer[BUFFER_SIZE];
    int packet_index = 0;

    // Implement flow control
    int send_buffer_size = 2 * 1024 * 1024;  // 2 MB Send Buffer
    setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size));

    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        Packet packet;
        packet.index = packet_index++;
        packet.size = file.gcount();
        memcpy(packet.data, buffer, packet.size);
        sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, addr_len);
    }
//file.close();                                                                // closing the file here itself to save RAM

    // Send the packets
  //  for (const auto& packet : packets) {
     //   sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, addr_len);
       // cout<<"Sending Packet: "<<packet.index<<endl;
    //}

    // Send "END" signal to mark the end of transmission, send this repeatedly till the client acknowledges.
    char END_SIGNAL[4] = "END";
    Packet endp;
    endp.index = packet_index++;
    endp.size = sizeof(END_SIGNAL);
    strncpy(endp.data, END_SIGNAL, sizeof(END_SIGNAL));  // Copies the contents of END_SIGNAL to endp.data
    sendto(sockfd, &endp, sizeof(endp), 0, (struct sockaddr*)&client_addr, addr_len);
    char clientmessage[100];
while(true){
    int recv_len = recvfrom(sockfd, clientmessage, sizeof(clientmessage), 0, (struct sockaddr*)&client_addr, &addr_len);
    
	if(recv_len>0){
	cout<<"Client Said: "<<clientmessage<<endl;
	break;
	}
	else if(recv_len==-1){
 	sendto(sockfd, &endp, sizeof(endp), 0, (struct sockaddr*)&client_addr, addr_len); // repeatedly send end till client sends ACK.
          cout<<"Sending END to Client"<<endl;
	}
 	}

    cout << "First attempt of file transfer completed." << endl;
file.close();
}

void retransmit_packets(int sockfd, struct sockaddr_in client_addr, socklen_t addr_len, const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file.is_open()) {
        cerr << "Error: Could not open file for retransmission" << endl;
        return;
    }

    vector<int> missing_packet_indices(nopac);

   // struct timeval tv;
   // tv.tv_sec = 1;  // 1 second timeout
   // tv.tv_usec = 0;
   // setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    while (true) {
        cout << "Waiting for retransmission request..." << endl;
        int recv_len = recvfrom(sockfd, missing_packet_indices.data(), missing_packet_indices.size() * sizeof(int), 0, (struct sockaddr*)&client_addr, &addr_len);
        
        if (recv_len > 0) {
            cout << "Retransmission request received" << endl;
            // Check if STOP signal received
            char STOP_SIGNAL[5] = "STOP";
            if (recv_len == sizeof(STOP_SIGNAL) && strncmp((char*)missing_packet_indices.data(), STOP_SIGNAL, sizeof(STOP_SIGNAL)) == 0) {
                cout << "Received STOP signal, ending retransmission." << endl;
                break;
            }

            // Retransmit missing packets
            for (int i = 0; i < recv_len / sizeof(int); i++) {
                int packet_index = missing_packet_indices[i];
                if (packet_index >= 0 && packet_index < nopac) {
                    Packet packet;
                    packet.index = packet_index;
                    file.seekg(packet_index * BUFFER_SIZE);
                    file.read(packet.data, BUFFER_SIZE);
                    packet.size = file.gcount();
                    sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&client_addr, addr_len);
                    cout << "Retransmitted packet index: " << packet_index << endl;
                }
            }
        } else if (recv_len == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                cout << "Timeout occurred, no data received" << endl;
                continue;
            } else {
                cerr << "Error in recvfrom: " << strerror(errno) << endl;
                break;
            }
        }
    }

    file.close();
    cout << "Retransmission phase completed." << endl;
}

int main() {
    int sockfd = create_socket(SERVER_PORT);
    cout << "Server is running, waiting for file requests..." << endl;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    char filename[300];
    ssize_t recv_len = recvfrom(sockfd, filename, sizeof(filename), 0, (struct sockaddr*)&client_addr, &addr_len);

    if (recv_len < 0) {
        cerr << "Error: Could not receive filename" << endl;
        close(sockfd);
        return 1;
    }

    filename[recv_len] = '\0';
    cout << "Client requested file: " << filename << endl;

    send_file(sockfd, client_addr, addr_len, filename);
    close(sockfd);
    cout << "Closed the initial transmission socket" << endl;

    int retransmission_sockfd = create_socket(RETRANSMISSION_PORT);
    cout << "Opened new socket for retransmission on port " << RETRANSMISSION_PORT << endl;

    retransmit_packets(retransmission_sockfd, client_addr, addr_len, filename);

    close(retransmission_sockfd);
    return 0;
}
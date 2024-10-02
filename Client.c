//2.) Client
#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctime>
#include <bits/stdc++.h>
#define SERVER_PORT 8090
#define BUFFER_SIZE 9000
#define END_SIGNAL "END"
#define STOP_SIGNAL "STOP"
using namespace std;

struct Packet {
    int index;
    int size;
    char data[BUFFER_SIZE];
};
int c=0;

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	std::time_t start,end;
    if (sockfd < 0) {
        cerr << "Error: Could not create socket" << endl;
        return 1;
    }

    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int bufsize = 4194304; // 4 MB buffer size at the UDP layer.
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
	start=time(0);
    string filename = "largefile.bin";
    sendto(sockfd, filename.c_str(), filename.size(), 0, (struct sockaddr*)&server_addr, addr_len);
    cout << "Requested file: " << filename << endl;                                                    // requesting for the file 

    // Vector for storing packets
    int nopac;
    recvfrom(sockfd, &nopac, sizeof(int), 0, (struct sockaddr*)&server_addr, &addr_len);
    cout << "Number of packets: " << nopac << endl;
    vector<Packet> received_packets(nopac);                                                         // preparing a vector to store the packets
    unordered_set<int> pktset;

    // Insert values from 0 to N
    for (int i = 0; i < nopac; ++i) {
        pktset.insert(i);
    }
    
    // Set timeout for receiving packets
    struct timeval timeout;
    timeout.tv_sec = 0;  // Timeout for receiving packets
    timeout.tv_usec=100000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));


    // Receiving packets initially
    while (true) {
        Packet packet;
        int recv_len = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, &addr_len);
        if (recv_len > 0) {
            if (strncmp(packet.data, END_SIGNAL, sizeof(END_SIGNAL)) == 0) {
                cout << "Received END signal, breaking out of while." << endl;             //END signal, break out of while                                      
                break;
            }
            if (packet.index >= 0 && packet.index < received_packets.size()) {             // receiving pkts in the vector                
		received_packets[packet.index] = packet;
		pktset.erase(pktset.find(packet.index));
                /*cout << "Received packet index " << packet.index << endl;
		c++;
                cout<<"total packets till date: "<<c<<endl;*/
            }
        } else if (recv_len == -1) {
            cout << "Timeout occurred, and no response, suspecting packetlloss, entering retransmission phase." << endl;              // timeout, break out of while.
            break;
        }
    }

    // Retransmission logic
   // vector<int> missing_packet_indices;
    //bool all_received = false;

    while (pktset.size()) {
       // missing_packet_indices.clear(); // Clear old list of missing packets

        // Check for missing packets
        //for (int i = 0; i < received_packets.size(); i++) {
        //    if (received_packets[i].size == 0) {                                   // pure O(N) logic here
        //        missing_packet_indices.push_back(i);
        //    }
        //}


	vector<int> missing_packet_indices(pktset.begin(), pktset.end());

        // If there are missing packets, request retransmission
        if (!missing_packet_indices.empty()) {
           // cout << "Requesting retransmission for " << missing_packet_indices.size() << " packets." << endl;

            // Send the list of missing packet indices to the server
            sendto(sockfd, missing_packet_indices.data(), missing_packet_indices.size() * sizeof(int), 0,                       //requesting the missing packet indices to server
                   (struct sockaddr*)&server_addr, addr_len);

            // Set a timeout for receiving retransmitted packets
           // timeout.tv_sec = 0;  // Timeout for receiving retransmitted packets
           // timeout.tv_usec=90000000;
	   // setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            // Receive retransmitted packets
            for (int i = 0; i < missing_packet_indices.size(); i++) {
                Packet packet;

                int recv_len = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, &addr_len);       //receiving the pkts


                if (recv_len > 0) {
                    received_packets[packet.index] = packet;
                    pktset.erase(pktset.find(packet.index));
                   // cout << "Retransmitted packet received, index: " << packet.index << endl;
		    //c++;
                    // cout<<"retrans-total packets till date: "<<c<<endl;
                } else if (recv_len ==-1) {
                    cout << "Timeout occurred while receiving retransmitted packets, retrying." << endl;//timeout, break outta the loop.
		    break;
                }



        } //else {
	      
            //all_received = true; // All packets have been received
        //}
            //cout<<"O DA"<<endl;
}
    }
/*
	// server testing
	vector<int> missing_packet_indices(0, 10);
sendto(sockfd, missing_packet_indices.data(), missing_packet_indices.size() * sizeof(int), 0,                       //requesting the missing packet indices to server
                   (struct sockaddr*)&server_addr, addr_len);

int xyz=0;

while(xyz<100) {
Packet packet;
                int recv_len = recvfrom(sockfd, &packet, sizeof(packet), 0, (struct sockaddr*)&server_addr, &addr_len);       //receiving the pkts


                if (recv_len > 0) {
			cout<<"serv good"<<endl;
break;
                    //cout << "Retransmitted packet received, index: " << packet.index << endl;
		    //c++;
                    // cout<<"retrans-total packets till date: "<<c<<endl;
                } else if (recv_len ==-1) {
                    cout << "serv bad" << endl;//timeout, break outta the loop.
		
                }

xyz+=1;
}*/

    // After all packets are received, send a STOP signal to the server
    sendto(sockfd, STOP_SIGNAL, sizeof(STOP_SIGNAL), 0, (struct sockaddr*)&server_addr, addr_len);                 //STOP signal sent thats it.
	end=time(0);
    cout << "All packets received and STOP signal sent." << endl;

    // Write packets to file finally
    ofstream outfile("received_" + filename, std::ios::binary);                                                        //finally write to the file from the vector. 
    for (const auto& packet : received_packets) {
        outfile.write(packet.data, packet.size);
    }
    outfile.close();

    cout << "File received and saved." << endl;
	cout<< difftime(end,start);
    close(sockfd);
    return 0;
}


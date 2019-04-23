/*
 * sender_socket.h
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#pragma once
#include "stdafx.h"
#include "packet_structs.h"


class SenderSocket {
	// pt 1
	clock_t start_time;
	SOCKET sock;
	double RTO; // RTO that is constantly updated and used in next communication
	int packet_size; // variable packet size for last packet received --> can be used in many functions
	int initialize_sockaddr(struct sockaddr_in& server, char* host = NULL, int port = 0);
	double elapsed_time;
	struct sockaddr_in request;
	int seq_number;
	LinkProperties* link_prop;

	// part 2
	double devRTT; // RTO calculations
	HANDLE stat, worker; // stat thread, worker thread
	// variables connected to stat thread
	StatData* s; // pointer so that it can be changed in other functions
	void update_receiver_info(ReceiverHeader* rh);
	void calculate_RTO(double sample_RTT);

	// timing for printing
	clock_t begin_transfer;

	// send function stuff
	int next_seq;
	bool send_packet(int index); // for Packet type only!

	// shared buffer stuff
	HANDLE eventQuit, empty, full, socketReceiveReady, finishSend;
	Packet *pending_pkts;
	int W;
	int lastReleased;
	int lastSeq;  // send tells the worker thread that this is the last packet, after which fin can send
	//long pending_packets;
	long pending_packets;

	// thread functions
	clock_t timer_expire; // time for base packet so be sent
	void receive_ACK();
	int retx_count, sameack_count; // count for checking if same base ack received many times
	clock_t beginRTT, endRTT;
	int nextToSend;

	// sockaddr stuff
	char* targetHost;
	int port;

public:
	// core functionality 
	SenderSocket(); // start timer, stat thread, etc
	int Open(char* targetHost, int port, int window_size, LinkProperties* lp); // targetHost, MAGIC_PORT, senderWindow, &lp
	int Send(char* charBuf, int bytes, int type = 2); // charBuf + off, bytes, data default (0 = SYN, 1 = FIN, 3 = lastPacket)
	int Close(double &elapsed_transfer);
	~SenderSocket();

	// printout getters
	double get_elapsed_time() { return elapsed_time; }
	double get_estRTT() { return this->s->RTT; }
	int get_packet_size() { return packet_size; }
	double calcualte_ideal_rate();
	//void set_last(int l) { lastSeq = l; }
	void set_last() { this->lastSeq = this->next_seq+1;  }

	// thread functions
	void runWorker(void);

};
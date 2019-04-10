/*
 * packet_structs.h
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#pragma once

#include "stdafx.h"

#define FORWARD_PATH 0
#define RETURN_PATH 1
#define MAGIC_PROTOCOL 0x8311AA

#pragma pack(push, 1)

struct Flags {
	DWORD reserved : 5; // must be zero
	DWORD SYN : 1;
	DWORD ACK : 1;
	DWORD FIN : 1;
	DWORD magic : 24;
	Flags() { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; }
	Flags(DWORD s, DWORD a, DWORD f) { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; reserved = 0; SYN = s; ACK = a; FIN = f; }
};

struct SenderDataHeader {
	Flags flags;
	DWORD seq; // must begin from 0
};

struct LinkProperties {
	// transfer parameters
	float RTT; // propagation RTT (in sec)
	float speed; // bottleneck bandwidth (in bits/sec)
	float pLoss[2]; // probability of loss in each direction
	DWORD bufferSize; // buffer size of emulated routers (in packets)
	LinkProperties() { memset(this, 0, sizeof(*this)); }
};

struct SenderSynHeader {
	SenderDataHeader sdh;
	LinkProperties lp;
};

struct ReceiverHeader {
	Flags flags;
	DWORD recvWnd; // receiver window for flow control (in pkts)
	DWORD ackSeq; // ack value = next expected sequence
};

// used for contents of each packet being sent
struct Packet {
	int seq; // for easy access in worker thread
	int type; // SYN = 0, FIN = 1, data = 2
	int size; // for retx in worker thread
	clock_t txTime; // transmission time
	char *buf; // actual packet with header
};

#pragma pack(pop)

struct StatData {
	clock_t start_time; // local to the thread function

	int sender_wind_base; // base of the sender window
	double data_ACKed; // MB of data acked by receiver
	int* next_seq; // next expected seq # --> ReceiverHeader
	int timeout_counter; // every timeout
	int fast_retx_counter; //
	DWORD sender_wind_size; // --> ss.Open parameter
	DWORD receiver_wind_size;  // --> ReceiverHeader
	DWORD effective_wind_size; // --> ss.Open at the end
	double goodput; // Mbps speed of app consuming data at receiver [(new - old) * 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)]

	double RTT; // estRTT --> all functions
	HANDLE isDone; // --> ss.Close
	void set_new_goodput(double new_goodput) {
		this->goodput = (new_goodput - this->goodput) * 8 * (MAX_PKT_SIZE - sizeof(SenderDataHeader));
	}
	void set_effective_win_size() { // effective window is min(sender_wind_size, receiver_wind_size);
		effective_wind_size = min(sender_wind_size, receiver_wind_size);
	}
	StatData(int* val) { memset(this, 0, sizeof(*this)); this->isDone = CreateEventA(NULL, true, false, NULL); next_seq = val; } // itialize all variables to 0
};


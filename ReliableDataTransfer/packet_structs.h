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
	Flags() { memset(this, 0, sizeof(*this)); magic = MAGIC_PROTOCOL; reserved = 0; }
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

#pragma pack(pop)
/*
 * sender_socket.cpp
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#include "sender_socket.h"
#include "stdafx.h"

// declaring thread functions for later defining
DWORD WINAPI statThread(LPVOID pParam);
DWORD WINAPI workerThread(LPVOID pParam);

/******************** CONSTRUCTOR ********************/
SenderSocket::SenderSocket() {
	// itializations
	this->start_time = clock(); 
	this->next_seq = this->nextToSend = this->retx_count = this->sameack_count = 0;
	this->lastSeq = -1;
	seq_number = 0; 
	this->pending_packets = 0;
	sock = INVALID_SOCKET; 
	elapsed_time = 0.0;
	this->s = new StatData(&(this->next_seq));
	this->s->old_sender_wind_base = this->s->sender_wind_base = 0;
	this->s->start_time = this->start_time;
	this->socketReceiveReady = CreateEvent(NULL, false, false, NULL);
	this->finishSend = CreateSemaphore(NULL, 0, 1, NULL); // semaphore to wait for worker to finish
	devRTT = 0;
}

/******************** DESTRUCTOR ********************/
SenderSocket::~SenderSocket() {
	if (WaitForSingleObject(this->stat, 0) == WAIT_OBJECT_0) { // if stat thread not terminated
		// sets the stat thread to complete so that it knows it is done printing
		SetEvent(this->s->isDone);
		CloseHandle(stat);
	}
	if (WaitForSingleObject(this->worker, 0) == WAIT_OBJECT_0) {
		SetEvent(this->finishSend);
		CloseHandle(worker);
	}

	this->s->isDone = NULL;
	this->finishSend = NULL;
	stat = NULL;
	worker = NULL;

	// delete StatData 
	delete s;
	
	// delete buffer
	for (int i = 0; i < this->W; ++i) {
		delete (pending_pkts + i)->buf;
	}
	delete pending_pkts;

}

/******************** OPEN ********************/
int SenderSocket::Open(char* targetHost, int port, int window_size, LinkProperties* link_prop) {
	//this->debug = debug;
	/////////////////////// initializations ///////////////////////
	this->s->sender_wind_size = this->W = window_size;
	this->s->RTT = link_prop->RTT;
	this->RTO = max(1.0, 2 * this->s->RTT);

	// initalize sempahore handles - TODO: MOVE THIS AROUND SO THAT IT USES EFFECTIVE WINDOW SIZE
	this->full = CreateSemaphore(NULL, 0, window_size, NULL);
	this->empty = CreateSemaphore(NULL, 0, window_size, NULL);

	// initialize packet buffer and buffer of each inidividual packet
	pending_pkts = new Packet[window_size * MAX_PKT_SIZE];
	for (int i = 0; i < window_size; ++i) {
		(pending_pkts + i)->buf = new char[MAX_PKT_SIZE];
	}

	if (sock != INVALID_SOCKET) {
		// if sock value is already set then Open is already called
		return ALREADY_CONNECTED;
	}

	/////////////////////// open UDP socket ///////////////////////
	// initialize socket
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
		// handle errors
		printf("Invalid socket with %d\n", WSAGetLastError());
		exit(-1);
	}

	if (WSAEventSelect(sock, socketReceiveReady, FD_READ) == SOCKET_ERROR) {
		printf("socket error in WSAEventSelect %d\n", WSAGetLastError());
		exit(-1);
	}


	// bind port 0
	this->link_prop = link_prop;
	struct sockaddr_in local;
	initialize_sockaddr(local);

	if (bind(sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) { // let the OS select next available port for me
		// handle errors
		printf("Bind errors\n");
		exit(-1);
	}

	// for sendto
	this->targetHost = targetHost;
	this->port = port;
	if ((packet_size = initialize_sockaddr(request, targetHost, port)) != STATUS_OK) {
		return packet_size; // for INVALID_NAME
	}

	SenderSynHeader sh{};
	SenderDataHeader sender_data{};
	sender_data.flags = Flags(1, 0, 0); // SYN is 1, rest are 0
	sender_data.seq = next_seq; // 0 basically
	sh.lp = *link_prop;
	sh.sdh = sender_data;
	for (int i = 1; i <= 4; ++i) {
		// tried too many times
		if (i == 4) {
			printf("open timeout\n"); return TIMEOUT;
		}


		////////////////////////// send request to the server //////////////////////////
		if ((packet_size = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&request, sizeof(request))) == SOCKET_ERROR) {
			printf("[%.2f] --> failed sendto with %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
			return FAILED_SEND;
		}

		beginRTT = clock(); // elapsed open is used for elapsed time if SYN and SYN-ACK work

		// increase incoming and outgoing capacity
		int kernelBuffer = 20e6; // 20 meg
		if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&kernelBuffer, sizeof(int)) == SOCKET_ERROR) {
			printf("[%.2f] --> failed setsockopt rcvbuf %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
			return FAILED_SEND;
		}
		kernelBuffer = 20e6; // 20 meg
		if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&kernelBuffer, sizeof(int)) == SOCKET_ERROR) {
			printf("[%.2f] --> failed setsockopt sndbuf %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
			return FAILED_SEND;
		}
		// return of the handshake

		////////////////////////// get ready to recieve response and check for timeout //////////////////////////


		if ((packet_size = WaitForSingleObject(socketReceiveReady, this->RTO * 1e3)) == WAIT_FAILED) {
			printf("WaitForSingleObject error %d\n", WSAGetLastError());
			exit(-1);
		}
		else if (packet_size == WAIT_OBJECT_0) {
			break;
		}

		//ResetEvent(this->socketReceiveReady); // reset socket to not ready


	}

	////////////////////////// reading received data //////////////////////////
	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((packet_size = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.2f] <-- failed recvfrom with %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
		return FAILED_RECV;
	}

	// update stat thread with receiver info
	ReceiverHeader *rh = (ReceiverHeader*)ans;
	update_receiver_info(rh);

	// RTO calculation
	endRTT = clock();
	elapsed_time = (double)(endRTT - beginRTT)/CLOCKS_PER_SEC; // updating to get elapsed time for print after function is complete, in the main
	calculate_RTO(elapsed_time);

	// start stat thread
	this->stat = CreateThread(NULL, 0, statThread, s, 0, NULL);
	// start worker thread
	this->worker = CreateThread(NULL, 0, workerThread, this, 0, NULL);
	SetThreadAffinityMask(this->worker, 2); // for optimization - puts all the work on the 2nd core
	
	
	// flow control
	this->lastReleased = min(window_size, rh->recvWnd);
	ReleaseSemaphore(empty, this->lastReleased, NULL);
	 
	// post-flow control
	this->s->receiver_wind_size = rh->recvWnd;
	this->s->set_effective_win_size();
	// printf("Open complete\n");


	return STATUS_OK; // implement error checking for part 5
}



/******************** SEND ********************/
int SenderSocket::Send(char* charBuf, int bytes, int type) {

	// if socket not open... aka if send called without open
	if (sock == INVALID_SOCKET) {
		return NOT_CONNECTED;
	}
	
	////////////////////////// insert packet to semaphore //////////////////////////
	// HANDLE arr[] = { eventQuit, empty };
	// WaitForMultipleObjects(2, arr, false, INFINITE);
	WaitForSingleObject(empty, INFINITE);

	// no need for mutex as no shared variables are modified
	int slot = this->next_seq % this->W;
	Packet *p = pending_pkts + slot; // pointer to packet struct
	//SenderDataHeader* sdh = new SenderDataHeader();
	//p->buf = (char*)sdh;
	SenderDataHeader* sdh = (SenderDataHeader*)(p->buf);

	// printf("%d\n", bytes + sizeof(SenderDataHeader));

	// set up remaining fields in sdh and p
	p->seq = sdh->seq = this->next_seq;
	p->type = type; // data
	p->size = sizeof(SenderDataHeader) + bytes;
	sdh->flags = Flags(); // defaulted to 0's with memset
	/*switch (type) {
		case 0:
			sdh->flags.SYN = 1;
			break;
		case 1:
			sdh->flags.FIN = 1;
			break;
		case 3:
			this->lastSeq = this->next_seq + 1;
			if (this->debug)
				printf("SEND(): lastSeq is %d\n", this->lastSeq);
			break;
	}*/
	if (type == 3)
	 	this->lastSeq = this->next_seq + 1;
	// sdh->seq = this->next_seq;
	// printf("Size senderdata %d\n", sizeof(SenderDataHeader));
	memcpy(sdh + 1, charBuf, bytes); // copy actual contents after header

	next_seq++; // for next packet
	ReleaseSemaphore(full, 1, NULL);	
	InterlockedIncrement(&(this->pending_packets));
	//this->pending_packets++;
	// printf("SEND(): pending %ld\n", this->pending_packets);

	// send request (by worker thread!)

	// receive info (by worker thread!)

	// update stat thread with receiver info (in receiveACK function!)

	return STATUS_OK; // implement error checking for part 5
}



/******************** CLOSE ********************/
int SenderSocket::Close(double &elapsed_time) {
	if (sock == INVALID_SOCKET) {
		// sock set in Open
		return NOT_CONNECTED;
	}
	// this->Send(nullptr, 0, 1);
	// for select
	// timeval tp;
	// fd_set fd;
	// set timeout to RTO
	// tp.tv_usec = (this->RTO - int(this->RTO))*1e6; // get the decimals of the RTO
	// tp.tv_sec = int(this->RTO); // get the full seconds of RTO
	// struct sockaddr_in req;
	// initialize_sockaddr(req, this->targetHost, this->port);

	////////////////////////// finish threads before sending FIN //////////////////////////
	/*if ((packet_size = WaitForSingleObject(empty, INFINITE)) == WAIT_FAILED) {// wait for reciever to finish
		printf("WaitForSingleObject error %d\n", WSAGetLastError());
		exit(-1);
	}*/

	// semaphore closed
	if (WaitForSingleObject(this->finishSend, INFINITE) == WAIT_FAILED) { // for finishing while loop
		printf("WaitForSingleObject error %d\n", WSAGetLastError());
		exit(-1);
	}
	CloseHandle(worker); // join worker thread
	// sets the stat thread to complete so that it knows it is done printing
	SetEvent(this->s->isDone); // trigger waitforoneobject
	CloseHandle(stat); // join

	// testing Ngyuen's approach
	socketReceiveReady = WSACreateEvent();

	// reinding to socket (select)
	if (WSAEventSelect(sock, socketReceiveReady, FD_READ) == SOCKET_ERROR) {
		printf("socket error in WSAEventSelect %d\n", WSAGetLastError());
		exit(-1);
	}
	

	// send request for handshake
	SenderSynHeader sh{};
	SenderDataHeader sender_data{};
	sender_data.flags = Flags(0, 0, 1); // FIN is 1, rest are 0
	sender_data.seq = this->next_seq; //
	sh.sdh = sender_data;
	sh.lp = *(this->link_prop);

	elapsed_time = (double)(clock() - begin_transfer) / CLOCKS_PER_SEC;

	for (int i = 1; i <=6; ++i) {
		// tried too many times
		if (i == 6) return TIMEOUT;

		////////////////////////// send request to the server //////////////////////////
		if ((packet_size = sendto(sock, (char*)&sh, sizeof(SenderSynHeader), 0, (struct sockaddr*)&(this->request), sizeof(this->request))) == SOCKET_ERROR) {
			printf("[%.2f] --> failed sendto with %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
			return FAILED_SEND;
		}

		beginRTT = (double)(clock() - this->start_time) / CLOCKS_PER_SEC; // elapsed open is used for elapsed time if SYN and SYN-ACK work
		// printf("[%.2f] --> FIN %d (attempt %d of %d, RTO %.3f)\n", beginRTT, this->next_seq, i, 5, this->RTO);

		// return of the handshake
		
		////////////////////////// get ready to recieve response and check for timeout //////////////////////////
		if ((packet_size = WaitForSingleObject(socketReceiveReady, this->RTO * 1e3)) == WAIT_FAILED) {
			printf("WaitForSingleObject error %d\n", WSAGetLastError());
			exit(-1);
		}
		else if (packet_size == WAIT_OBJECT_0) {
			break;
		}
		printf("%X\n", packet_size);
	}
	// printf("%d\n", sizeof(SenderSynHeader));

	////////////////////////// reading received data //////////////////////////
	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if (recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size) == SOCKET_ERROR) {
		printf("[%.2f] <-- failed recvfrom with %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
		return FAILED_RECV;
	}
	//endRTT = clock();
	update_receiver_info((ReceiverHeader*)ans);

	printf("[%.2f] <-- FIN-ACK %d window %X\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, *(this->s->next_seq), this->s->receiver_wind_size);


	// close socket to end communication... if already closed or not open, error will yield
	if (closesocket(sock) == SOCKET_ERROR) {
		// should not even get here
		printf("[%.2f] --> failed to close socket with %d\n", (double)(clock() - start_time) / CLOCKS_PER_SEC, WSAGetLastError()); // check if this is how they want it
		return NOT_CONNECTED;
	}
	sock = INVALID_SOCKET;
	return STATUS_OK; // implement error checking for part 5
}




////////////////////////////////// H E L P E R   F U N C T I O N S //////////////////////////////////

//////////////////// worker thread functions //////////////////////////
void SenderSocket::runWorker(void)
{
	int old_wind_base = this->s->sender_wind_base;
	// bool retx = false;
	DWORD timeout;
	HANDLE events[] = { socketReceiveReady, full };
	long zero = 0;
	while (true)
	{
		if (this->retx_count >= 50) { // skip packet if too many retx
			printf("[%.2f] --> 50+ retx attempts occured with packet %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, this->s->sender_wind_base);
			InterlockedDecrement(&(this->pending_packets)); // 1 time for just base
			//this->pending_packets--;
			ReleaseSemaphore(empty, 1, NULL);
			this->s->sender_wind_base++;
			this->retx_count = 0;
		}
		// set timeout
		if (this->pending_packets != zero) // if worker and sender dont catch up to each other // pending packets
			timeout = this->RTO * 1e3; // also if the sender window + size (end of wind) is not greater than nextToSend (still inside of window)
		else
			timeout = INFINITE;
			// if (debug)
			// printf("RUNWORKER(): timeout is %d with nextToSend %d senderbase %d and window size %d\n", timeout, nextToSend, this->s->sender_wind_base, this->s->effective_wind_size);
		
		// printf("RUNWORKER(): pending_packets is %ld\n", this->pending_packets);
		// wait to see if times out or not
		int ret = WaitForMultipleObjects(2, events, false, timeout);
		// ret == array index of the object that satisfied the wait
		// printf("ret is %d\n", ret);
		switch (ret)
		{
		case WAIT_TIMEOUT:  // if break occurs, retx buffer here
			this->s->timeout_counter++;
			this->retx_count++;
			// retx = true;
			// retx this->pending_pkts[this->s->sender_wind_base % this->W];
			// printf("RUNWORKER(): TIMEOUT %d\n", this->s->sender_wind_base);
			send_packet(this->s->sender_wind_base);
			break;
		case 0:	// packet is ready in the socket - get the ACK (socketReceiveReady)
			// retx = receive_ACK(); // move senderBase; fast retx if 3 same ACK
			receive_ACK();

			break;
		case 1:	// packet is ready in the sender - send the packet (full)
			// retx = false;
			send_packet(nextToSend++);
			break;
			// 
		default:// errored out - WAIT_FAILED D:
			printf("[%.2f] --> WaitForMultipleObjects failed in worker thread.\nExiting...\n", (double)(clock() - start_time) / CLOCKS_PER_SEC);
			exit(-1);
		}
		//if (nextToSend == this->s->sender_wind_base || retx // first packet of window or just did a retx(timeout / 3 - dup ACK
		//	|| this->s->sender_wind_base != old_wind_base) { // senderBase moved forward
			// printf("timer reset\n");
			//old_wind_base = this->s->sender_wind_base; // in case the base moved forward, for later checks
			//this->calculate_RTO((double)(endRTT - beginRTT)/ CLOCKS_PER_SEC);
			// beginRTT = clock();
		// }
	}
}

DWORD WINAPI workerThread(LPVOID pParam) {
	SenderSocket *ss = (SenderSocket *)pParam;
	// set time-critical priority
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

	ss->runWorker();
	return 0;
}



//////////////////// stat thread function //////////////////////////
DWORD WINAPI statThread(LPVOID pParam)
{
	StatData* stat = (StatData*)pParam;
	while (WaitForSingleObject(stat->isDone, 2000) == WAIT_TIMEOUT) {
		int time = (clock() - stat->start_time) / CLOCKS_PER_SEC;
		printf("[%2d] B %6d ( %3.1f MB) N %6d T %d F %d W %d S %.3f Mbps RTT %.3f\n", time, stat->sender_wind_base, stat->get_data_ACKed(), *(stat->next_seq),
			stat->timeout_counter, stat->fast_retx_counter, stat->effective_wind_size, stat->get_goodput(), stat->RTT);
	}
	return 0;
}



//////////////////// send packet function //////////////////// 
bool SenderSocket::send_packet(int index) { // for Packet type only! // doesn't need to be a boolean
	if (index == 0) { // if this is the first packet - TODO: if we use buf with SYN, change to 1
		begin_transfer = clock();
	}
	// struct sockaddr_in req; // can this be shared across all sends - yes TODO
	// initialize_sockaddr(req, this->targetHost, this->port);
	this->pending_pkts[index % this->W].txTime = clock();

	if (sendto(sock, (char*)(this->pending_pkts[index % this->W].buf), this->pending_pkts[index % this->W].size, 0, (struct sockaddr*)&(this->request), sizeof(this->request)) == SOCKET_ERROR) {
		printf("[%2f] <-- Failed in sendto with %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
		exit(-1);
	}
	//if (this->debug)
	//	printf("SEND_PACKET(): packet %d sent\n", index);
	return true;
}

//////////////////// send packet function //////////////////// 
void SenderSocket::receive_ACK() {	
//	ResetEvent(this->socketReceiveReady); // reset socket to not ready

	// initializations for receiving
	char ans[MAX_PKT_SIZE];
	struct sockaddr_in response;
	int response_size = sizeof(response);

	// in theory select told us that sock is ready to recvfrom, so no need to reattempt
	if ((packet_size = recvfrom(sock, ans, MAX_PKT_SIZE, 0, (struct sockaddr*)&response, &response_size)) == SOCKET_ERROR) {
		printf("[%.2f] <-- failed recvfrom with %d\n", (double)(clock() - this->start_time) / CLOCKS_PER_SEC, WSAGetLastError());
		exit(-1);
	}

	// update stat thread with receiver info
	ReceiverHeader *rh = (ReceiverHeader*)ans;
	int ack = rh->ackSeq;
	//if (this->debug)
	//	printf("RECEIVE_ACK(): ACK %d received\n", rh->ackSeq);
	// printf("rh->ackSeq %d sender_base %d\t", rh->ackSeq, this->s->sender_wind_base);
	// check if same ack
	if (ack == this->s->sender_wind_base) { // if window base is same as ACK value (fast retx counter)
		this->sameack_count++;
		if (this->sameack_count == 3) { // only fast retx once
			// this->sameack_count = 0;
			send_packet(ack); // fast rext
			// if (this->debug)
			//	printf("RECEIVE_ACK(): reACKing %d\n", rh->ackSeq);
			this->s->fast_retx_counter++;
			this->retx_count++; // STILL DOING DFJDSFKASHFKLDSAFHDKSAL
			// return true; // tells the user to fast rext
		}
	}
	else { // if base is moved
		// pending packets update
		InterlockedAdd(&(this->pending_packets), (long)(this->s->sender_wind_base - ack)); // difference between sender wind base and ackseq many times
		//this->pending_packets += this->s->sender_wind_base - ack;

		// flow control
		update_receiver_info(rh); // moves the base
		// how much we can advance the semaphore
		int newReleased = this->s->sender_wind_base + this->s->effective_wind_size - this->lastReleased;
		// int newReleased = rh->ackSeq - this->s->sender_wind_base;
		ReleaseSemaphore(empty, newReleased, NULL);
		this->lastReleased += newReleased;

		// fast retx counting reset
		// update_receiver_info(rh);
		if (this->retx_count == 0)  // no prior retx and on x + y - 1, not x + y
			calculate_RTO((double)(clock() - this->pending_pkts[(ack - 1) % this->W].txTime) / CLOCKS_PER_SEC);
			//if (debug)
			//	printf("RECEIVE_ACK(): new RTT is %f with sampleRTT %f and value %d for endRTT %f and beginRTT %f\n", this->s->RTT, (double)(endRTT - this->pending_pkts[(rh->ackSeq - 1) % this->W].txTime) / CLOCKS_PER_SEC, rh->ackSeq - 1, (double)(clock() - endRTT) / CLOCKS_PER_SEC, (double)(clock() - this->pending_pkts[(rh->ackSeq - 1) % this->W].txTime) / CLOCKS_PER_SEC);
		//}
		//else {
			// if (debug)
			//	printf("RECEIVE_ACK(): value %d and same RTO", rh->ackSeq - 1);
		//}
		this->retx_count = 0; // for 50 max retx
		this->sameack_count = 0; // for fast retx
		if (this->lastSeq == ack) {
			ReleaseSemaphore(this->finishSend, 1, NULL);
			// if (debug)
			// printf("RECEIVE_ACK(): finishSend released with remaining_packets at %ld\n", this->pending_packets);
		}
	}
	
	// return false;
}



//////////////////// misc functions ////////////////////
int SenderSocket::initialize_sockaddr(struct sockaddr_in& server, char* host, int port) {

	// structure used in DNS lookups
	struct hostent *remote;

	// structure for connecting to server
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;					// sets the family to IPv4
	server.sin_port = htons(port);					// provides the port 0 to bind to... 

	if (host != NULL) {
		DWORD IP;
		// inet_pton(AF_INET, targetHost, &IP);
		IP = inet_addr(host);
		if (IP == INADDR_NONE) { //not an IP
			// if not a valid IP, then do a DNS lookup
			if ((remote = gethostbyname(host)) == NULL)
			{
				printf("[%.2f] --> target %s is invalid\n", (double)(clock() - start_time) / CLOCKS_PER_SEC, host);
				return INVALID_NAME;
			}
			else { // take the first IP address and copy into sin_addr 
				memcpy((char *)&(server.sin_addr), remote->h_addr, remote->h_length);
				IP = server.sin_addr.S_un.S_addr;
			}
		}
		else {
			server.sin_addr.S_un.S_addr = IP;
		}
	}
	else
		server.sin_addr.s_addr = INADDR_ANY;			// allows me to receive packets on all physics interfaces of the system

	return STATUS_OK;
}

void SenderSocket::update_receiver_info(ReceiverHeader* rh) {
	this->s->receiver_wind_size = rh->recvWnd;
	this->s->sender_wind_base = rh->ackSeq;
	this->s->set_effective_win_size();
	this->s->data_ACKed += packet_size * 1e-6; // bytes to megabytes
}

void SenderSocket::calculate_RTO(double sample_RTT) {
	double alpha = 0.125, beta = 0.25;
	this->s->RTT = (1 - alpha) * this->s->RTT + alpha * sample_RTT;
	devRTT = (1 - beta) * devRTT + beta * fabs(sample_RTT - this->s->RTT);
	RTO = this->s->RTT + 4 * max(devRTT, 0.010);
}

double SenderSocket::calcualte_ideal_rate() {
	return this->s->sender_wind_size * MAX_PKT_SIZE * 8 / (1000.0 * this->s->RTT); // ideal_rate = (windowSize*MAX_PKT_SIZE*8/1000)/estRTT
}
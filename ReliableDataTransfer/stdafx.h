/*
 * stdafx.h
 * CSCE 463 - Spring 2019
 * Feras Khemakhem
 */

#pragma once

// needed to use inet_addr in dns_funct with winsock2.h
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

// for inet_pton
#include <ws2tcpip.h>


#include <string>
#include <vector>
#include <math.h> // fabs
#include <algorithm> // min, max
#include <time.h>
#include <sys\timeb.h> 
#include <ctype.h>

// possible status codes from ss.Open, ss.Send, ss.Close
#define STATUS_OK 0 // no error
#define ALREADY_CONNECTED 1 // second call to ss.Open() without closing connection
#define NOT_CONNECTED 2 // call to ss.Send()/Close() without ss.Open()
#define INVALID_NAME 3 // ss.Open() with targetHost that has no DNS entry
#define FAILED_SEND 4 // sendto() failed in kernel
#define TIMEOUT 5 // timeout after all retx attempts are exhausted
#define FAILED_RECV 6 // recvfrom() failed in kernel

// linker values
#define FORWARD_PATH 0
#define RETURN_PATH 1

// variables required for ss
#define MAGIC_PORT 22345 // receiver listens on this port
#define MAX_PKT_SIZE (1500-28) // maximum UDP packet size accepted by receiver 

// checksum
#define CHKSM_TBLE_SIZE 256
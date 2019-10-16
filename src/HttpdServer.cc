#include <sysexits.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <thread>

#include "logger.hpp"
#include "HttpdServer.hpp"

#define MAXPENDING 5    /* Maximum outstanding connection requests */
#define RCVBUFSIZE 32   /* Size of receive buffer */

HttpdServer::HttpdServer(INIReader& t_config)
	: config(t_config)
{
	auto log = logger();

	string pstr = config.Get("httpd", "port", "");
	if (pstr == "") {
		log->error("port was not in the config file");
		exit(EX_CONFIG);
	}
	port = pstr;

	string dr = config.Get("httpd", "doc_root", "");
	if (dr == "") {
		log->error("doc_root was not in the config file");
		exit(EX_CONFIG);
	}
	doc_root = dr;
}

void HttpdServer::launch() {
	auto log = logger();

	log->info("Launching web server");
	log->info("Port: {}", port);
	log->info("doc_root: {}", doc_root);

	// Put code here that actually launches your webserver...
	addrinfo hints, *servinfo, *p;
	int rv;
  	int servSock;                    /* Socket descriptor for server */

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // Prefer IPv6, but allow IPv4
	hints.ai_socktype = SOCK_STREAM; // use TCP
	hints.ai_flags = AI_PASSIVE; // we're going to be a server

	if ((rv = getaddrinfo(nullptr, port.c_str(), &hints, &servinfo)) != 0) {
		log->info("Error getaddrinfo: {}", gai_strerror(rv));
		exit(1);
	}


	for (p = servinfo; p != nullptr; p = p->ai_next) {
		if ((servSock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			log->info("Socket Creating Error");
			continue;
		}
		if (bind(servSock, p->ai_addr, p->ai_addrlen) == -1) {
			close(servSock);
			log->info("Socket Bind Error");
			continue;
		}
		break;
	}

	freeaddrinfo(servinfo);
	if (p == nullptr) {
		log->info("Server failed to bind");
		exit(1);
	}
	/* Mark the socket so it will listen for incoming connections */
	if (listen(servSock, MAXPENDING) < 0) {
		log->info("listen() failed");
		exit(1);
	}

	sockaddr_in msgClntAddr; /* Client address */
	unsigned int clntLen;            /* Length of client address data structure */
	int clntSock;                    /* Socket descriptor for client */

	for (;;) /* Run forever */
	{
		/* Set the size of the in-out parameter */
		clntLen = sizeof(msgClntAddr);

		/* Wait for a client to connect */
		if ((clntSock = accept(servSock, (struct sockaddr *) &msgClntAddr, 
								&clntLen)) < 0)
			log->info("accept() failed");

		/* clntSock is connected to a client! */

		log->info("Handling client {}", inet_ntoa(msgClntAddr.sin_addr));
#if 1
		std::thread handleClient([this,clntSock]{HandleChildConnection(clntSock);});
		handleClient.detach();
#else
		if (!fork()) { // this is the child process
			close(servSock); // child doesn't need the listener
			log->info("Handling New Child {}", clntSock);
			while (1) {

			}
			close(clntSock);
			exit(0);
		}
		close(clntSock); // parent doesn't need this
#endif
	}

}

void HttpdServer::HandleChildConnection(int clntSock) {
	// set on an idle loop
	auto log = logger();
	log->info("Handling New Child {}", clntSock);
	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	bool handleCon = true;
	while(handleCon)
	{
		fd_set rfds;

		FD_ZERO(&rfds);
		FD_SET(clntSock, &rfds);
		const auto recVal = select(clntSock + 1, &rfds, NULL, NULL, &tv);
		switch(recVal)
		{
			case(0):
			{
				//Timeout
				log->info("Timeout {}", clntSock);
				handleCon = false;
				break;
			}
			case(-1):
			{
				//Error
				log->info("Error encountered in select {}", clntSock);
				break;
			}
			default:
			{
				/*Packet Data Type*/ 
				#pragma message("I guess another select inside a loop might be required!!")
				char pkt[100000];
				if(recv(clntSock, &pkt, 100000*sizeof(char), 0) < 0)
				{
					//Failed to Recieve Data
					log->info("Failed to receive data Child {}", clntSock);
					break;
				}
				else
				{
					//Recieved Data!!
					log->info("{}", pkt);
					// Handle request
					log->info("Received data Child {}", clntSock);
				}
				break;
			}
		}
	}
	log->info("Closing Child {}", clntSock);
	if (close(clntSock) != 0)
		log->info("Falied to close Child {}", clntSock);
}

void HttpdServer::ProcessRequest(std::string s) {
	auto log = logger();
	log->info("Closing Child {}", s);
}
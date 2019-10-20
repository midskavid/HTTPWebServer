#include <sysexits.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <thread>
#include <fstream>
#include <iostream>
#include "logger.hpp"
#include "HttpdServer.hpp"

#define MAXPENDING 5    /* Maximum outstanding connection requests */
#define RCVBUFSIZE 10000   /* Size of receive buffer */
#define SOCKET_READ_TIMEOUT_SEC 5

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
	// Handle ~...
	char* home;
	home = getenv("HOME");
	if (doc_root[0]=='~') {
		// realpath does not handle ~
		char *real_path=realpath((std::string (home)+doc_root.substr(1)).c_str(), nullptr);
		doc_root = std::string (real_path);
	}
	std::string mt = config.Get("httpd", "mime_types", "");
	if (mt=="") {
		log->error("mime_types was not in the config file");
		exit(EX_CONFIG);
	}
	mime_types=mt;
	if (mime_types[0]=='~') {
		// handle ~
		char *real_path=realpath((std::string (home)+mime_types.substr(1)).c_str(), nullptr);
		mime_types = std::string (real_path);
	}
}

void HttpdServer::ParseMimeFile() {
	std::ifstream fp(mime_types);
	auto log=logger();
	if (fp.fail()) {
		log->error("MimeFile Not Found!!!");
		return;
	}
	std::string key, val;
	while (fp>>key>>val){
		mimeTypes[key]=val;
	}
	log->info("Mime File parsed");
}

void HttpdServer::launch() {
	auto log = logger();

	log->info("Launching web server");
	log->info("Port: {}", port);
	log->info("doc_root: {}", doc_root);
	log->info("mime_types: {}", mime_types);

	log->info("Parsing MimeFile");
	ParseMimeFile();

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
	bool handleCon = true;
	timeval tv;
	tv.tv_sec = SOCKET_READ_TIMEOUT_SEC;
	tv.tv_usec = 0;
#if 0
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
				char pkt[RCVBUFSIZE];
				memset(pkt, 0, RCVBUFSIZE);
				int len = 0;
				if((len=recv(clntSock, &pkt, RCVBUFSIZE*sizeof(char), 0)) < 0)
				{
					//Failed to Recieve Data
					log->info("Failed to receive data Child {}", clntSock);
					break;
				}
				else
				{
					//Recieved Data!!
					// Handle request
					ProcessRequest(pkt, len, clntSock);
					log->info("Received data Child {}", clntSock);
				}
				break;
			}
		}
	}
#else
	setsockopt(clntSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	std::string pending="";
	while (handleCon) {
		char pkt[RCVBUFSIZE];
		memset(pkt, 0, RCVBUFSIZE);
		auto recvLen = recv(clntSock, &pkt, RCVBUFSIZE*sizeof(char), 0);
		if (recvLen==0) {
			log->info("Connection closed by cliet {}", clntSock);
			handleCon=false;
		}
		else if (recvLen==-1) {
			//Error occurred
			if ((errno == EAGAIN) || (errno != EWOULDBLOCK)) {
				// TimedOut!
				log->info("Timeout {}", clntSock);
				handleCon = false;
				if (pending.length()>0)
				#pragma message("you might want to check if the socket has something to be read too...")
					HandleTimeOut(clntSock);
				break;
			}
		}
		else {
			// Handle msg.. In case of a partial msg, keep it in a buffer and append.
			std::string allData(pkt);
			allData = pending+allData;
			auto pos = allData.rfind("\r\n\r\n");
			if (pos!=std::string::npos) {
				// Found something.. may have multiple msgs.. might also have a pending msg...
				auto msg = allData.substr(0,pos+4);
				handleCon = ProcessRequests(msg, clntSock);
				if (pos+4!=allData.length()) {
					pending = allData.substr(pos+4+1,allData.length()-pos-4-1); 
				}
				else
					pending="";
			}
			else {
				// partial msg
				pending=allData;
			}
		}
	}
#endif
	log->info("Closing Child {}", clntSock);
	if (close(clntSock) != 0)
		log->info("Falied to close Child {}", clntSock);
}

bool HttpdServer::ProcessRequests(std::string msgs, int clntSock) {
	auto log = logger();
	std::string delimiter = "\r\n";
	size_t pos=0;
	while ((pos = msgs.find(delimiter+delimiter)) != std::string::npos) {
		std::string response = "";
		std::string s = msgs.substr(0, pos+delimiter.length()); 
		msgs.erase(0, pos+2*delimiter.length());
		std::vector<std::string> request;
		bool sendBinary=false;
		int getFileSize=0;

		while ((pos = s.find(delimiter)) != std::string::npos) {
			auto token = s.substr(0, pos);
			log->info("{}",token);
			request.emplace_back(token);
			s.erase(0, pos+delimiter.length());
		}

		// Check request is well formed..
		// Verify Header...
		bool malformed=false; 
		if (request[0].substr(0,4) != "GET ")
			malformed=true;
		if (request[0].substr(request[0].length()-9) != " HTTP/1.1")
			malformed=true;

		bool closeConnection=false;
		bool hostFound=false;
		for (size_t ii=1; ii<request.size(); ++ii){
			// verify key value pairs..
			pos=request[ii].find(": ");
			if (pos==std::string::npos)
				malformed=true;
			// Check if another key value exists without CRLF..
			if (request[ii].rfind(": ")!=pos)
				malformed=true;
			if (request[ii].substr(0,pos)=="Connection"&&request[ii].substr(pos+2,request[ii].length()-pos-2)=="close")
				closeConnection=true;
			if (request[ii].substr(0,pos)=="Host")
				hostFound=true;
		}

		std::string path=request[0].substr(4,request[0].length()-8-5);
		if (malformed || !hostFound) {
			response = CreateErrorResponse(ErrorResponse::ERR400);
			closeConnection=true;
		}
		else if (!VerifyRequestPath(path)) {
			response = CreateErrorResponse(ErrorResponse::ERR404);
		}
		else {
			// Create response..
			struct stat attrib;
			auto fileExists = stat(path.c_str(), &attrib)==0;
			if (!fileExists) {
				response = CreateErrorResponse(ErrorResponse::ERR404);
			}
			else {
				auto posE = path.rfind(".");
				std::string extn = "";
				if (posE==std::string::npos) {
					// no extension!!!!
					#pragma message("No extension found!!!")
					sendBinary=false;
				}
				else {
					extn=path.substr(posE);
					sendBinary=true;
				}
				log->info("File extension {}",extn);
				// https://stackoverflow.com/questions/13542345/how-to-convert-st-mtime-which-get-from-stat-function-to-string-or-char
				time_t t = attrib.st_mtime;
				struct tm lt;
				localtime_r(&t, &lt);
				char timbuf[80];			
				strftime(timbuf, sizeof(timbuf), "%d.%m.%Y %H:%M:%S", &lt);
				getFileSize = int(attrib.st_size);
				std::string fileSize = std::to_string(getFileSize);
				std::string extenFound = "application/octet-stream";
				if (mimeTypes.count(extn)>0) {
					extenFound = mimeTypes[extn];
				}
				
				if (!closeConnection)
					response=std::string("HTTP/1.1 200 OK\r\nServer: MyServer 1.0\r\nLast-Modified: ")+std::string(timbuf)+std::string("\r\nContent-Length: ")+fileSize+std::string("\r\nContent-Type: ")+extenFound+std::string("\r\n\r\n");
				else 
					response=std::string("HTTP/1.1 200 OK\r\nServer: MyServer 1.0\r\nLast-Modified: ")+std::string(timbuf)+std::string("\r\nContent-Length: ")+fileSize+std::string("\r\nContent-Type: ")+extenFound+std::string("\r\nConnection: close")+std::string("\r\n\r\n");
				log->info("{}",response);
			}
		}
		
		int ret=0;
		int len=response.length();
		do {
			ret=send(clntSock, response.c_str(), response.size(), 0);
			if (ret < 0) {
				log->error("Client closed connection");
				closeConnection=true;
				break;
			}
			response = response.substr(0,response.length()-ret);
			ret=0;
			len=response.length();
		}while (len<ret);

		if (sendBinary) {
			// open file and send...
			int ret=0;
			//std::ifstream inputF(path, std::ios::binary );
			FILE *fptr = fopen(path.c_str(),"rb");
			int inputF = fileno(fptr);
			off_t offset=0;
			do {
				ret = sendfile(clntSock, inputF , &offset, getFileSize);
				getFileSize -= ret;
			}while(getFileSize>0);

		}
		if (closeConnection || malformed){
			// break here ignoring other requests and close connection
			return false;
		}
	}
	return true;
}

bool HttpdServer::VerifyRequestPath(std::string& path) {
	auto log = logger();
	if (path[0]!='/')
		return false;
	#pragma message (" verify ending / or begining / ")
	if (path=="/")
		path="/index.html";

	path=doc_root+path; // remove /
	ParsePath(path);

	// now check if the initial portions match..
	if (doc_root != path.substr(0,doc_root.length())) {
		log->info ("Malformed URL");
		return false;
	}
	return true;
}

void HttpdServer::ParsePath(std::string& path) {
	auto log = logger();
	char *real_path = realpath(path.c_str(), nullptr);
	if (real_path==nullptr) {
		log->error("Null string constructed by realpath.. Path given to it {}", path);
		return;
	}
	std::string newPath(real_path);
	log->info("OldPath {}", path);
	log->info("NewPath {}", newPath);
	path=newPath;
}

std::string HttpdServer::CreateErrorResponse(ErrorResponse err) {
	auto log = logger();
	switch (err)
	{
	case ErrorResponse::ERR400 :
		{
		std::string err = "HTTP/1.1 400 Malformed Request\r\n";
		return err;
		}
	case ErrorResponse::ERR404 :
		{
		std::string err = "HTTP/1.1 404 Requested file not found.\r\n";
		return err;
		}
	default :
		return "";
	}
}

void HttpdServer::HandleTimeOut(int clntSock) {
	std::string err = "HTTP/1.1 400 Connection TimedOut\r\n";
	auto log=logger();
	int ret=0;
	int len=err.length();
	do {
		int ret = send(clntSock, err.c_str(), err.length(), 0);
		if (ret == -1) {
			log->error("Error occurred while sending.. exit for this child");
			return;
		}
		err=err.substr(0,ret);
		len=len-ret;
		ret=0;
	} while(ret<len);
}
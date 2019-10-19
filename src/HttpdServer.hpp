#ifndef HTTPDSERVER_HPP
#define HTTPDSERVER_HPP

#include "inih/INIReader.h"
#include "logger.hpp"
#include <unordered_map>

using namespace std;

class HttpdServer {
	enum ErrorResponse {ERR404, ERR400};

	// Member Functions
	void ProcessRequests(std::string, int);
	void ParsePath(std::string& path);
	void HandleChildConnection(int);
	void HandleTimeOut(int);
	void ParseMimeFile();
	bool VerifyRequestPath(std::string& path);
	std::string CreateErrorResponse(ErrorResponse);
	
	// Member variables
	std::unordered_map<std::string,std::string> mimeTypes;

public:
	HttpdServer(INIReader& t_config);
	void launch();

protected:
	INIReader& config;
	string port;
	string doc_root;
	std::string mime_types;
};

#endif // HTTPDSERVER_HPP

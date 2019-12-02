// Socket.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "Socket.h"
#include "afxsock.h"
#include "Winsock2.h"
#include <iostream>
#include <string>
#include <windows.h>
#include <stdio.h>
#include <fstream>
#include <thread>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#define PORT 8888
#define IP "127.0.0.1"
#define FILE "blacklist.conf"
#define TTL 180000

// The one and only application object

CWinApp theApp;

using namespace std;

//class support caching
class later
{
public:
	template <class callable, class... arguments>
	later(int after, bool async, callable&& f, arguments&&... args)
	{
		std::function<typename std::result_of<callable(arguments...)>::type()> task(std::bind(std::forward<callable>(f), std::forward<arguments>(args)...));

		if (async)
		{
			std::thread([after, task]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(after));
				task();
			}).detach();
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(after));
			task();
		}
	}

};

//Get port
int get_port(char* buf)
{
	string request = buf;
	int pos = request.find(".com:");
	string temp = request.substr(pos + 5); //lay chuoi co sau vi tri post 6 ki tu
	pos = temp.find("\r\n");
	temp.erase(pos);
	char* port = new char[temp.length() + 1];
	for (int i = 0; i < temp.length(); i++)
	{
		port[i] = temp[i];
	}
	port[temp.length()] = '\0';
	return atoi(port);
}

wchar_t *convertCharArrayToLPCWSTR(const char* charArray)
{
	wchar_t* wString = new wchar_t[4096];
	MultiByteToWideChar(CP_ACP, 0, charArray, -1, wString, 4096);
	return wString;
}

// get IP address from host name
char* get_ip(char* host)
{
	hostent *h = gethostbyname(host);
	unsigned char *addr = reinterpret_cast<unsigned char *>(h->h_addr_list[0]);
	int add[4];
	copy(addr, addr + 4, add);
	string temp = "";
	temp += to_string(add[0]) + "." + to_string(add[1]) + "." + to_string(add[2]) + "." + to_string(add[3]);
	char* ip = new char[temp.length() + 1];
	for (int i = 0; i < temp.length(); i++)
	{
		ip[i] = temp[i];
	}
	ip[temp.length()] = '\0';
	return ip;
}

// get the host name from the request
char* get_host(char* buf)
{
	string request = buf;
	int pos = request.find("Host: ");
	string temp = request.substr(pos + 6);
	pos = temp.find("\r\n");
	temp.erase(pos);
	pos = temp.find(":");
	if (pos!=-1)
		temp.erase(pos);
	char* host = new char[temp.length() + 1];
	for (int i = 0; i < temp.length(); i++)
	{
		host[i] = temp[i];
	}
	host[temp.length()] = '\0';
	return host;
}

// check if host name is exist in blacklist file
bool check(string fileName, char* host)// return true if exist
{
	fstream f;
	string line;
	f.open(fileName, ios::in);
	while (getline(f, line))
	{
		if (line.find(host, 0) != string::npos)
		{
			f.close();
			return true;
		}
	}
	f.close();
	return false;
}

//http 1.0 or 1.1
char *get_http_type(char *buffer)
{
	string package = buffer;
	int pos = package.find("HTTP/");
	string segment = package.substr(pos);
	pos = segment.find("\r\n");
	segment.erase(pos);
	char *result = (char*)calloc(segment.length() + 1, sizeof(char));
	for (int i = 0; i < segment.length(); i++)
		result[i] = segment[i];
	strcat(result, "\0");
	return result;
}

//build 403 error http reponse with type input is http 1.0/1.1
char *build_403_error(char *type)
{
	char *query;
	char *error = " 403 Forbidden\r\nContent-Type: text/html\r\n\r\n";
	char *body = "<html>\r\n<h1>403 FORBIDDEN</h1>\r\n<h3>You are not allow to connect to this page</h3>\r\n</html>";
	query = (char*)calloc(strlen(type) + strlen(error)+strlen(body), sizeof(char));
	strcpy(query, type);
	strcat(query, error);
	strcat(query, body);
	return query;
}

//delete cache file and file name in cache database
void remove_cache(char *filename)
{
	string name = filename;
	fstream f;
	f.open("cache.txt", ios::in | ios::out | ios::app);
	fstream temp;
	temp.open("temp.txt", ios::in | ios::out | ios::app);
	string line;
	while (getline(f, line))
	{
		if (line.find(name) != string::npos)
		{
			line.replace(line.find(name), name.length(), "");
		}
		if (!line.empty())
		{
			temp << line << endl;
		}
	}
	f.close();
	temp.close();
	remove("cache.txt");
	rename("temp.txt", "cache.txt");
	string file = name;
	char* t_fileName = new char[file.length() + 1];
	for (int i = 0; i < file.length(); i++)
	{
		t_fileName[i] = file[i];
	}
	t_fileName[file.length()] = '\0';
	remove(t_fileName);
}

//thread keep cache file a live
DWORD WINAPI check_cache(LPVOID arg)
{
	char *host = (char*)arg;
	later wait_til_end_ttl(TTL, false, &remove_cache, host);
	return true;
}

void cache_file_init(char *filename)
{
	fstream fh;
	char *file = (char*)calloc(strlen(filename) + 4, sizeof(char));
	strcpy(file, filename);
	strcat(file, ".txt");
	fh.open(file, ios::out);
	fh.close();
}

//create cache file and insert file name into database
void insert_cache(char *filename, char* src)
{
	fstream f;
	f.open("cache.txt", ios::out | ios::app);
	f << filename << endl;
	f.close();
	fstream fh;
	char *file = (char*)calloc(strlen(filename) + 4, sizeof(char));
	strcpy(file, filename);
	strcat(file, ".txt");
	fh.open(file, ios::out | ios::app);
	fh << src << endl;
	fh.close();
	CreateThread(NULL, 0, check_cache, file, 0, NULL);
}

//
char *get_cache_data(char *filename)
{
	char *res;
	char *file = (char*)calloc(strlen(filename) + 4, sizeof(char));
	strcpy(file, filename);
	strcat(file, ".txt");
	ifstream f(file);
	f.seekg(0, f.end);
	int size = f.tellg();
	f.seekg(0, f.beg);
	res = (char*)calloc(size, sizeof(char));
	f.read(res, size);
	f.close();
	return res;
}

//
void clear_cache_database()
{
	fstream f;
	f.open("cache.txt", ios::out);
	f.close();
}

//thread receive http request from client
DWORD WINAPI Permission_checking(LPVOID arg)
{
	SOCKET *tClient = (SOCKET*)arg;
	CSocket Client;
	Client.Attach(*tClient);

	//nhan http request tu client
	int result;
	int tmpres;
	int count = 0;

	char *rev_buffer = (char*)calloc(102400, sizeof(char));
	char *res_buffer = (char*)calloc(10240, sizeof(char));
	result = Client.Receive(rev_buffer, 102400, 0);
	if (result == SOCKET_ERROR)
	{
		Client.Close();
		cout << "Receive: receive failed with error!" << endl;
	}
	else if (result > 0)
	{
		strcat(rev_buffer, "\0");
		//check host name and prepare http response
		char *host = get_host(rev_buffer);
		char *http_type = get_http_type(rev_buffer);
		if (check(FILE, host))
		{
			res_buffer = build_403_error(http_type);
			Client.Send(res_buffer, strlen(res_buffer), 0);
		}
		else
		{
			if (check("cache.txt", host))
			{
				res_buffer = get_cache_data(host);
				Client.Send(res_buffer, strlen(res_buffer), 0);
			}
			else
			{
				/*connect and get response from server procedure*/
				CSocket Webserver;
				Webserver.Create();
				char *ip = get_ip(host);
				UINT port = (UINT)get_port(rev_buffer);
				if (port == 0) port = 80;
				if (Webserver.Connect(convertCharArrayToLPCWSTR(ip), port) < 0)
				{
					res_buffer = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n<html>\r\n<h1>404 NOT FOUND</h1>\r\n<h3>Your requested web page was not found</h3>\r\n</html>";
					cout << "Connect to server failed!" << endl;
					Client.Send(res_buffer, strlen(res_buffer), 0);
				}
				else
				{
					cout << "Connect successful! Host: " << host << endl;
					int sent = 0;
					cout << rev_buffer << endl;
					while (sent < strlen(rev_buffer))
					{
						tmpres = Webserver.Send(rev_buffer + sent, strlen(rev_buffer) - sent, 0);
						if (tmpres == -1)
						{
							cout << "Send request failed! Host: " << host << endl;
							Webserver.Close();
							return 0;
						}
						sent += tmpres;
					}
					cout << "Send successful!" << endl;
					memset(res_buffer, 10240, 0);
					cache_file_init(host);
					while ((tmpres = Webserver.Receive(res_buffer, 10240, 0)) > 0)
					{
						if (tmpres > 0)
						{
							Client.Send(res_buffer, tmpres, 0);
							//insert_cache(host, res_buffer);
						}
					}
					if (tmpres < 0)
					{
						cout << "Receive failed!" << endl;
					}
					Webserver.Close();
				}
			}
		}
	}
	return result;
}
	
int main()
{

    int nRetCode = 0;

    HMODULE hModule = ::GetModuleHandle(nullptr);

	if (hModule != nullptr)
	{
		// initialize MFC and print and error on failure
		if (!AfxWinInit(hModule, nullptr, ::GetCommandLine(), 0))
		{
			// TODO: code your application's behavior here.
			wprintf(L"Fatal Error: MFC initialization failed\n");
			nRetCode = 1;
		}
		else
		{
			// TODO: code your application's behavior here.
			if (AfxSocketInit() == FALSE)
			{
				cout << "Khong the khoi tao Socket Libraray";
				return FALSE;
			}

			CSocket ProxyServer;
			//Tao socket cho server port 8888, giao thuc TCP
			if (ProxyServer.Create(PORT, SOCK_STREAM, _T(IP)) == 0)
			{
				cout << "Khoi tao that bai!!!" << endl;
				cout << ProxyServer.GetLastError() << endl;
				return false;
			}
			else
			{
				cout << "Server khoi tao thanh cong !!!" << endl;

				if (ProxyServer.Listen(1) == false)
				{
					cout << "Khong the lang nghe tren port nay!!!" << endl;
					ProxyServer.Close();
					return false;
				}
			}

			//Khoi tao mot socket de duy tri viec ket noi & trao doi du lieu
			CSocket Connector;
			clear_cache_database();
			do
			{
				cout << "Server dang lang nghe ket noi tu client..." << endl;
				if (ProxyServer.Accept(Connector))
				{
					cout << "Co client ket noi!" << endl;
					SOCKET *tConnector = new SOCKET();
					*tConnector = Connector.Detach();
					CreateThread(NULL, 0, Permission_checking, tConnector, 0, NULL);
				}
			} while (ProxyServer.Listen());	
			Connector.Close();
			ProxyServer.Close();
        }
    }
    else
    {
        // TODO: change error code to suit your needs
        wprintf(L"Fatal Error: GetModuleHandle failed\n");
        nRetCode = 1;
    }

    return nRetCode;
}
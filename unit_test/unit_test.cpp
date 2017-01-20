#include "../include/xsocket.io.hpp"


int main()
{
	xsocket_io::xserver server;
	server.bind("127.0.0.1", 9001);
}
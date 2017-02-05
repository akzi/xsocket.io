#include "../include/xsocket.io.hpp"


struct login
{
	int32_t numUsers;
	XGSON(numUsers);
};
struct stop_type 
{
	std::string username;
	XGSON(username);
};
struct new_message
{
	std::string username;
	std::string message;
	XGSON(username, message);
};
typedef stop_type typing;

int main()
{
	int32_t sessions = 0;
	xsocket_io::xserver server;
	server.bind("127.0.0.1", 3001);
	server.start();
	server.set_static("public/");

	server.on_connection([&](xsocket_io::session &sess) {

		sessions++;

		sess.on("add user", [&](xjson::obj_t &obj) {
			sess.set("username", obj.get<std::string>());
			sess.emit("login", login{sessions});
		});

		sess.on("new message", [&](xjson::obj_t &obj){
			new_message msg;
			msg.message= obj.get<std::string>();
			msg.username = sess.get("username");
			sess.broadcast.emit("new message", msg);
		});

		sess.on("stop typing", [&sess]{
			sess.broadcast.emit("stop typing", stop_type{sess.get("username")});
		});
		sess.on("typing", [&sess] {
			sess.broadcast.emit("typing", typing{ sess.get("username")});
		});
	});
	getchar();
}
#include "../include/xsocket.io.hpp"
#include "../../xtest/include/xtest.hpp"

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
typedef stop_type typing;

struct new_message
{
	std::string username;
	std::string message;
	XGSON(username, message);
};
struct user_join
{
	std::string username;
	int32_t numUsers;
	XGSON(username, numUsers);
};

typedef  user_join user_left;

XTEST_SUITE(protoc)
{
	XUNIT_TEST(decode)
	{
		auto str = "23:42[\"new message\",\"sss\"]17:42[\"stop typing\"]";
		auto packets = xsocket_io::detail::decode_packet(str, false);
	}
}

XTEST_SUITE(base64)
{
	XUNIT_TEST(decode)
	{
		auto str = "AAgG/zB7InNpZCI6IkZoak1UOS1fTzZGLU82OWJBQUFqIiwidXBncmFkZXMiOltdLCJwaW5nSW50ZXJ2YWwiOjI1MDAwLCJwaW5nVGltZW91dCI6NjAwMDB9";
		std::string result;
		assert(xutil::base64::decode(str, result));

		str = "AAkF/zB7InBpbmdJbnRlcnZhbCIgOiA1MDAwLCAicGluZ1RpbWVvdXQiIDogMTIwMDAsICJzaWQiIDogIk5UTXhPRFF6TnpJek16VXpPVFkiLCAidXBncmFkZXMiIDogW119";
		std::string result2;
		assert(xutil::base64::decode(str, result2));

		str = "AAH/Mw==";
		std::string result3;
		assert(xutil::base64::decode(str, result3));


		str = "AAH/Mw==";

		str = "AAL/NDA=";

		str = "AAL/NDA=";
	}
}

XTEST_SUITE(main)
{
	XUNIT_TEST(main)
	{

		int32_t sessions = 0;
		xsocket_io::xserver server;
		server.bind("127.0.0.1", 3001);
		server.start();
		server.set_static("public/");

		server.on_connection([&] (xsocket_io::session &sess) {

			sess.on("add user", [&](xjson::obj_t &obj) {
				
				if (sess.get("add_user").size()) return;
				sess.set("add_user","true");

				sessions++;
				sess.set("username", obj.get<std::string>());
				sess.emit("login", login{ sessions });
				sess.broadcast.emit("user joined",  user_join{ 
					sess.get("username"),sessions 
				});
			});

			sess.on("new message", [&](xjson::obj_t &obj) {
				new_message msg;
				msg.message = obj.get<std::string>();
				msg.username = sess.get("username");
				sess.broadcast.emit("new message", msg);
			});

			sess.on("stop typing", [&sess] {
				sess.broadcast.emit("stop typing", stop_type{ 
					sess.get("username") 
				});
			});
			sess.on("typing", [&sess] {
				sess.broadcast.emit("typing", typing{ 
					sess.get("username") 
				});
			});

			sess.on("disconnect", [&] {
				if (sess.get("add_user").size())
					sessions--;
				sess.broadcast.emit("user left", user_left{ 
					sess.get("username"), sessions 
				});
			});
		});
		getchar();
	}
}

xtest_run;
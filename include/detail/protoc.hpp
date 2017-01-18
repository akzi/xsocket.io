#pragma once
namespace xsocket_io
{
	namespace detail
	{
		enum packet_type
		{
			e_open,
			e_close,
			e_ping,
			e_pong,
			e_message,
			e_upgrade,
			e_noop
		};
		enum class error_msg
		{
			e_transport_unknown,
			e_session_id_unknown,
			e_bad_handshake_method,
			e_bad_request,
		};
		inline std::string to_json(error_msg msg)
		{
			if (msg == error_msg::e_bad_handshake_method)
				return "{\"code\":2, \"message\":\"Bad handshake method\"}";
			else if (msg == error_msg::e_bad_request)
				return "{\"code\":3, \"message\":\"Bad request\"}";
			else if (msg == error_msg::e_session_id_unknown)
				return "{\"code\":1, \"message\":\"Session ID unknown\"}";
			else if (msg == error_msg::e_transport_unknown)
				return "{\"code\":0, \"message\":\"Transport unknown\"}";
			assert(false);
			return{};
		}
		inline std::string packet_msg(const std::string &msg, packet_type type, bool binary)
		{
			if (!binary)
				return std::to_string(msg.size() + 1)+ ":" + std::to_string(type) + msg;
		}

		struct open_msg 
		{
			std::string sid;
			std::vector<std::string> upgrades;
			int64_t pingInterval;
			int64_t pingTimeout;
			XGSON(sid, upgrades, pingInterval, pingTimeout);
		};


	}
}
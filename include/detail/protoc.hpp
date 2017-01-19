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
		enum playload_type
		{
			e_connect,
			e_disconnect,
			e_event,
			e_ack,
			e_error,
			e_binary_event,
			e_binary_ack
		};
		enum class error_msg
		{
			e_transport_unknown,
			e_session_id_unknown,
			e_bad_handshake_method,
			e_bad_request,
		};
		struct packet
		{
			std::string playload_;
			packet_type packet_type_;
			playload_type playload_type_;
			int64_t packet_id_ = 0;
			std::string nsp_;
			bool binary_ = false;
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

		inline std::string packet_msg(const packet &_packet)
		{
			if (!_packet.binary_)
			{
				return std::to_string(_packet.playload_.size() + 1) + ":"
					+ std::to_string(_packet.playload_type_) + _packet.playload_;
			}
		}
		inline std::list<packet> unpacket(const std::string &data,bool binary)
		{

			#define assert_ptr()\
			if(pos >= end) \
				throw std::logic_error("parse packet len error");

			std::list<packet> packets;

			auto pos = (char*)data.c_str();
			auto end = pos + data.size();
			do
			{
				packet _packet;
				packet_type _packet_type;
				playload_type _playload_type;
				long len = 0;
				if (!binary)
				{
					char *ptr = nullptr;
					len = std::strtol(pos, &ptr, 10);
					if (!len || !ptr || (*ptr != ':'))
						throw std::logic_error("parse packet len error");
					++ptr;
					assert_ptr();
					pos = ptr;
				}
				else
				{
					std::string len_str;
					if (*pos != 0 && *pos != 1)
						throw std::runtime_error("packet error");
					bool playload_binary = *pos == 1;
					++pos;
					do
					{
						len_str.push_back(*pos);
					} while (*pos != 255);

					auto len = std::strtol(len_str.c_str(), nullptr, 10);
					if (!len)
						throw std::logic_error("parse packet len error");
					++pos;
					assert_ptr();
				}
				
				char ch = *pos;
				if (ch < e_open || ch > e_noop)
					throw std::logic_error("parse packet type error");
				++pos;
				assert_ptr();
				_packet_type = static_cast<packet_type>(ch);

				ch = *pos;
				if (ch < e_connect || ch > e_binary_ack)
					throw std::logic_error("parse playload type error");
				_playload_type = static_cast<playload_type>(ch);
				++pos;
				assert_ptr();
				if (len > 1)
				{
					_packet.playload_.append(pos, len - 1);
					pos += len;
				}
				_packet.binary_ = false;
				_packet.packet_type_ = _packet_type;
				_packet.playload_type_ = _playload_type;
				packets.emplace_back(std::move(_packet));
			} while (pos < end);

			return packets;
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
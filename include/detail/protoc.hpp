#pragma once
namespace xsocket_io
{
	namespace detail
	{
		enum packet_type
		{
			e_null0 = 100,
			e_open = 0,
			e_close,
			e_ping,
			e_pong,
			e_message,
			e_upgrade,
			e_noop
		};
		enum playload_type
		{
			e_null1 = 100,
			e_connect = 0,
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
			packet_type packet_type_ = packet_type::e_null0;
			playload_type playload_type_ = playload_type::e_null1;
			int64_t packet_id_ = 0;
			std::string nsp_;
			bool binary_ = false;
			bool is_string_ = false;;
		};
		inline std::string to_json(error_msg msg)
		{
			if (msg == error_msg::e_bad_handshake_method)
				return "{\"code\":2, ""\"message\":\"Bad handshake method\"}";
			else if (msg == error_msg::e_bad_request)
				return "{\"code\":3, \"message\":\"Bad request\"}";
			else if (msg == error_msg::e_session_id_unknown)
				return "{\"code\":1, \"message\":\"Session ID unknown\"}";
			else if (msg == error_msg::e_transport_unknown)
				return "{\"code\":0, \"message\":\"Transport unknown\"}";
			assert(false);
			return{};
		}
		inline std::string encode_packet(packet &_packet)
		{
			if (_packet.playload_type_ != playload_type::e_null1)
			{
				_packet.playload_ = 
					std::to_string(_packet.packet_type_) + 
					std::to_string(_packet.playload_type_) + 
					_packet.playload_;
			}
			else
				_packet.playload_ = std::to_string(_packet.packet_type_) + _packet.playload_;

			if (!_packet.binary_)
			{
				return std::to_string(_packet.playload_.size() + 1) + ":" + _packet.playload_;
			}
			
			auto playload_len_str = std::to_string(_packet.playload_.size());
			std::string buffer;
			buffer.resize(playload_len_str.size() + 2);

			buffer[0] = 1;
			if (_packet.is_string_)
				buffer[0] = 0;

			for (size_t i = 0; i < playload_len_str.size(); i++)
			{
				std::string temp;
				temp.push_back(playload_len_str[i]);
				auto num = std::strtol(temp.c_str(), 0, 10);
				buffer[i + 1] = ((char)num);
			}
			buffer[buffer.size() - 1] = (char)255;
			buffer.append(_packet.playload_);
			return buffer;
			assert(false);
			return{};
		}
		inline std::list<packet> decode_packet(const std::string &data,bool binary)
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
				packet_type _packet_type = e_null0;
				playload_type _playload_type = e_null1;
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
				
				char ch = *pos - '0';
				if (ch < e_open || ch > e_noop)
					throw std::logic_error("parse packet type error");
				_packet_type = static_cast<packet_type>(ch);
				++pos;
				if (_packet_type == packet_type::e_message)
				{
					assert_ptr();
					ch = *pos - '0';
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
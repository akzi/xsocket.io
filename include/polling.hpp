#pragma once
namespace xsocket_io
{
	class session;
	class request;
	namespace detail
	{

		class polling
		{
		public:
			polling(xnet::connection &&conn)
				:conn_(std::move(conn))
			{
				init();
			}
			~polling()
			{

			}
			void close()
			{
				is_close_ = true;
			}
			
			void send(detail::packet_type _packet_type, detail::playload_type _playload_type, std::string &&_play_load)
			{
				detail::packet _packet;
				_packet.binary_ = _packet_type == detail::e_binary_event;
				_packet.packet_type_ = _packet_type;
				_packet.playload_ = std::move(_play_load);
				_packet.packet_type_ = _packet_type;
				if (!polling_ || packet_buffers_.size())
				{
					packet_buffers_.emplace_back(std::move(_packet));
					return;
				}
				auto buffer = detail::packet_msg(_packet);
				send_data(buffer, 200);
				is_sending_ = true;
				polling_ = false;
			}
		private:
			friend class session;

			void init()
			{
				
			}
			
			std::string gen_sid()
			{
				static std::atomic_int64_t uid = 0;
				auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
				auto id = uid.fetch_add(1) + now;
				return xutil::base64::encode(std::to_string(id));
			}
			bool check_sid(const std::string &sid)
			{
				assert(check_sid_);
				return check_sid_(sid);
			}

			bool set_timer(const std::function<void()> &action)
			{
				if (timer_id_)
					del_timer_(timer_id_);

				timer_id_ = set_timer_(ping_timeout_, [this, action] {
					action();
					timer_id_ = 0;
					return false;
				});
			}
			
			void set_heartbeat_timeout()
			{
				set_timer([this] {
					detail::packet _packet;
					_packet.packet_type_ = detail::packet_type::e_close;
					_packet.playload_type_ = detail::playload_type::e_disconnect;
					send_data(detail::packet_msg(_packet), 200);
					on_close();
				});
			}
			void on_polling_req()
			{
				using namespace detail;
				using strncasecmper = xutil::functional::strncasecmper;
				auto sid = http_parser_.get_header<strncasecmper>("sid");
				set_heartbeat_timeout();
				if (sid.empty())
				{
					open_msg msg;
					msg.pingInterval = ping_interval_;
					msg.pingTimeout = ping_timeout_;
					msg.sid = gen_sid();
					msg.upgrades = get_upgrades_(query_.get("transport"));
					xjson::obj_t obj;
					msg.xpack(obj);
					auto data = obj.str();
					auto buffer = std::to_string(data.size() + 1) + ":0" + data;
					send_data(buffer, 200);
					return;
				}
				if (!check_sid(sid))
				{
					send_data(to_json(error_msg::e_session_id_unknown), 400, false);
					return;
				}
				flush();

			}
			void on_packet(const std::list<detail::packet> &_packet)
			{
				for (auto &itr : _packet)
					on_packet_(itr);
			}
			void on_request()
			{
				
			}
			

			bool check_static(const std::string &url)
			{
				std::string filepath;
				if (!check_static_(url, filepath))
					return false;
				return true;
			}

			bool check_transport(const std::string &transport)
			{
				assert(check_transport_);
				return check_transport_(transport);
			}
			

			bool support_binary_ = false;
			
			bool polling_ = false;
			uint32_t ping_interval_ = 25000;
			uint32_t ping_timeout_ = 60000;
			std::size_t	timer_id_ = 0;

			xhttper::query query_;
		
			std::list<packet> packet_buffers_;
			std::function<void()> on_heartbeat_;
			std::function<void(detail::packet)> on_packet_;
			std::function<void()> handle_req_;
			std::function<bool(const std::string &)> check_transport_;
			std::function<bool(const std::string&)>	check_sid_;
			std::function<bool(const std::string &, std::string &)>	check_static_;
			std::function<std::vector<std::string>(const std::string&)>	get_upgrades_;
			std::function<std::size_t(uint32_t, std::function<bool()>&&)> set_timer_;
			std::function<void(std::size_t)> del_timer_;
		};

	}
}
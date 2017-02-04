#pragma once
namespace xsocket_io
{
	class session;
	namespace detail
	{
		class polling
		{
		public:
			polling(xnet::connection &&conn)
				:request_(std::move(conn))
			{
				init();
			}
			~polling()
			{

			}
			void close()
			{

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
				polling_ = false;
			}
		private:
			friend class session;

			void init()
			{
				request_.on_request_ = [this] {
					on_request();
				};
			}
			
			std::string gen_sid()
			{
				static std::atomic_int64_t uid = 0;
				auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
				auto id = uid.fetch_add(1) + now;
				auto sid = xutil::base64::encode(std::to_string(id));
				sid.pop_back();
				return sid;
			}
			bool check_sid(const std::string &sid)
			{
				assert(check_sid_);
				return check_sid_(sid);
			}

			void set_timer(const std::function<void()> &action)
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
				});
			}
			std::string build_resp(const std::string &resp, int status = 200)
			{
				xhttper::http_builder http_builder;

				http_builder.set_status(status);
				http_builder.append_entry("Content-Length", std::to_string(resp.size()));
				http_builder.append_entry("Content-Type", "text/plain; charset=utf-8");
				http_builder.append_entry("Connection", "keep-alive");
				http_builder.append_entry("Date", xutil::functional::get_rfc1123()());
				http_builder.append_entry("X-Powered-By", "xsocket.io");
				auto buffer = http_builder.build_resp();
				buffer.append(resp);
				return  buffer;
			}
			void on_polling_req()
			{
				using namespace detail;
				using strncasecmper = xutil::functional::strncasecmper;
				auto sid = request_.get_entry("sid");
				set_heartbeat_timeout();
				if (sid.empty())
				{
					open_msg msg;
					msg.pingInterval = ping_interval_;
					msg.pingTimeout = ping_timeout_;
					msg.sid = gen_sid();
					msg.upgrades = get_upgrades_(query_.get("transport"));
					xjson::obj_t obj;
					obj = msg;
					auto data = obj.str();
					auto buffer = std::to_string(data.size() + 1) + ":0" + data;
					request_.write(build_resp(buffer));
					return;
				}
				if (!check_sid(sid))
				{
					request_.write(build_resp(to_json(error_msg::e_session_id_unknown),400));
					return;
				}


			}
			void on_packet(const std::list<detail::packet> &_packet)
			{
				for (auto &itr : _packet)
					on_packet_(itr);
			}
			void on_request()
			{
				auto url = request_.url();
				std::cout << "URL: " << url << std::endl;;
				if (request_.method() == "GET")
				{
					if (url.find('?') == url.npos)
					{
						if (check_static(url))
							return;
					}
					if (request_.path() != "/socket.io/")
					{
						request_.write(build_resp("Cannot GET " + url, 404));
						return;
					}
					on_polling_req();
				}
				else if (request_.method() == "POST")
				{

				}
			}
			

			bool check_static(const std::string &url)
			{
				std::string filepath;
				if (!check_static_(url, filepath))
					return false;
				request_.send_file(filepath);
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

			request request_;
		};

	}
}
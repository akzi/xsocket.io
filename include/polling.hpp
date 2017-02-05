#pragma once
namespace xsocket_io
{
	class session;
	namespace detail
	{
		class polling
		{
		public:
			polling(std::shared_ptr<request> req)
				:request_(std::move(req))
			{
				init();
			}
			~polling()
			{

			}
			void close()
			{

			}
			
			void send(detail::packet_type _packet_type,
						detail::playload_type _playload_type, 
						std::string &&_play_load)
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
				auto buffer = detail::encode_packet(_packet);
				polling_ = false;
			}
		private:
			friend class session;

			
			void init()
			{
				request_->on_request_ = [this] {
					on_request();
				};
				request_->close_callback_ = [this] {
					close_callback_();
				};
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

			
			bool check_sid()
			{
				auto sid = request_->get_query().get("sid");
				if (sid_ != sid)
				{
					auto origin = request_->get_entry("Origin");
					request_->write(build_resp(detail::to_json(detail::error_msg::e_session_id_unknown), 400, origin));
					on_close();
					return false;
				}
				return true;
			}
			
			void on_polling_req()
			{
				origin_ = request_->get_entry("origin");
				if (!check_sid())
					return;
			}
			
			
			
			
			void on_request()
			{
				auto url = request_->url();
				auto method = request_->method();
				auto origin = request_->get_entry("Origin");
				if (method == "GET")
				{
					if (url.find('?') == url.npos)
					{
						if (check_static(url))
							return;
					}
					if (request_->path() != "/socket.io/")
					{
						request_->write(build_resp("Cannot GET " + url, 404, origin));
						return;
					}
					on_polling_req();
				}
				else if (method == "POST")
				{
					if (request_->path() == "/socket.io/")
					{ 
						if (!check_sid())
							return;
						auto body = request_->body();
						auto packets = detail::decode_packet(body, false);
						auto origin = request_->get_entry("Origin");
						for (auto itr: packets)
						{
							if (itr.packet_type_ == packet_type::e_close)
							{
								request_->write(build_resp("ok", 200, origin, false));
								on_close();
							}
							else if (itr.packet_type_ == packet_type::e_ping)
							{
							}
						}
						request_->write(build_resp("ok", 200, origin, false));
						
					}
				}
			}

			bool check_static(const std::string &url)
			{
				std::string filepath;
				if (!check_static_(url, filepath))
					return false;
				request_->send_file(filepath);
				return true;
			}

			bool check_transport(const std::string &transport)
			{
				assert(check_transport_);
				return check_transport_(transport);
			}
			
			void on_close()
			{
				close_callback_();
				throw std::logic_error("close_request");
			}

			int32_t ping_timeout_ = 12000;
			bool support_binary_ = false;
			bool add_user_ = false;
			bool polling_ = false;
			bool connect_ack_ = false;
			
			std::size_t	timer_id_ = 0;
			std::string sid_;
			std::string origin_;
			xhttper::query query_;
		
			std::list<packet> packet_buffers_;
			std::function<void()> on_heartbeat_;
			std::function<void()> close_callback_;
			std::function<void(const std::string &)> on_sid_;
			std::function<void(const std::list<detail::packet> &)> on_packet_;
			std::function<void()> handle_req_;
			std::function<bool(const std::string &)> check_transport_;
			std::function<bool(const std::string&)>	check_sid_;
			std::function<bool(const std::string &, std::string &)>	check_static_;
			std::function<std::vector<std::string>(const std::string&)>	get_upgrades_;
			std::function<std::size_t(uint32_t, std::function<bool()>&&)> set_timer_;
			std::function<void(std::size_t)> del_timer_;

			std::shared_ptr<request> request_;
		};

	}
}
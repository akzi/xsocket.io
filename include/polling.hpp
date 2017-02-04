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
				auto buffer = detail::encode_packet(_packet);
				polling_ = false;
			}
		private:
			friend class session;

			struct cookie_opt
			{
				bool httpOnly_ = true;
				bool secure_ = false;
				std::string path_ = "/";
				int64_t expires_ = 0;
				
				enum class sameSite
				{
					Null,
					Strict,
					Lax
				};
				sameSite sameSite_ = sameSite::Null;
			};
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

			std::string make_cookie(const std::string &key, const std::string &value, cookie_opt opt)
			{
				std::string buffer;
				buffer.append(key);
				buffer.append("=");
				buffer.append(value);
				if (opt.path_.size())
				{
					buffer.append("; path=");
					buffer.append(opt.path_);
				}
				if (opt.secure_)
				{
					buffer.append("; Secure");
				}
				if (opt.httpOnly_)
				{
					buffer.append("; HttpOnly");
				}
				if (opt.expires_ > 0)
				{
					buffer.append("; Expires=");
					buffer.append(std::to_string(opt.expires_));
				}
				buffer.pop_back();
				return buffer;
			}
			std::string build_resp(const std::string &resp, int status = 200, bool binary = false)
			{
				xhttper::http_builder http_builder;

				std::string content_type = "text/plain; charset=utf-8";
				if (binary)
					content_type = "application/octet-stream";
				http_builder.set_status(status);
				
				
				http_builder.append_entry("Content-Length", std::to_string(resp.size()));
				http_builder.append_entry("Content-Type", content_type);
				http_builder.append_entry("Connection", "keep-alive");
				http_builder.append_entry("Date", xutil::functional::get_rfc1123()());

				http_builder.append_entry("Set-Cookie", make_cookie("io", sid_, {}));
				
				if (request_.get_entry("Origin").size())
				{
					http_builder.append_entry("Access-Control-Allow-Credentials", "true");
					http_builder.append_entry("Access-Control-Allow-Origin",request_.get_entry("Origin"));
				}

				auto buffer = http_builder.build_resp();
				buffer.append(resp);
				return  buffer;
			}
			bool check_sid()
			{
				using namespace detail;
				auto sid = request_.get_query().get("sid");
				set_heartbeat_timeout();
				if (sid.empty())
				{
					sid_ = gen_sid();

					xjson::obj_t obj;
					packet _packet;
					open_msg msg;
					msg.pingInterval = ping_interval_;
					msg.pingTimeout = ping_timeout_;
					msg.upgrades = get_upgrades_(query_.get("transport"));
					msg.sid = sid_;

					obj = msg;
					_packet.binary_ = !!!request_.get_entry("b64").size();
					_packet.packet_type_ = packet_type::e_open;
					_packet.is_string_ = true;
					_packet.playload_type_ = playload_type::e_null1;
					_packet.playload_ = obj.str();
					request_.write(build_resp(encode_packet(_packet), 200, _packet.binary_));
					return false;
				}
				if (sid == sid_)
					return true;

				if (!check_sid_(sid))
				{
					request_.write(build_resp(to_json(error_msg::e_session_id_unknown), 400));
					return false;
				}
				return true;
			}
			bool check_connect_ack()
			{
				if (!connect_ack_)
				{
					packet _packet;
					_packet.packet_type_ = packet_type::e_message;
					_packet.playload_type_ = playload_type::e_connect;
					_packet.binary_ = !!!request_.get_entry("b64").size();
					_packet.is_string_ = true;
					connect_ack_ = true;
					request_.write(build_resp(encode_packet(_packet), 200, _packet.binary_));
					false;
				}
				return true;
			}
			void on_polling_req()
			{
				std::cout << "get polling:" << this << std::endl;
				if (!check_sid())
					return;
				if (!check_connect_ack())
					return;
			}
			void pong()
			{
				packet _packet;
				_packet.packet_type_ = packet_type::e_pong;
				_packet.binary_ = !!!request_.get_entry("b64").size();
				_packet.is_string_ = true;
				request_.write(build_resp(encode_packet(_packet), 200, _packet.binary_));
			}
			void resp_login()
			{
				packet _packet;
				_packet.is_string_ = true;
				_packet.binary_ = !!!request_.get_entry("b64").size();
				xjson::obj_t obj;
				obj.add("login");
				xjson::obj_t members;
				members["members"] = 2;
				obj.add(members);
				_packet.playload_ = obj.str();
				_packet.packet_type_ = packet_type::e_message;
				_packet.playload_type_ = playload_type::e_event;
				request_.write(build_resp(encode_packet(_packet), 200, _packet.binary_));
			}
			void check_add_user(const packet &_packet)
			{
				if (!add_user_ && _packet.playload_.size())
				{
					auto obj = xjson::build(_packet.playload_);
					if (obj.get<std::string>(0) == "add user")
					{
						resp_login();
					}
					add_user_ = true;
				}
			}
			void on_packet(const std::list<detail::packet> &_packet)
			{
				std::cout << "post  polling:"<< this << std::endl;

				for (auto &itr : _packet)
				{
					if (itr.packet_type_ == packet_type::e_ping)
					{
						pong();
						request_.write(build_resp("ok"));
						continue;
					}
					else if (itr.packet_type_ == packet_type::e_message)
					{
						on_packet_(itr);
						check_add_user(itr);
						request_.write(build_resp("ok"));
					}
					
				}
					
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
					on_packet(decode_packet(request_.body(), false));
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
			bool add_user_ = false;
			bool polling_ = false;
			bool connect_ack_ = false;
			uint32_t ping_interval_ = 25000;
			uint32_t ping_timeout_ = 60000;
			std::size_t	timer_id_ = 0;
			std::string sid_;

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
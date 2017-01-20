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
			void send_file(const std::string &filepath)
			{
				if (!xutil::vfs::file_exists()(filepath))
					send_data("Cannot get file", 400, false);
				if (check_cache(filepath))
					return;
				do_send_file(filepath);
			}
			void send(detail::packet_type _packet_type, detail::playload_type _playload_type, std::string &&_play_load)
			{
				detail::packet _packet;
				_packet.binary_ = _packet_type == detail::e_binary_event;
				_packet.packet_type_ = _packet_type;
				_packet.playload_ = std::move(_play_load);
				_packet.packet_type_ = _packet_type;
				if (is_sending_ || !polling_ || packet_buffers_.size())
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
			friend class request;
			bool check_cache(const std::string &filepath)
			{
				using get_rfc1123 = xutil::functional::get_rfc1123;
				using last_modified = xutil::vfs::last_modified;
				using strcasecmper = xutil::functional::strcasecmper;
				using strncasecmper = xutil::functional::strncasecmper;

				auto cache_control = http_parser_.get_header<strncasecmper>("Cache-Control");
				if (cache_control.size() && strcasecmper()("no-cache", cache_control.c_str()))
					return false;
				auto pragma = http_parser_.get_header<strncasecmper>("Pragma");
				if (pragma.size() && strcasecmper()("no-cache", pragma.c_str()))
					return false;

				auto if_modified_since = http_parser_.get_header<strncasecmper>("If-Modified-Since");
				auto if_none_match = http_parser_.get_header<strncasecmper>("If-None-Match");
				if (if_modified_since.empty() && if_none_match.empty())
					return false;
				if (if_modified_since.size())
				{
					auto last_modified_ = get_rfc1123()(last_modified()(filepath));
					if (last_modified_ != if_modified_since)
						return false;
				}
				if (if_none_match.size())
				{
					auto ssbuf = std::ostringstream();
					auto etag = last_modified()(filepath) +
						xutil::vfs::file_size()(filepath);
					ssbuf << std::hex << etag;
					if (if_none_match != ssbuf.str())
						return false;
				}

				http_builder_.set_status(304);
				http_builder_.append_entry("Accept-range", "bytes");
				http_builder_.append_entry("Date", get_rfc1123()());
				http_builder_.append_entry("Connection", "keep-alive");
				if (is_sending_)
					send_buffers_.emplace_back(http_builder_.build_resp());
				else
				{
					conn_.async_send(http_builder_.build_resp());
					is_sending_ = true;
				}
				return true;
			}
			std::pair<uint64_t, uint64_t> get_range()
			{
				static std::pair<uint64_t, uint64_t>  noexist = { UINT64_MAX, UINT64_MAX };
				using strncasecmper = xutil::functional::strncasecmper;
				std::string range = http_parser_.get_header<strncasecmper>("Range");
				if (range.empty())
					return noexist;
				auto pos = range.find("=");
				if (pos == range.npos)
					return noexist;
				++pos;
				auto end = pos;
				auto begin = std::stoull(range.c_str() + pos, &end, 10);
				if (end == pos)
					begin = UINT64_MAX;
				pos = range.find('-');
				if (pos == range.npos)
					return noexist;
				++pos;
				if (pos == range.size())
					return{ begin, UINT64_MAX };

				return{ begin, std::stoull(range.c_str() + pos, 0, 10) };
			}
			void do_send_file(const std::string &filepath)
			{
				using get_extension = xutil::functional::get_extension;
				using get_filename = xutil::functional::get_filename;
				using get_rfc1123 = xutil::functional::get_rfc1123;
				using last_modified = xutil::vfs::last_modified;
				auto range = get_range();
				int64_t begin = 0;
				int64_t end = 0;
				std::ios_base::openmode  mode;

				mode = std::ios::binary | std::ios::in;
				std::fstream file;
				file.open(filepath.c_str(), mode);
				if (!file.good())
					return;

				file.seekg(0, std::ios::end);
				auto size = file.tellg();
				end = size;

				bool has_range = false;
				if (range.first != UINT64_MAX)
				{
					begin = range.first;
					has_range = true;
				}
				if (range.second != UINT64_MAX)
				{
					has_range = true;
					if (range.first == UINT64_MAX)
					{
						end = size;
						begin = (int64_t)size - range.second;
					}
					else
					{
						end = range.second;
					}
				}

				http_builder_.append_entry("Date", get_rfc1123()());
				http_builder_.append_entry("Connection", "keep-alive");
				http_builder_.append_entry("Content-Type", http_builder_.get_content_type(get_extension()(filepath)));
				http_builder_.append_entry("Content-Length", std::to_string(end - begin).c_str());
				http_builder_.append_entry("Content-Disposition", "attachment; filename=" + get_filename()(filepath));

				auto ssbuf = std::ostringstream();
				auto lm = last_modified()(filepath) + size;
				ssbuf << std::hex << lm;
				http_builder_.append_entry("Etag", ssbuf.str());
				if (has_range)
				{
					std::string content_range("bytes ");
					content_range.append(std::to_string(begin));
					content_range.append("-");
					content_range.append(std::to_string(end));
					content_range.append("/");
					content_range.append(std::to_string(size));

					http_builder_.set_status(206);
					http_builder_.append_entry("Accept-Range", "bytes");
					http_builder_.append_entry("Content-Range", content_range);
				}

				std::string buffer;
				buffer.resize(102400);
				auto coro_func = [&, this] {
					std::function<void()> resume_handle;
					file.seekg(begin, std::ios::beg);
					conn_.regist_send_callback([&](std::size_t len) {
						if (len == 0)
						{
							close();
							file.close();
							resume_handle();
							return;
						}
						if (begin == end)
						{
							file.close();
							resume_handle();
							return;
						}
						auto to_reads = std::min<uint64_t>(buffer.size(), end - begin);
						file.read((char*)buffer.data(), to_reads);
						auto gcount = file.gcount();
						if (gcount > 0)
						{
							conn_.async_send(buffer.data(), (uint32_t)gcount);
							begin += gcount;
						}
					});
					conn_.async_send(std::move(http_builder_.build_resp()));
					xcoroutine::yield(resume_handle);
					regist_send_callback();
					regist_recv_callback();
				};
				xcoroutine::create(coro_func);
			}
			void regist_recv_callback()
			{
				conn_.regist_recv_callback([this](char *data, std::size_t len) {
					if (!len)
						return on_close();
					recv_callback(data, len);
					if (is_close_)
						return on_close();
				});
			}
			void regist_send_callback()
			{
				conn_.regist_send_callback([this](std::size_t len) {
					if (!len)
						return on_close();
					is_sending_ = false;
					flush();
				});
			}

			void init()
			{
				regist_recv_callback();
				regist_send_callback();
				conn_.async_recv_some();
			}

			void send_data(const std::string &msg, int status, bool binary = true)
			{
				auto content_type = support_binary_ ?
					"text/html; charset=utf-8" : "application/octet-stream";
				if (binary == false)
					content_type = "text/html; charset=utf-8";
				http_builder_.set_status(status);
				http_builder_.append_entry("Content-Length", std::to_string(msg.size()));
				http_builder_.append_entry("Content-Type", content_type);
				http_builder_.append_entry("Connection", "keep-alive");
				http_builder_.append_entry("Date", xutil::functional::get_rfc1123()());
				http_builder_.append_entry("X-Powered-By", "xsocket.io");
				auto buffer = http_builder_.build_resp();
				buffer.append(msg);
				if (is_sending_ || !polling_)
				{
					send_buffers_.emplace_back(std::move(buffer));
					return;
				}
				conn_.async_send(std::move(buffer));
				is_sending_ = true;
				polling_ = false;
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
			void flush()
			{
				if (is_sending_ || !polling_)
					return;

				if (send_buffers_.size())
				{
					conn_.async_send(std::move(send_buffers_.front()));
					send_buffers_.pop_front();
					is_sending_ = true;
					return;
				}
				if (packet_buffers_.empty())
					return;
				std::string buffer;
				for (auto &itr : packet_buffers_)
				{
					buffer += detail::packet_msg(itr);
				}
				send_data(buffer, 200);
				packet_buffers_.clear();
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
			void on_data()
			{
				using cmp = xutil::functional::strncasecmper;
				auto content_length_str = http_parser_.get_header<cmp>("Content_Length");
				auto content_length = std::strtoul(content_length_str.c_str(), 0, 10);

				if (content_length_str.empty() || !content_length)
				{
					detail::error_msg msg;
					msg = detail::error_msg::e_bad_request;
					return send_data(detail::to_json(msg), 400, false);
				}
				auto body = http_parser_.get_string();
				if (body.size() != content_length)
				{
					std::function<void()> resume;
					auto coro_func = [&, this] {
						conn_.regist_recv_callback([&, this](char *data, std::size_t len) {
							if (!len)
							{
								is_close_ = true;
								resume();
								return;
							}
							body.append(data, len);
							resume;
						});
						conn_.async_recv(content_length - body.size());
						xcoroutine::yield(resume);
						regist_recv_callback();
					};
					xcoroutine::create(coro_func);
					if (is_close_)
						return on_close();
				}
				on_packet(detail::unpacket(body, support_binary_));
			}
			void recv_callback(char *data, std::size_t len)
			{
				http_parser_.append(data, len);
				if (!http_parser_.parse_req())
					return;

				polling_ = true;
				auto url = http_parser_.url();
				auto method = http_parser_.get_method();

				if (check_static(url))
					return;

				auto pos = url.find('?');
				if (pos == url.npos)
				{
					send_data(detail::to_json(detail::error_msg::e_bad_request), 400, false);
					return;
				}


				auto path = url.substr(0, pos++);
				if (path != "/socket.io/")
				{
					if (!handle_req_)
					{
						auto resp = "Cannot " + method + " " + url;
						return send_data(resp, 400);
					}
					handle_req_();
					if (is_close_)
						return on_close();
				}
				query_ = xhttper::query(url.substr(pos, url.size() - pos));
				auto transport = query_.get("transport");
				if (transport.empty() || !check_transport(transport))
				{
					send_data(detail::to_json(detail::error_msg::e_transport_unknown), 400, false);
					return;
				}
				support_binary_ = !!query_.get("b64").size();
				if (method == "GET")
					on_polling_req();
				else if (method == "POST")
					on_data();
			}

			bool check_static(const std::string &url)
			{
				std::string filepath;
				if (!check_static_(url, filepath))
					return false;
				send_file(filepath);
				return true;
			}

			bool check_transport(const std::string &transport)
			{
				assert(check_transport_);
				return check_transport_(transport);
			}
			void on_close()
			{
				if (timer_id_)
					del_timer_(timer_id_);
				timer_id_ = 0;
			}

			bool support_binary_ = false;
			bool is_close_ = false;
			bool is_sending_ = false;
			bool polling_ = false;
			uint32_t ping_interval_ = 25000;
			uint32_t ping_timeout_ = 60000;
			std::size_t	timer_id_ = 0;

			xhttper::query query_;
			std::list<detail::packet> packet_buffers_;
			std::list<std::string> send_buffers_;

			std::function<void()> on_heartbeat_;
			std::function<void(detail::packet)> on_packet_;
			std::function<void()> handle_req_;
			std::function<bool(const std::string &)> check_transport_;
			std::function<bool(const std::string&)>	check_sid_;
			std::function<bool(const std::string &, std::string &)>	check_static_;
			std::function<std::vector<std::string>(const std::string&)>	get_upgrades_;
			std::function<std::size_t(uint32_t, std::function<bool()>&&)> set_timer_;
			std::function<void(std::size_t)> del_timer_;

			xhttper::http_parser http_parser_;
			xhttper::http_builder http_builder_;
			xnet::connection conn_;
		};

	}
}
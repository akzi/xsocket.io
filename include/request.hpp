#pragma once
namespace xsocket_io
{
	class request
	{
	public:
		request(xnet::connection &&conn)
			:conn_(std::move(conn))
		{

		}
		~request()
		{

		}
		void close()
		{

		}
		void send_file(const std::string &filepath)
		{
			std::ifstream file(filepath, std::ios::binary);
			if (!file.good())
				response("Cannot get file");
		}
		void send(std::string &&msg, detail::packet_type type = detail::e_message)
		{
			if (is_sending_ || !polling_ || packet_buffers_.size())
			{
				packet_buffers_.emplace_back(std::move(msg), type);
				return;
			}
			auto buffer = detail::packet_msg(msg, type, !support_binary_);
			response(buffer, 200);
			is_sending_ = true;
			polling_ = false;
		}
	private:
		void init()
		{
			conn_.regist_recv_callback([this](char *data, std::size_t len) {
				if (!len)
					return on_close();
				on_data(data, len);
				if (is_close_)
					return on_close();
			});
			conn_.regist_send_callback([this](std::size_t len){
				if (!len)
					return on_close();
				is_sending_ = false;
			});
		}

		void response(const std::string &msg, int status = 400)
		{
			auto content_type = support_binary_ ? 
				"text/html; charset=utf-8" : "application/octet-stream";
			http_builder_.set_status(status);
			http_builder_.append_entry("Content-Length", std::to_string(msg.size()));
			http_builder_.append_entry("Content-Type", content_type);
			http_builder_.append_entry("Connection", "keep-alive");
			http_builder_.append_entry("Date", xutil::functional::get_rfc1123()());
			http_builder_.append_entry("X-Powered-By", "xsocket.io");
			auto buffer = http_builder_.build_resp();
			buffer.append(msg);
			conn_.async_send(std::move(buffer));
			is_sending_ = true;
		}
		std::string gen_sid()
		{
			static std::atomic_int64_t uid = 0;
			auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			auto id = uid.fetch_add(1) + now;
			return xutil::base64::encode(std::to_string(id));
		}
		void send_msg(const std::string &msg, detail::packet_type type)
		{
			response(packet_msg(msg, type, !support_binary_),200);
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

			timer_id_ = set_timer_(ping_timeout_, [this,action] {
				action();
				timer_id_ = 0;
				return false;
			});
		}
		void flush()
		{
			if (packet_buffers_.empty() || is_sending_)
				return ;
			std::string buffer;
			for (auto &itr :packet_buffers_)
			{
				buffer += packet_msg(itr.first, itr.second, !support_binary_);
			}

			packet_buffers_.clear();
		}
		void on_polling_req()
		{
			using namespace detail;
			using strncasecmper = xutil::functional::strncasecmper;
			auto sid = http_parser_.get_header<strncasecmper>("sid");
			if (sid.empty())
			{
				open_msg msg;
				msg.pingInterval = ping_interval_;
				msg.pingTimeout = ping_timeout_;
				msg.sid = gen_sid();
				msg.upgrades = get_upgrades_(query_.get("transport"));
				xjson::obj_t obj;
				msg.xpack(obj);
				send_msg(obj.str(), detail::e_open);
				return;
			}
			if (!check_sid(sid))
			{
				response(to_json(error_msg::e_session_id_unknown));
				return;
			}
			flush();
			set_timer([this] {
				send_msg("", detail::e_close);
				on_close();
			});
		}

		void on_data(char *data, std::size_t len)
		{
			http_parser_.append(data, len);
			if (http_parser_.parse_req())
			{
				polling_ = true;
				auto url = http_parser_.url();
				auto pos = url.find('?');
				if (pos == url.npos)
					return response("{\"code\":0,\"message\":\"Transport unknown\"}");

				auto path = url.substr(0, pos++);
				if (path != "/socket.io/")
				{
					if (check_static(url))
						return;
					if (!handle_req_)
					{
						auto method = http_parser_.get_method();
						auto resp = "Cannot " + method + " " + url;
						return response(resp);
					}
					handle_req_(*this);
					if (is_close_)
						return on_close();
				}
				query_ = xhttper::query(url.substr(pos, url.size() - pos));
				auto transport = query_.get("transport");
				if (transport.empty() || !check_transport(transport))
					return response("{\"code\":0,\"message\":\"Transport unknown\"}");
				support_binary_ = !!query_.get("b64").size();
				on_polling_req();
			}
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

		bool															support_binary_ = false;
		bool															is_close_ = false;
		bool															is_sending_ = false;
		bool															polling_ = false;
		int64_t															ping_interval_ = 25000;
		int64_t															ping_timeout_ = 60000;
		int64_t															timer_id_ = 0;
		
		xhttper::query													query_;
		std::list<std::pair<std::string,detail::packet_type>>			packet_buffers_;

		std::function<void(request &)>									handle_req_;
		std::function<bool(const std::string &)>						check_transport_;
		std::function<bool(const std::string&)>							check_sid_;
		std::function<bool(const std::string &, std::string &)>			check_static_;
		std::function<std::vector<std::string>(const std::string&)>		get_upgrades_;
		std::function<std::size_t(uint32_t, std::function<bool()>&&)>	set_timer_;
		std::function<void(std::size_t)>								del_timer_;

		xhttper::http_parser											http_parser_;
		xhttper::http_builder											http_builder_;
		xnet::connection												conn_;
	};
}
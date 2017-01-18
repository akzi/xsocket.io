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
	private:
		void init()
		{
			conn_.regist_recv_callback([this](char *data, std::size_t len) {
				if (!len)
					return on_close();
				return on_data(data, len);
			});
		}
		void send_msg(std::string &&msg)
		{
			if (is_send_)
			{
				send_buffers_.emplace_back(std::move(msg));
				return;
			}
			conn_.async_send(std::move(msg));
			is_send_ = true;
		}
		void response_404(const std::string &msg)
		{
			http_builder_.set_status(404);
			http_builder_.append_entry("Content-Length", std::to_string(msg.size()));
			http_builder_.append_entry("Content-Type", "text/html; charset=utf-8");
			http_builder_.append_entry("Connection", "keep-alive");
			http_builder_.append_entry("Date", xutil::functional::get_rfc1123()());
			http_builder_.append_entry("X-Powered-By", "xsocket.io");
			auto buffer = http_builder_.build_resp();
			buffer.append(msg);
			send_msg(std::move(buffer));
		}
		void on_data(char *data, std::size_t len)
		{
			http_parser_.append(data, len);
			if (http_parser_.parse_req())
			{
				auto url = http_parser_.url();
				auto pos = url.find('?');
				if (pos == url.npos)
					return response_404("{\"code\":0,\"message\":\"Transport unknown\"}");

				auto path = url.substr(0, pos++);
				if (path != "/socket.io/")
				{
					if (check_static(url))
						return;
					if (!handle_req_)
					{
						auto method = http_parser_.get_method();
						auto resp = "Cannot " + method + " " + url;
						return response_404(resp);
					}
					handle_req_(*this);
					if (is_close_)
						return on_close();
				}
				query_ = xhttper::query(url.substr(pos, url.size() - pos));
				auto transport = query_.get("transport");
				if (transport.empty() || !check_transport(transport))
					return response_404("{\"code\":0,\"message\":\"Transport unknown\"}");
				b64_ = !!query_.get("b64").size();
			}
		}
		bool check_static(const std::string &url)
		{
			if (url == "/");
		}
		bool check_transport(const std::string &transport)
		{
			assert(check_transport_);
			return check_transport_(transport);
		}
		void on_close()
		{

		}
		bool b64_;
		bool is_close_ = false;
		bool is_send_ = false;
		xhttper::query query_;
		std::list<std::string> send_buffers_;
		std::function<void(request &)> handle_req_;
		std::function<bool(const std::string &)> check_transport_;
		xhttper::http_parser http_parser_;
		xhttper::http_builder http_builder_;
		xnet::connection conn_;
	};
}
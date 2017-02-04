#pragma once
namespace xsocket_io
{
	namespace detail
	{
		class polling;
	}

	class request
	{
	public:
		request(xnet::connection &&conn)
			:conn_(std::move(conn))
		{
			init();
		}
		~request()
		{

		}
		std::string url()
		{
			return url_;
		}
		std::string path()
		{
			return path_;
		}
		xhttper::query get_query()
		{
			return query_;
		}
		std::string get_entry(const char *name)
		{
			using cmp = xutil::functional::strncasecmper;
			return http_parser_.get_header<cmp>(name);
		}
		std::string get_entry(const std::string &name)
		{
			using cmp = xutil::functional::strncasecmper;
			return http_parser_.get_header<cmp>(name.c_str());
		}
		std::string method()
		{
			return http_parser_.get_method();
		}
		
		void send_file(const std::string &filepath)
		{
			if (!xutil::vfs::file_exists()(filepath))
				resp("Cannot get file", 400, false);
			if (check_cache(filepath))
				return;
			do_send_file(filepath);
		}
		void write(std::string &&buffer)
		{
			send_buffers_.push_back(std::move(buffer));
			flush();
		}
		
	private:
		friend class detail::polling;
		void do_send_file(const std::string &filepath)
		{
			std::cout << filepath << std::endl;

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
			xhttper::http_builder http_builder;

			http_builder.append_entry("Date", get_rfc1123()());
			http_builder.append_entry("Connection", "keep-alive");
			http_builder.append_entry("Content-Type", http_builder.get_content_type(get_extension()(filepath)));
			http_builder.append_entry("Content-Length", std::to_string(end - begin).c_str());

			auto ssbuf = std::ostringstream();
			auto lm = last_modified()(filepath) + size;
			ssbuf << std::hex << lm;
			http_builder.append_entry("Etag", ssbuf.str());
			if (has_range)
			{
				std::string content_range("bytes ");
				content_range.append(std::to_string(begin));
				content_range.append("-");
				content_range.append(std::to_string(end));
				content_range.append("/");
				content_range.append(std::to_string(size));

				http_builder.set_status(206);
				http_builder.append_entry("Accept-Range", "bytes");
				http_builder.append_entry("Content-Range", content_range);
			}

			std::string buffer;
			buffer.resize(102400);
			std::function<void()> resume_handle;
			file.seekg(begin, std::ios::beg);
			conn_.regist_send_callback([&](std::size_t len) {

				if (len == 0)
				{
					is_close_ = true;
					file.close();
					if(resume_handle)
						resume_handle();
					return;
				}
				if (begin == end)
				{
					file.close();
					if (resume_handle)
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
			conn_.async_send(std::move(http_builder.build_resp()));
			xcoroutine::yield(resume_handle);
			init_send_callback();
			if (is_close_)
				return on_close();
		}
		void resp(const std::string &msg, int status, bool binary = true)
		{
			xhttper::http_builder http_builder;
			http_builder.set_status(status);
			http_builder.append_entry("Content-Length", std::to_string(msg.size()));
			http_builder.append_entry("Content-Type", "text/html; charset=utf-8");
			http_builder.append_entry("Connection", "keep-alive");
			http_builder.append_entry("Date", xutil::functional::get_rfc1123()());
			http_builder.append_entry("X-Powered-By", "xsocket.io");
			auto buffer = http_builder.build_resp();
			buffer.append(msg);
			if (is_sending_)
			{
				send_buffers_.emplace_back(std::move(buffer));
				return;
			}
			conn_.async_send(std::move(buffer));
			is_sending_ = true;
		}
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
			xhttper::http_builder http_builder;
			http_builder.set_status(304);
			http_builder.append_entry("Accept-range", "bytes");
			http_builder.append_entry("Date", get_rfc1123()());
			http_builder.append_entry("Connection", "keep-alive");
			if (is_sending_)
				send_buffers_.emplace_back(http_builder.build_resp());
			else
			{
				conn_.async_send(http_builder.build_resp());
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
		void recv_callback(char *data, std::size_t len)
		{
			http_parser_.append(data, len);
			if (!http_parser_.parse_req())
				return;
			
			url_ = http_parser_.url();
			auto pos = url_.find('?');
			if (pos == url_.npos)
			{
				path_ = url_;
				query_ = std::move(xhttper::query());
				return on_request();
			}
			path_ = url_.substr(0, pos);
			pos++;
			auto args = url_.substr(pos, url_.size() - pos);
			query_ = std::move(xhttper::query(args));
			on_request();
		}
		void on_request()
		{
			xcoroutine::create([this] {
				on_request_();
				http_parser_.reset();
				if(!is_close_)
					conn_.async_recv_some();
			});
			
		}
		void flush()
		{
			if (is_sending_)
				return;

			if (send_buffers_.size())
			{
				conn_.async_send(std::move(send_buffers_.front()));
				send_buffers_.pop_front();
				is_sending_ = true;
				return;
			}
		}
		void init_send_callback()
		{
			conn_.regist_send_callback([this](std::size_t len) {
				if (!len)
					return on_close();
				is_sending_ = false;
				flush();
			});
		}
		void on_close()
		{

		}

		void init()
		{
			regist_recv_callback();
			init_send_callback();
			conn_.async_recv_some();
		}
		bool is_close_ = false;
		bool is_sending_ = false;
		std::list<std::string> send_buffers_;
		xhttper::http_parser http_parser_;
		xnet::connection conn_;
		std::string url_;
		std::string path_;
		xhttper::query query_;

		std::function<void()> on_request_;
	};
}
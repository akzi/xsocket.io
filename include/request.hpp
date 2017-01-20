#pragma once
namespace xsocket_io
{
	class request
	{
	public:
		request(detail::polling &_polling)
			:polling_(_polling)
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
			return polling_.http_parser_.get_header<cmp>(name);
		}
		std::string get_entry(const std::string &name)
		{
			using cmp = xutil::functional::strncasecmper;
			return polling_.http_parser_.get_header<cmp>(name.c_str());
		}
		std::string method()
		{
			return polling_.http_parser_.get_method();
		}
	private:
		void init()
		{
			url_ = polling_.http_parser_.url();
			auto pos = url_.find('?');
			if (pos == url_.npos)
			{
				path_ = url_;
				return;
			}
			path_ = url_.substr(0, pos);
			pos++;
			auto args = url_.substr(pos, url_.size() - pos);
			query_ = std::move(xhttper::query(args));
		}
		detail::polling &polling_;
		std::string url_;
		std::string path_;
		xhttper::query query_;
	};
}
#pragma once
namespace xsocket_io
{
	class xserver
	{
	public:
		using handle_request_t = std::function<void(request, response)> ;
		using handle_session_close_t = std::function<void(session)>;
		xserver()
			:proactor_pool_(1)
		{
			init();
		}

		void on_connection(const std::function<void(session&)> &handle)
		{
			connection_callback_ = handle;
		}
		void on_close(const handle_session_close_t &handle)
		{
			handle_session_close_ = handle;
		}

		void on_request(const handle_request_t &handle)
		{
			handle_request_ = handle;
		}
		void bind(const std::string &ip, int port)
		{
			proactor_pool_.bind(ip, port);
		}
		void start()
		{
			proactor_pool_.start();
		}
		void set_static(const std::string &path)
		{
			static_path_ = path;
		}
	private:
		void init()
		{
			proactor_pool_.regist_accept_callback([this](xnet::connection &&conn) {

				std::unique_ptr<session> sess(new session(proactor_pool_, std::move(conn)));
				sess->regist_session_ = [this](auto &&...args) { return regist_session(std::forward<decltype(args)>(args)...); };
				sess->check_static_ = [this](auto &&...args) { return check_static(std::forward<decltype(args)>(args)...); };
				sess->get_upgrades_ = [this](auto &&...args) { return get_upgrades(std::forward<decltype(args)>(args)...); };
				sess->check_sid_ = [this](auto &&...args) { return check_sid(std::forward<decltype(args)>(args)...); };
				std::unique_lock<std::mutex> lock_g(session_mutex_);
				session_cache_.emplace_back(std::move(sess));
			});
		}
		void regist_session(const std::string &sid, session *sess)
		{
			std::unique_lock<std::mutex> lock_g(session_mutex_);
			for (auto itr = session_cache_.begin(); itr != session_cache_.end(); ++itr)
			{
				if (itr->get() == sess)
				{
					sessions_.emplace(sid, std::move(*itr));
					session_cache_.erase(itr);
					lock_g.unlock();
					assert(connection_callback_);	
					connection_callback_(*sess);
					return;
				}
			}
		}
		bool check_static(const std::string& filename, std::string &filepath)
		{
			filepath = xutil::vfs::getcwd()() + static_path_ + filename;
			if (filename == "/")
				filepath = xutil::vfs::getcwd()() + static_path_ +  "index.html";
			if (xutil::vfs::file_exists()(filepath))
				return true;
			filepath.clear();
			return false;
		}

		bool check_sid(const std::string &sid)
		{
			std::unique_lock<std::mutex> lock_g(session_mutex_);
			return sessions_.find(sid) != sessions_.end();
		}
		std::vector<std::string> get_upgrades(const std::string &transport)
		{
			return{};
		}
		std::mutex session_mutex_;
		std::map<std::string, std::unique_ptr<session>> sessions_;
		std::list<std::unique_ptr<session>> session_cache_;
		std::function<void(session &)> connection_callback_;
		xnet::proactor_pool proactor_pool_;

		handle_request_t handle_request_;
		handle_session_close_t handle_session_close_;
		std::string static_path_;
	};
}
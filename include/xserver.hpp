#pragma once
namespace xsocket_io
{
	class xserver
	{
	public:
		xserver()
		{

		}

		void on_connection(const std::function<void(session&)> &handle)
		{
			connection_callback_ = handle;
		}
		void on_close()
		{

		}

		void on_request()
		{

		}
		void bind(const std::string &ip, int port)
		{
			proactor_pool_.bind(ip, port);
		}
	private:
		void init()
		{
			proactor_pool_.regist_accept_callback([this](xnet::connection &&conn) {

				std::unique_ptr<session> sess(new session(proactor_pool_, std::move(conn)));
				sess->regist_session_ = [this](auto &&...args) {
					return regist_session(std::forward<decltype(args)>(args)...); 
				};
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
		std::mutex session_mutex_;
		std::map<std::string, std::unique_ptr<session>> sessions_;
		std::list<std::unique_ptr<session>> session_cache_;
		std::function<void(session &)> connection_callback_;
		xnet::proactor_pool proactor_pool_;
	};
}
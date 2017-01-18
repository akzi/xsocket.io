#pragma once
namespace xsocket_io
{
	class session
	{
	public:
		session()
		{

		}
		session(session &&sess)
		{
			reset_move(std::move(sess));
		}
		session &operator = (session &&sess)
		{
			reset_move(std::move(sess));
			return *this;
		}
		template<typename ...Args>
		void on(const std::string &event_name, std::function<void(Args &&)> &&handle)
		{
			regist_event(event_name, std::move(handle));
		}

		template<typename T>
		void emit(const std::string &event_name,const T &msg)
		{
			std::string data = detail::packet_msg(event_name, msg);
			ws_sess_.send_text(data);
		}
	private:
		friend class xserver;
		void reset_move(session &sess)
		{
			if (&sess == this)
				return;
			close_callbacks_ = std::move(sess.close_callbacks_);
			event_handles_ = std::move(sess.event_handles_);
			msg_ = std::move(sess.msg_);
			session_id_ = sess.session_id_;
			regist_session_ = std::move(sess.regist_session_);
			sess.session_id_ = 0;
			
		}
		void init()
		{
			regist_session_(*this);
		}
		void frame_callback(const std::string& data)
		{

		}
		void regist_event(const std::string &event_name, std::function<void(std::string &&)> &&handle_)
		{
			std::function<void(std::string &&)> handle;
			std::function<void()> func = [handle = std::move(handle_),this]{
				handle(std::move(msg_));
				msg_.clear();
			};
			if (event_handles_.find(event_name) != event_handles_.end())
				throw std::runtime_error("event");
			event_handles_.emplace(event_name, std::move(func));
		}
		void regist_event(const std::string &event_name, std::function<void()> &&handle)
		{
			if (event_handles_.find(event_name) != event_handles_.end())
				throw std::runtime_error("event");
			event_handles_.emplace(event_name, std::move(handle));
		}



		std::vector<std::function<void()>> close_callbacks_;
		std::map<std::string, std::function<void()>> event_handles_;
		std::function<void(session &)> regist_session_;
		xnet::connection conn_;
		std::string msg_;
		int64_t session_id_ = 0;
	};
}
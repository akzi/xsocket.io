#pragma once
namespace xsocket_io
{
	class session
	{
	public:
		session(xnet::proactor_pool &ppool, xnet::connection &&conn)
			:pro_pool_(ppool),
			polling_(std::move(conn))
		{
			init();
		}
		template<typename ...Args>
		void on(const std::string &event_name, std::function<void(Args &&...)> &&handle)
		{
			regist_event(event_name, std::move(handle));
		}

		template<typename T>
		void emit(const std::string &event_name,const T &msg)
		{

		}
		std::string get_sid()
		{
			return sid_;
		}
	private:
		session(session &&sess) = delete;
		session(session &sess) = delete;
		session &operator = (session &&sess) = delete;
		session &operator = (session &sess) = delete;

		friend class xserver;

		void on_heartbeat()
		{

		}
		void on_packet(const detail::packet &_packet)
		{

		}
		void handle_req()
		{
			
		}
		bool check_transport(const std::string &_transport)
		{
			return check_transport_(_transport);
		}
		bool check_sid(const std::string &sid)
		{
			return check_sid_(sid);
		}
		bool check_static(const std::string &url, std::string &filepath)
		{
			return check_static_(url, filepath);
		}
		std::size_t set_timer(uint32_t timeout, std::function<bool()>&& actions)
		{
			return pro_pool_.set_timer(timeout, std::move(actions));
		}
		void del_timer(std::size_t timer_id)
		{
			pro_pool_.cancel_timer(timer_id);
		}
		std::vector<std::string> get_upgrades(const std::string &transport)
		{
			return get_upgrades_(transport);
		}
		void init_polling()
		{
			polling_.on_heartbeat_ = [this](auto &&...args) {return on_heartbeat(std::forward<decltype(args)>(args)...); };
			polling_.on_packet_ = [this](auto &&...args) {return on_packet(std::forward<decltype(args)>(args)...); };
			polling_.check_sid_ = [this](auto &&...args) {return check_sid(std::forward<decltype(args)>(args)...); };
			polling_.check_transport_ = [this](auto &&...args) {return check_transport(std::forward<decltype(args)>(args)...); };
			polling_.check_static_ = [this](auto &&...args) {return check_static(std::forward<decltype(args)>(args)...); };
			polling_.get_upgrades_ = [this](auto &&...args) {return get_upgrades(std::forward<decltype(args)>(args)...); };
			polling_.set_timer_ = [this](auto &&...args) {return set_timer(std::forward<decltype(args)>(args)...); };
			polling_.del_timer_ = [this](auto &&...args) {return del_timer(std::forward<decltype(args)>(args)...); };
			polling_.handle_req_ = [this](auto &&...args) {return handle_req(std::forward<decltype(args)>(args)...); };
		}
		void init()
		{
			init_polling();
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
		bool upgrade_ = false;
		xnet::proactor_pool &pro_pool_;
		std::vector<std::function<void()>> close_callbacks_;
		std::map<std::string, std::function<void()>> event_handles_;
		std::function<void(const std::string &, session*)> regist_session_;
		std::function<bool(const std::string &, std::string &)> check_static_;
		std::function<bool(const std::string &)> check_sid_;
		std::function<bool(const std::string &)> check_transport_;
		std::function<std::vector<std::string>(const std::string &)> get_upgrades_;
		xnet::connection conn_;
		std::string msg_;
		std::string sid_ ;
		detail::polling polling_;
	};
}
#pragma once
namespace xsocket_io
{
	class session
	{
	public:
		session(xnet::proactor_pool &ppool)
			:pro_pool_(ppool)
		{
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
		void on_request(request &req)
		{
			auto sid = req.get_query().get("sid");
			if (sid != sid_)
			{
				if (!check_sid(sid))
				{
					auto origin = req.get_entry("Origin");
					req.write(build_resp(detail::to_json(detail::error_msg::e_session_id_unknown), 400, origin));
					return;
				}
				return on_request_(detach_request(&req));
			}
			auto method = req.method();
			auto path = req.path();
			auto b64 = !!req.get_query().get("b64").size();
			auto transport = req.get_query().get("transport");
			if (path == "/socket.io/")
			{
				if (method == "POST" && transport == "polling")
				{
					auto body = req.body();
					on_packet(detail::decode_packet(body, false));
					auto origin = req.get_entry("Origin");
					req.write(build_resp("ok", 200, origin));
				}
			}
			
		}
		bool check_add_user(const detail::packet &_packet)
		{
			if (_packet.playload_.size())
			{
				auto obj = xjson::build(_packet.playload_);
				if (obj.get<std::string>(0) == "add user")
				{
					polling_->resp_login(get_session_size_());
					return true;
				}
			}
			return false;
		}
		void on_packet(const std::list<detail::packet> &_packet)
		{
			if (polling_)
			{
				for (auto itr : _packet)
				{
					if (itr.packet_type_ == detail::packet_type::e_ping)
					{
						polling_->pong();
					}
					else if (itr.packet_type_ == detail::packet_type::e_message)
					{
						if(check_add_user(itr))
							continue;

					}
				}
			}
		}
		void attach_request(std::unique_ptr<request> req)
		{
			auto ptr = req.get();
			req->on_request_ = [this, ptr] {
				on_request(*ptr);
			};
			req->close_callback_ = [this, ptr] {
				detach_request(ptr);
			};
			on_request(*ptr);
			requests_.emplace_back(std::move(req));
		}
		std::unique_ptr<request> detach_request(request *ptr)
		{
			for (auto itr = requests_.begin(); itr != requests_.end(); ++itr)
			{
				if (itr->get() == ptr)
				{
					auto req_ptr = std::move(*itr);
					requests_.erase(itr);
					return req_ptr;
				}
			}
			return nullptr;
		}
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
			polling_->on_heartbeat_ = [this](auto &&...args) {return on_heartbeat(std::forward<decltype(args)>(args)...); };
			polling_->on_sid_= [this](auto &&...args) {return on_sid(std::forward<decltype(args)>(args)...); };
			polling_->on_packet_ = [this](auto &&...args) {return on_packet(std::forward<decltype(args)>(args)...); };
			polling_->check_sid_ = [this](auto &&...args) {return check_sid(std::forward<decltype(args)>(args)...); };
			polling_->check_transport_ = [this](auto &&...args) {return check_transport(std::forward<decltype(args)>(args)...); };
			polling_->check_static_ = [this](auto &&...args) {return check_static(std::forward<decltype(args)>(args)...); };
			polling_->get_upgrades_ = [this](auto &&...args) {return get_upgrades(std::forward<decltype(args)>(args)...); };
			polling_->set_timer_ = [this](auto &&...args) {return set_timer(std::forward<decltype(args)>(args)...); };
			polling_->del_timer_ = [this](auto &&...args) {return del_timer(std::forward<decltype(args)>(args)...); };
			polling_->handle_req_ = [this](auto &&...args) {return handle_req(std::forward<decltype(args)>(args)...); };
			polling_->close_callback_ = [this](auto &&...args) {return on_close(std::forward<decltype(args)>(args)...); };
			polling_->sid_ = sid_;
			polling_->init();
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

		void on_sid(const std::string &sid)
		{
			sid_ = sid;
		}
		void on_close()
		{
			close_callback_(this);
		}

		bool upgrade_ = false;
		xnet::proactor_pool &pro_pool_;
		std::function<void(session *)> close_callback_;
		std::function<session &(const std::string &sid)> get_session_;
		std::map<std::string, std::function<void()>> event_handles_;
		std::function<bool(const std::string &, std::string &)> check_static_;
		std::function<bool(const std::string &)> check_sid_;
		std::function<bool(const std::string &)> check_transport_;
		std::function<std::vector<std::string>(const std::string &)> get_upgrades_;
		std::function<void(std::unique_ptr<request> &&)> on_request_;
		std::function<std::size_t()> get_session_size_;
		xnet::connection conn_;
		std::string msg_;
		std::string sid_ ;
		std::unique_ptr<detail::polling> polling_;
		std::list<std::unique_ptr<request>> requests_;

	};
}
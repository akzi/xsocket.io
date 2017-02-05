#pragma once
namespace xsocket_io
{
	class session
	{
	public:
		struct broadcast_t
		{
			template<typename T>
			void emit(const std::string &event_name, const T &msg)
			{
				auto sessions = get_sessions_();
				for (auto itr: sessions)
				{
					if(itr->sid_ == sid_)
						continue;
					itr->emit(event_name, msg);
				}
			}
		private:
			friend class session;
			std::string sid_;
			std::function<std::list<session*>()> get_sessions_;
		};
		session(xnet::proactor_pool &ppool)
			:pro_pool_(ppool)
		{
		}
		template<typename Lambda>
		void on(const std::string &event_name, const Lambda &lambda)
		{
			regist_event(event_name, xutil::to_function(lambda));
		}

		template<typename T>
		void emit(const std::string &event_name,const T &msg)
		{
			xjson::obj_t obj;
			obj.add(event_name);
			obj.add(msg);

			detail::packet _packet;
			_packet.packet_type_ = detail::packet_type::e_message;
			_packet.playload_type_ = detail::playload_type::e_event;
			_packet.playload_ = obj.str();
			_packet.binary_ = !b64_;
			auto _request = polling_.lock();
			if (_request)
			{
				auto msg = detail::encode_packet(_packet);
				_request->write(build_resp(msg, 200, origin_, !b64_));
				return;
			}
			packets_.emplace_back(_packet);
		}
		
		std::string get_sid()
		{
			return sid_;
		}
		void set(const std::string &key, const std::string &value)
		{
			properties_[key] = value;
		}
		std::string get(const std::string &key)
		{
			return properties_[key];
		}
		broadcast_t broadcast;
	private:
		session(session &&sess) = delete;
		session(session &sess) = delete;
		session &operator = (session &&sess) = delete;
		session &operator = (session &sess) = delete;

		friend class xserver;

		struct cookie_opt
		{
			bool httpOnly_ = true;
			bool secure_ = false;
			std::string path_ = "/";
			int64_t expires_ = 0;

			enum class sameSite
			{
				Null,
				Strict,
				Lax
			};
			sameSite sameSite_ = sameSite::Null;
		};


		std::string make_cookie(const std::string &key, const std::string &value, cookie_opt opt)
		{
			std::string buffer;
			buffer.append(key);
			buffer.append("=");
			buffer.append(value);
			if (opt.path_.size())
			{
				buffer.append("; path=");
				buffer.append(opt.path_);
			}
			if (opt.secure_)
			{
				buffer.append("; Secure");
			}
			if (opt.httpOnly_)
			{
				buffer.append("; HttpOnly");
			}
			if (opt.expires_ > 0)
			{
				buffer.append("; Expires=");
				buffer.append(std::to_string(opt.expires_));
			}
			buffer.pop_back();
			return buffer;
		}
		void pong()
		{
			detail::packet _packet;
			_packet.packet_type_ = detail::packet_type::e_pong;
			auto _request = polling_.lock();
			if (_request)
			{
				_packet.binary_ = !b64_;
				_request->write(build_resp(encode_packet(_packet), 200, origin_, _packet.binary_));
				polling_.reset();
				return;
			}
			packets_.emplace_back(std::move(_packet));
		}

		void connect_ack()
		{
			connect_ack_ = true;
			auto _request = polling_.lock();

			detail::packet _packet;
			_packet.packet_type_ = detail::packet_type::e_message;
			_packet.playload_type_ = detail::playload_type::e_connect;
			_packet.binary_ = !b64_;

			if (_request)
			{
				_request->write(build_resp(encode_packet(_packet), 200, origin_, _packet.binary_));
				polling_.reset();
				return;
			}
			packets_.push_back(_packet);
		}
		void on_packet(const std::list<detail::packet> &_packet)
		{
			if (polling_.lock())
			{
				for (auto itr : _packet)
				{
					if (itr.packet_type_ == detail::packet_type::e_ping)
					{
						pong();
					}
					else if (itr.packet_type_ == detail::packet_type::e_close)
					{
						return on_close();
					}
					else if (itr.packet_type_ == detail::packet_type::e_message)
					{
						on_message(itr);
					}
				}
			}
		}

		void on_message(const detail::packet &_packet)
		{
			try
			{
				auto obj = xjson::build(_packet.playload_);
				std::string event = obj.get<std::string>(0);
				auto itr = event_handles_.find(event);
				if (itr == event_handles_.end())
					return;
				if(obj.size() == 1)
					itr->second(xjson::obj_t());
				else
					itr->second(obj.get(1));
			}
			catch (const std::exception& e)
			{
				std::cout << e.what() << std::endl;
			}
		}

		void on_polling(std::shared_ptr<request> &req)
		{
			polling_ = req;
			auto method = req->method();
			auto path = req->path();
			auto url = req->url();
			origin_ = req->get_entry("Origin");
			b64_ = !!req->get_query().get("b64").size();
			if (!connect_ack_)
			{
				connect_ack_ = true;
				return connect_ack();
			}
			if (packets_.empty())
				return;
			std::string buffer;
			for (auto &itr : packets_)
			{
				itr.binary_ = !b64_;
				buffer += detail::encode_packet(itr);
			}
			packets_.clear();
			req->write(build_resp(buffer, 200, origin_, !b64_));
		}
		void init()
		{
			broadcast.sid_ = sid_;
			broadcast.get_sessions_ = get_sessions_;
		}

		void regist_event(const std::string &event_name, std::function<void(xjson::obj_t&obj)> &&handle_)
		{
			std::function<void(xjson::obj_t&obj)> handle;
			auto func = [handle = std::move(handle_)](xjson::obj_t &obj){
				handle(obj);
			};
			if (event_handles_.find(event_name) != event_handles_.end())
				throw std::runtime_error("event");
			event_handles_.emplace(event_name, std::move(func));
		}

		void regist_event(const std::string &event_name, std::function<void()> &&handle_)
		{
			std::function<void(xjson::obj_t&obj)> handle;
			auto func = [handle = std::move(handle_)](xjson::obj_t &){
				handle();
			};
			if (event_handles_.find(event_name) != event_handles_.end())
				throw std::runtime_error("event");
			event_handles_.emplace(event_name, std::move(func));
		}

		void on_sid(const std::string &sid)
		{
			sid_ = sid;
		}
		void on_close()
		{
			auto itr = event_handles_.find("disconnect");
			if (itr != event_handles_.end())
				itr->second(xjson::obj_t());
			close_callback_(sid_);
		}

		bool connect_ack_ = false;
		bool upgrade_ = false;
		bool b64_ = false;
		std::string origin_;

		std::list<detail::packet> packets_;
		std::map<std::string, std::function<void(xjson::obj_t&)>> event_handles_;
		xnet::proactor_pool &pro_pool_;
		std::function<void(const std::string &)> close_callback_;
		std::function<void(std::shared_ptr<request>)> on_request_;
		std::function<std::list<session*>()> get_sessions_;
		xnet::connection conn_;
		std::string sid_ ;

		std::weak_ptr<request> polling_;
		std::map<std::string, std::string > properties_;
	};
}
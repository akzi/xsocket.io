#pragma once
namespace xsocket_io
{
	class socket: public std::enable_shared_from_this<socket>
	{
	public:
		struct broadcast_t
		{
			template<typename T>
			void emit(const std::string &event_name, const T &msg)
			{
				socket_->do_broadcast(rooms_, event_name, msg);
				rooms_.clear();
			}
			void to(const std::string &name)
			{
				rooms_.insert(name);
			}
			void in(const std::string &name)
			{
				rooms_.insert(name);
			}
		private:
			friend class socket;
			std::set<std::string> rooms_;
			socket *socket_;
		};
		socket()
		{
		}
		~socket()
		{
			if (timer_id_)
				cancel_timer_(timer_id_);

			auto rooms = rooms_;
			for (auto &itr : rooms)
				leave(itr);
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
			
			if(in_rooms_.empty())
				return send_packet(_packet);
			broadcast_room(in_rooms_, _packet);
			in_rooms_.clear();
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
		void join(const std::string &room)
		{
			if (rooms_.find(room) == rooms_.end())
			{
				rooms_.insert(room);
				join_room_(room, shared_from_this());
				return;
			}
		}
		void leave(const std::string &room)
		{
			rooms_.erase(room);
			leave_room_(room, shared_from_this());
		}
		socket &in(const std::string &room)
		{
			in_rooms_.insert(room);
			return *this;
		}
		socket &to(const std::string &room)
		{
			in_rooms_.insert(room);
			return *this;
		}
		broadcast_t broadcast;
	private:
		socket(socket &&sess) = delete;
		socket(socket &sess) = delete;
		socket &operator = (socket &&sess) = delete;
		socket &operator = (socket &sess) = delete;

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
		void broadcast_room(std::set<std::string> &rooms, 
			detail::packet &_packet)
		{
			std::set<std::string> ids;
			for (auto &itr : rooms)
			{
				auto &sockets = get_socket_from_room_(itr);
				for (auto &sock : sockets)
				{
					auto ptr = sock.second.lock();
					if (!ptr)
						continue;
					if (ptr->nsp_ != nsp_)
						continue;
					if (ids.find(sock.first) == ids.end())
					{
						ptr->send_packet(_packet);
						ids.insert(sock.first);
					}
				}
			}
		}
		template<typename T>
		void do_broadcast(std::set<std::string> &rooms_, 
			const std::string &event_name, const T &msg)
		{
			xjson::obj_t obj;
			obj.add(event_name);
			obj.add(msg);

			detail::packet _packet;
			_packet.packet_type_ = detail::packet_type::e_message;
			_packet.playload_type_ = detail::playload_type::e_event;
			_packet.playload_ = obj.str();

			if (rooms_.empty())
			{
				auto &sockets = get_sockets_();
				for (auto &itr : sockets)
				{
					if (itr.first == sid_ || itr.second->nsp_ != nsp_)
						continue;
					itr.second->send_packet(_packet);
				}
				return;
			}
			broadcast_room(rooms_, _packet);
		}
		void send_packet(detail::packet &_packet)
		{
			_packet.nsp_ = nsp_;
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
		std::string make_cookie(const std::string &key, 
			const std::string &value, 
			cookie_opt opt)
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
			send_packet(_packet);
		}

		void connect_ack(const std::string &nsp,
			const std::string &playload = {}, 
			detail::playload_type _playload_type = detail::playload_type::e_connect)
		{
			connect_ack_ = true;
			detail::packet _packet;
			_packet.packet_type_ = detail::packet_type::e_message;
			_packet.playload_type_ = _playload_type;
			_packet.binary_ = !b64_;
			_packet.playload_ = playload;
			_packet.nsp_ = nsp;
			send_packet(_packet);
		}
		void on_packet(const std::list<detail::packet> &_packet)
		{
			for (auto &itr : _packet)
			{
				if (itr.packet_type_ == 
					detail::packet_type::e_ping)
				{
					pong();
					set_timeout();
				}
				else if (itr.packet_type_ == 
					detail::packet_type::e_close)
				{
					return on_close();
				}
				else if (itr.packet_type_ == 
					detail::packet_type::e_message)
				{
					if (itr.playload_type_ == 
						detail::playload_type::e_event)
					{
						if(nsp_ == itr.nsp_)
							on_message(itr);
					}
					else if (itr.playload_type_ ==
						detail::playload_type::e_connect)
					{
						nsp_ = itr.nsp_;
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
				COUT_ERROR(e);
			}
		}

		void on_polling(std::shared_ptr<request> &req)
		{
			auto method = req->method();
			auto path = req->path();
			auto url = req->url();
			origin_ = req->get_entry("Origin");
			b64_ = !!req->get_query().get("b64").size();
			
			if (on_connection_)
			{
				if (!on_connection_(nsp_, *this))
					connect_ack(nsp_, 
						"\"Invalid namespace\"", 
						detail::playload_type::e_error);
				else if(nsp_ != "/")
					connect_ack(nsp_);
				on_connection_ = nullptr;
			}
			
			polling_ = req;
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
			broadcast.socket_ = this;
			connect_ack(nsp_);
		}

		void regist_event(const std::string &event_name,
			std::function<void(xjson::obj_t&obj)> &&handle_)
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
			if (!on_connection_)
			{
				auto itr = event_handles_.find("disconnect");
				if (itr != event_handles_.end())
					itr->second(xjson::obj_t());
			}
			close_callback_(sid_);
		}
		void set_timeout()
		{
			if (timer_id_)
				cancel_timer_(timer_id_);

			timer_id_ = set_timer_(timeout_, [this] {
				detail::packet _packet;
				_packet.packet_type_ = detail::e_close;
				send_packet(_packet);
				timer_id_ = 0;
				on_close();
				return false;
			});
		}
		bool connect_ack_ = false;
		bool upgrade_ = false;
		bool b64_ = false;
		int32_t timeout_;
		std::string origin_;

		std::list<detail::packet> packets_;
		std::map<std::string, std::function<void(xjson::obj_t&)>> event_handles_;
		std::function<void(const std::string &)> close_callback_;
		std::function<void(std::shared_ptr<request>)> on_request_;
		std::function<std::map<std::string, std::shared_ptr<socket>>&()> get_sockets_;
		
		std::function<std::size_t(int32_t, std::function<bool()> &&)> set_timer_;
		std::function<void(std::size_t)> cancel_timer_;
		std::size_t timer_id_ = 0;

		xnet::connection conn_;
		std::string sid_ ;
		std::string nsp_ = "/";

		std::weak_ptr<request> polling_;
		std::map<std::string, std::string > properties_;

		std::function<bool(const std::string &, socket&)> on_connection_;
		

		std::set<std::string> rooms_;
		std::set<std::string> in_rooms_;

		std::function<void(const std::string &, std::shared_ptr<socket>&)> join_room_;
		std::function<void(const std::string &, std::shared_ptr<socket>&)> leave_room_;

		std::function<std::map<std::string, std::weak_ptr<socket>>(const std::string &)> get_socket_from_room_;
	};
}
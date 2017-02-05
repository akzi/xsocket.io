#pragma once
namespace xsocket_io
{
	class xserver
	{
	public:
		using handle_request_t = std::function<void(request, response)> ;
		using handle_session_close_t = std::function<void(session&)>;
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
		int64_t uid()
		{
			static std::atomic_int64_t uid_ = 0;
			return uid_++;
		}
		void init()
		{
			proactor_pool_.regist_accept_callback([this](xnet::connection &&conn) {
				std::unique_ptr<request> request_ptr(new request(std::move(conn)));
				attach_request(std::move(request_ptr));
			});
		}
		int64_t attach_request(std::unique_ptr<request> &&request_ptr)
		{
			auto id = uid();
			auto ptr = request_ptr.get();
			request_ptr->on_request_ = [id, this, ptr] {
				on_request(id, *ptr);
			};
			request_ptr->close_callback_ = [id, this] {
				detach_request(id);
			};
			requests_.emplace(id, std::move(request_ptr));
			return id;
		}

		std::unique_ptr<request> detach_request(int64_t id)
		{
			std::lock_guard<std::mutex> _lock_guard(requests_mutex_);

			std::unique_ptr<request> req_ptr;
			auto itr = requests_.find(id);
			assert(itr != requests_.end());
			req_ptr = std::move(itr->second);
			requests_.erase(itr);
			return req_ptr;
		}
		void on_request(std::unique_ptr<request> &&req_ptr)
		{
			auto ptr = req_ptr.get();
			auto id = attach_request(std::move(req_ptr));
			on_request(id, *ptr);
		}
		void on_request(int64_t id, request &req)
		{
			if (req.method() == "POST")
			{
				auto sid = req.get_query().get("sid");
				if (sid.size())
				{
					if (!check_sid(sid))
					{
						auto origin = req.get_entry("Origin");
						req.write(build_resp(detail::to_json(detail::error_msg::e_session_id_unknown), 400, origin));
						return;
					}
					auto req_ptr = detach_request(id);
					do 
					{
						std::unique_lock<std::mutex> lock_g(session_mutex_);
						auto itr = sessions_.find(sid);
						itr->second->attach_request(std::move(req_ptr));
					} while (false);
				}
			}
			else if (req.method() == "GET")
			{
				auto url = req.url();
				if (url.find('?') == url.npos)
				{
					std::string filepath;
					if (check_static(url, filepath))
					{
						req.send_file(filepath);
						return;
					}
				}
				auto &query = req.get_query();
				auto transport = query.get("transport");
				auto sid = query.get("sid");
				auto t = query.get("t");
				auto path = req.path();
				if (path == "/socket.io/")
				{
					if (sid.empty())
					{
						return handle_new_session(id, req);
					}
				}
				
			}
		}
		void handle_new_session(int64_t id, request &req)
		{
			using namespace detail;
			auto sid = gen_sid();

			xjson::obj_t obj;
			packet _packet;
			open_msg msg;
			msg.pingInterval = ping_interval_;
			msg.pingTimeout = ping_timeout_;
			msg.upgrades = get_upgrades(req.query_.get("transport"));
			msg.sid = sid;

			obj = msg;
			_packet.binary_ = !!!req.get_entry("b64").size();
			_packet.packet_type_ = packet_type::e_open;
			_packet.is_string_ = true;
			_packet.playload_type_ = playload_type::e_null1;
			_packet.playload_ = obj.str();

			req.write(build_resp(encode_packet(_packet), 200, req.get_entry("Origin"), _packet.binary_));
			
			new_session(sid, detach_request(id));
		}
		
		std::string gen_sid()
		{
			static std::atomic_int64_t uid = 0;
			auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
			auto id = uid.fetch_add(1) + now;
			auto sid = xutil::base64::encode(std::to_string(id));
			while(sid.size() && sid.back() =='=')
				sid.pop_back();
			return sid;
		}

		std::size_t get_session_size()
		{
			return sessions_.size();
		}
		void new_session(const std::string &sid, std::unique_ptr<request> &req)
		{
			std::unique_ptr<session> sess(new session(proactor_pool_));
			sess->check_static_ = [this](auto &&...args) { return check_static(std::forward<decltype(args)>(args)...); };
			sess->get_upgrades_ = [this](auto &&...args) { return get_upgrades(std::forward<decltype(args)>(args)...); };
			sess->check_sid_ = [this](auto &&...args) { return check_sid(std::forward<decltype(args)>(args)...); };
			sess->close_callback_ = [this](auto &&...args) { return session_on_close(std::forward<decltype(args)>(args)...); };
			sess->on_request_ = [this](auto &&...args) { return on_request(std::forward<decltype(args)>(args)...); };
			sess->get_session_size_ = [this](auto &&...args) { return get_session_size(std::forward<decltype(args)>(args)...); };
			sess->polling_.reset(new detail::polling(std::move(req)));
			sess->sid_ = sid;
			sess->init();

			auto ptr = sess.get();
			do 
			{
				std::unique_lock<std::mutex> lock_g(session_mutex_);
				sessions_.emplace(sid, std::move(sess));
			} while (0);
			
			if(connection_callback_)
				connection_callback_(*ptr);
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

		void session_on_close(session *sess)
		{
			std::unique_lock<std::mutex> lock_g(session_mutex_);
			std::unique_ptr<session> ptr;
			auto itr = sessions_.find(sess->get_sid());
			if (itr != sessions_.end())
			{
				ptr = std::move(itr->second);
				sessions_.erase(itr);
				return;
			}
			if(handle_session_close_)
				handle_session_close_(*ptr);
		}
		std::vector<std::string> get_upgrades(const std::string &transport)
		{
			return {};
		}
		std::mutex session_mutex_;
		std::map<std::string, std::unique_ptr<session>> sessions_;

		std::mutex requests_mutex_;
		std::map<int64_t, std::unique_ptr<request>> requests_;

		std::function<void(session &)> connection_callback_;
		xnet::proactor_pool proactor_pool_;

		handle_request_t handle_request_;
		handle_session_close_t handle_session_close_;
		std::string static_path_;

		uint32_t ping_interval_ = 2500;
		uint32_t ping_timeout_ = 6000;
	};
}
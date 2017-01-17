#pragma once
#include <map>
#include "../../xwebsocket/include/websocket.hpp"
#include "detail/detail.hpp"
#include "session.hpp"

namespace xsocket_io
{
	class socketio
	{
	public:
		socketio()
		{

		}
		void on(const std::string &name, const std::function<void(session &&)> &handle)
		{
			if (name == "connection")
			{
				connection_callback_ = handle;
				return;
			}
		}
	private:
		void init()
		{
			server_.regist_accept_callback([this](xwebsocket::session &&ws_sess) {
				websocket_accept_callback(std::move(ws_sess));
			});
		}
		void websocket_accept_callback(xwebsocket::session &&ws_sess)
		{
			session sess(std::move(ws_sess));
			sess.regist_session_ = [this](session &sess) { regist_session(sess); };
			sess.init();
			assert(connection_callback_);
			connection_callback_(std::move(sess));
		}
		void regist_session(session &sess)
		{
			if(!sess.session_id_)
				sess.session_id_ = gen_session_id();
			sessions_[sess.session_id_] = &sess;
		}
		int64_t gen_session_id()
		{
			return session_id_++;
		}

		int64_t session_id_ = 1;
		std::map<int64_t, session*> sessions_;
		xwebsocket::xserver server_;
		std::function<void(session &&)> connection_callback_;
	};
}
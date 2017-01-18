#pragma once
namespace xsocket_io
{
	class xserver
	{
	public:
		xserver()
		{

		}

		void on_connection(const std::function<void(session &&)> &handle)
		{
			connection_callback_ = handle;
		}
		void on_close()
		{

		}
		void on_get()
		{

		}

		void on_post()
		{

		}

		
	private:
		void init()
		{
			proactor_pool_.regist_accept_callback([this](xnet::connection &&conn) {

			});
		}
		int64_t gen_session_id()
		{
			return session_id_++;
		}

		int64_t session_id_ = 1;
		std::map<int64_t, session> sessions_;
		std::function<void(session &&)> connection_callback_;
		xnet::proactor_pool proactor_pool_;
	};
}
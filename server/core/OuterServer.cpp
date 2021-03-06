#include "../pre_compile.h"
#include "Session.h"
#include "OuterServer.h"

#include "../network/SocketAddress.h"
#include "IOContext.h"
#include "../DBTask.h"




OuterServer::OuterServer()
	: listen_sock{ INVALID_SOCKET }, completion_port{ INVALID_HANDLE_VALUE }, accepter{ INVALID_HANDLE_VALUE }, session_heap{ INVALID_HANDLE_VALUE }
	, io_handler{ nullptr }, ip{}, port{ 0 }, sessions{ }
	, custom_last_server_error{ c2::enumeration::ER_NONE }, custom_last_os_error{ c2::enumeration::ER_NONE } // error'
	, maximum_accept_count{ 0 }, concurrent_thread_count{ 0 }
	, version{}
	, enable_nagle_opt{ false }, enable_keep_alive_opt{}
	, maximum_listening_count{}, capacity{}
	, total_recv_count{}, total_recv_bytes{}, total_sent_bytes{}, total_sent_count{}, current_accepted_count{}
{}

OuterServer::~OuterServer()
{}

bool OuterServer::init_network_and_system()
{
	WSAData wsa;
	if (0 != WSAStartup(MAKEWORD(2, 2), &wsa))
	{
		//debug_code(printf("%s::%s \n", __FILE__, #__LINE__));

		this->custom_last_os_error = c2::enumeration::ER_COMPLETION_PORT_INITIATION_FAILURE;

		return false;
	}

	completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, c2::global::concurrent_io_thread_count);
	if (NULL == completion_port)
	{
		debug_code(printf("%s::%s \n", __FILE__, __LINE__));

		this->custom_last_os_error = c2::enumeration::ER_COMPLETION_PORT_INITIATION_FAILURE;

		return false;
	}

	listen_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == this->listen_sock)
	{
		debug_code(printf("%s::%d \n", __FILE__, __LINE__));

		return false;
	}


	HANDLE returned_hanlde = CreateIoCompletionPort((HANDLE)this->listen_sock, this->completion_port, c2::constant::ASYNC_ACCEPT_SIGN, 0);
	if (returned_hanlde == NULL || returned_hanlde != this->completion_port)
	{
		debug_code(printf("%s::%s \n", __FILE__, __LINE__));

		return false;
	}

	// linger
	linger linger_opt{ 1, 0 };
	if (SOCKET_ERROR == setsockopt(listen_sock, SOL_SOCKET, SO_LINGER, (char*)&linger_opt, sizeof(LINGER)))
	{
		debug_code(printf("%s::%s \n", __FILE__, __LINE__));

		return false;
	}

	if (true == this->enable_nagle_opt) //네이글 알고리즘 on/off
	{
		int option = TRUE;
		setsockopt(this->listen_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&option, sizeof(option));
	}

	DWORD bytesReturned{ 0 };
	tcp_keepalive option{ true, 10000, 1000 };
	WSAIoctl(listen_sock, SIO_KEEPALIVE_VALS, &option, sizeof(tcp_keepalive), 0, 0, &bytesReturned, NULL, NULL);

	SocketAddress sock_addr{ this->ip, this->port };
	if (SOCKET_ERROR == ::bind(this->listen_sock, sock_addr.get_as_sockaddr(), sock_addr.size()))
	{
		printf("server initation... bind error : %d\n", GetLastError());
		return false;
	}

	DWORD bytes = 0;
	GUID uuid_accept_ex = WSAID_ACCEPTEX;
	if (SOCKET_ERROR == WSAIoctl(listen_sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&uuid_accept_ex, sizeof(uuid_accept_ex),
		&OuterServer::accept_ex, sizeof(OuterServer::accept_ex),
		&bytes, NULL, NULL))
	{
		this->custom_last_os_error = c2::enumeration::ER_ACCEPTEX_LAODING_FAILURE;
		return false;
	}

	bytes = 0;
	GUID guid_disconnect_ex = WSAID_DISCONNECTEX;
	if (SOCKET_ERROR == WSAIoctl(listen_sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid_disconnect_ex, sizeof(guid_disconnect_ex),
		&OuterServer::disconnect_ex, sizeof(OuterServer::disconnect_ex),
		&bytes, NULL, NULL))
	{
		this->custom_last_os_error = c2::enumeration::ER_ACCEPTEX_LAODING_FAILURE;
		return false;
	}


	bytes = 0;
	GUID guid_connect_ex = WSAID_CONNECTEX;
	if (SOCKET_ERROR == WSAIoctl(listen_sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guid_connect_ex, sizeof(guid_connect_ex),
		&OuterServer::connect_ex, sizeof(OuterServer::connect_ex),
		&bytes, NULL, NULL))
	{
		this->custom_last_os_error = c2::enumeration::ER_CONNECTEX_LAODING_FAILURE;
		return false;
	}

	return true;



}

bool OuterServer::init_sessions()
{
	session_heap = HeapCreate(/* HEAP_ZERO_MEMORY */ HEAP_GENERATE_EXCEPTIONS, 0, NULL);
	if (INVALID_HANDLE_VALUE == session_heap)
	{
		c2::util::crash_assert();
	}

	// 배열 생성.
	sessions = (Session**)HeapAlloc(session_heap, HEAP_GENERATE_EXCEPTIONS | HEAP_ZERO_MEMORY, sizeof(Session*) * this->capacity);
	for (int n = 0; n < this->capacity; ++n)
	{
		id_pool.push(n);
	}

	on_create_sessions(this->capacity);

	//this->sessions = created_sessions;

	for (int n = 0; n < this->capacity; ++n)
	{
		//new(&sessions[n]) Session();
		sessions[n]->session_id = n;
		sessions[n]->server = this;
	}

	return true;
}

bool OuterServer::init_threads()
{
	uint64_t n = 0;

	if (c2::global::concurrent_io_thread_count < 1)
	{
		c2::util::crash_assert();
	}

	for (; n < c2::global::concurrent_io_thread_count; ++n)
	{
		void* params = new void* [3]{ (void*)this, (void*)c2::enumeration::ThreadType::TT_IO_AND_TIMER, (void*)n };

		accepter = (HANDLE)_beginthreadex(NULL, NULL, OuterServer::start_thread, params, NULL, NULL);
	}

	// 접속을 나중에 받기 위해 ㅇㅇ
	void* params = new void* [3]{ (void*)this, (void*)c2::enumeration::ThreadType::TT_ACCEPTER, (void*)n };
	accepter = (HANDLE)_beginthreadex(NULL, NULL, OuterServer::start_thread, params, NULL, NULL);

	return true;
}

void OuterServer::start()
{
	on_start();
}

void OuterServer::acceptor_procedure(uint64_t idx)
{
	SOCKET		local_listen_sock = this->listen_sock;
	uint64_t	post_accepted_counter = 0;

	if (SOCKET_ERROR == listen(local_listen_sock, SOMAXCONN_HINT(this->maximum_listening_count)))
	{
		this->custom_last_os_error = c2::enumeration::ER_ACCEPTEX_LAODING_FAILURE; // ;
		return;
	}

	//// concurrent_stack 을통해서 꺼냄.
	//for (; post_accepted_counter < this->capacity; )
	//{
	//	// 세션을 얻고...

	//	post_accepted_counter += 1;

	//	Sleep(100);
	//}

	const uint16_t	maximum_accept_waiting_count = this->maximum_accept_count;
	size_t			accpet_waiting_count = 0;
	size_t			id = 0;

	// session 꺼내서 
	for (;;)
	{
		//  대기인수보다 적게 대기 하고 있거나  +  stack 비어있지 않을때
		while (accpet_waiting_count - this->current_accepted_count < maximum_accept_waiting_count)
		{
			if (id_pool.try_pop(id))
			{
				Session* session = this->acquire_session_ownership(id);

				session->post_accept();

				accpet_waiting_count += 1;

				release_session_ownership(id);
			}
			else
			{
				break;
			}
		}

		Sleep(20);
	}


}


void OuterServer::io_service_procedure(uint64_t custom_thread_id)
{
	OuterServer::local_storage_accessor = custom_thread_id;
	HANDLE	local_completion_port{ this->completion_port };
	int64_t	thread_id{ GetCurrentThreadId() };

	for (;;)
	{

		DWORD			transfered_bytes	{};
		LPOVERLAPPED	overlapped_ptr		{};
		ULONG_PTR		completion_key		{};

		bool ret = GetQueuedCompletionStatus(local_completion_port, &transfered_bytes, &completion_key, &overlapped_ptr, INFINITE);
		if (ret == 0)
		{
			if (GetLastError() == WAIT_TIMEOUT)
				continue;
		}
		else if (transfered_bytes == 0 && overlapped_ptr == nullptr && completion_key == 0)
		{
			return;
		}

		on_wake_io_thread();

		// accpet 처리.
		if (completion_key == c2::constant::ASYNC_ACCEPT_SIGN)
		{
			IoContext* context_ptr{ reinterpret_cast<IoContext*>(overlapped_ptr) };
			Session* session = (Session*)context_ptr->session;
			
			session->accept_completion();
			on_accept(session);
			continue;
		}

		// 
		Session* session = acquire_session_ownership(completion_key);
		if (nullptr == session)
		{
			c2::util::crash_assert();
		}

		// acquire session
		if ((size_t)overlapped_ptr == c2::constant::SEND_SIGN) 
		{
			session->post_send();

			release_session_ownership(completion_key);

			continue;
		}

		IoContext* context_ptr	{ reinterpret_cast<IoContext*>(overlapped_ptr) };
		switch (context_ptr->io_type)
		{
		case IO_RECV:
			session->recv_completion(transfered_bytes);
			break;

		case IO_SEND:
			session->send_completion(transfered_bytes);
			break;

		case IO_DISCONNECT:
			session->disconnect_completion();
			on_disconnect(session->session_id);
			break;

		case IO_ACCEPT:
			//session->accept_completion();
			//break;
		default:
			c2::util::crash_assert();
			break;
		}

		// release session
		release_session_ownership(completion_key);

		on_sleep_io_thread();
	}
}

#include "../PathFinder.h"

void OuterServer::io_and_timer_service_procedure(uint64_t custom_thread_id)
{
	local_timer = new TimeTaskScheduler{};
	local_timer->bind_server(this);
	TimeTaskScheduler* local_timer_capture = local_timer;
	printf("%d :: worker therad start !\n", GetCurrentThreadId());
	OuterServer::local_storage_accessor = custom_thread_id;
	c2::local::io_thread_id = custom_thread_id;

	local_path_finder = new PathFinder();

	HANDLE	local_completion_port{ this->completion_port };
	int64_t	thread_id{ GetCurrentThreadId() };

	for (;;)
	{
		local_timer_capture->do_timer_job();

		DWORD			transfered_bytes{};
		LPOVERLAPPED	overlapped_ptr{};
		ULONG_PTR		completion_key{};

		bool ret = GetQueuedCompletionStatus(local_completion_port, &transfered_bytes, &completion_key, &overlapped_ptr, c2::constant::GQCS_TIME_OUT);
		if (ret == 0)
		{
			if (GetLastError() == WAIT_TIMEOUT)
				continue;
		}
		else if (transfered_bytes == 0 && overlapped_ptr == nullptr && completion_key == 0)
		{
			return;
		}

		on_wake_io_thread();

		// accpet 처리.
		if (completion_key == c2::constant::ASYNC_ACCEPT_SIGN)
		{
			IoContext* context_ptr{ reinterpret_cast<IoContext*>(overlapped_ptr) };
			Session* session = (Session*)context_ptr->session;

			session->accept_completion();
			on_accept(session);
			continue;
		}

		// 
		Session* session = acquire_session_ownership(completion_key);
		if (nullptr == session)
		{
			c2::util::crash_assert();
		}

		// acquire session
		switch ((size_t)transfered_bytes)
		{
			case c2::constant::SEND_SIGN:
			{
				session->post_send();
				release_session_ownership(completion_key);
				continue;
			}
			case c2::constant::DB_SIGN:
			{
				session->on_handling_db_task(reinterpret_cast<DBTask*>(overlapped_ptr));
				release_session_ownership(completion_key);
				continue;
			}
			default:
				break;
		}

		IoContext* context_ptr{ reinterpret_cast<IoContext*>(overlapped_ptr) };
		switch (context_ptr->io_type)
		{
		case IO_RECV:
			session->recv_completion(transfered_bytes);
			break;

		case IO_SEND:
			session->send_completion(transfered_bytes);
			break;

		case IO_DISCONNECT:
			session->disconnect_completion();
			on_disconnect(session->session_id);
			break;

		case IO_ACCEPT:
			//session->accept_completion();
			//break;
		default:
			c2::util::crash_assert();
			break;
		}

		// release session
		release_session_ownership(completion_key);

		on_sleep_io_thread();
	}
}

void OuterServer::custom_precedure(uint64_t idx)
{
}




uint32_t WINAPI OuterServer::start_thread(LPVOID param)
//void OuterServer::start_thread(OuterServer* server, c2::enumeration::ThreadType thread_type, void* param)
{
	using namespace c2::enumeration;

	ThreadInfo* info = reinterpret_cast<ThreadInfo*>(param);

	switch (info->thread_tye)
	{
	case ThreadType::TT_ACCEPTER:
		info->server->acceptor_procedure(info->index);
		break;

	case ThreadType::TT_IO:
		info->server->io_service_procedure(info->index);
		break;

	case ThreadType::TT_IO_AND_TIMER:
		info->server->io_and_timer_service_procedure(info->index);
		break;

	case ThreadType::TT_CUSTOM:
		info->server->custom_precedure(info->index);
		break;

	default:
		c2::util::crash_assert();
		break;
	}

	delete info;

	return 0;
}



bool OuterServer::initialize()
{
	printf("server init... ");
	do
	{
		if (false == init_network_and_system())
		{
			printf("n");
			break;
		}

		if (false == init_sessions())
		{
			printf("s");

			break;
		}
		if (false == init_threads())
		{
			printf("t");
			break;
		}

		printf("ok.");

		return true;

	} while (false);


	printf("failure.");

	return false;
}

void OuterServer::finalize()
{
	this->destroy_sessions();
}

void OuterServer::on_connect(uint64_t session_id){}
bool OuterServer::on_accept(Session* session)
{
	return false;
}

void OuterServer::on_disconnect(uint64_t session_id){}
void OuterServer::on_wake_io_thread() {}
void OuterServer::on_sleep_io_thread() {}

void OuterServer::on_update()
{
}

void OuterServer::on_start()
{

}

void OuterServer::on_timer_service(const TimerTask& timer_job)
{
}

//void OuterServer::on_timer_service(const TimerTask& timer_job)

void OuterServer::on_create_sessions(size_t n)
{
	Session* sessions_ptr = (Session*)HeapAlloc(session_heap, 0, sizeof(Session) * n);
	
	for (size_t i = 0; i < n; ++i)
	{
		sessions[i] = sessions_ptr;
		new(sessions[i]) Session();
	}

	//return reinterpret_cast<Session*>(new Session[n]);
}

void OuterServer::destroy_sessions()
{
	HeapDestroy(session_heap); // 실패하든가 말든가 ㅋㅋ 에러 췤 안함
	//session_heap = INVALID_HANDLE_VALUE;
}


void OuterServer::request_disconnection(uint64_t session_id, c2::enumeration::DisconnectReason dr)
{
	Session* session = acquire_session_ownership(session_id);
	if (session == nullptr)
		return;

	if (session->release_flag != 1)
	{
		//debug_console(printf("OuterServer::disconnect() : session_id : %d, ref_count : %d", session->session_id, session->refer_count));
		c2::util::crash_assert();
	}


	session->discon_context.overalpped.Internal = 0;
	session->discon_context.overalpped.InternalHigh = 0;

	//InterlockedIncrement64(&session->refer_count);

	// session에서 하고 

	if (FALSE == OuterServer::disconnect_ex(session->sock, &session->discon_context.overalpped, TF_REUSE_SOCKET, 0))
	{
		DWORD error_code = GetLastError();
		switch (error_code)
		{
		case WSA_IO_PENDING: // IO_PENDING이면 넘김.
			return;

		case WSAENOTCONN: // 이미 종료된 소켓 
			//release_session_ownership(session_id);
			InterlockedDecrement64(&session->refer_count);
			release_session(session);
			return;
			//case WSAEINVAL:
			//	break;

		default:
			c2::util::crash_assert(); // 덤프 남겨보자.
			break;
		}
	}

}


// disconnectEx에서 처리.
void OuterServer::release_session(Session* session)
{
	for (int n = 0; n < session->packet_sent_count; ++n)
	{
		session->sent_packets[n]->decrease_ref_count();
	}

	session->reset();

	uint64_t session_id = session->session_id;

	id_pool.push(increase_session_stamp(session_id));
}


void OuterServer::disconnect_after_sending_packet(uint64_t session_id, c2::Packet* out_packet)
{
}

void OuterServer::send_packet(uint64_t session_id, c2::Packet* out_packet)
{
	Session* session = this->acquire_session_ownership(session_id);
	if (nullptr == session)
	{
		out_packet->decrease_ref_count();

		this->release_session_ownership(session_id);

		return;
	}
	// packet이 들어옴.
	// concurrent_queue에 넣음.
	session->send_buffer.push(out_packet);

	// send중이 아니라면....
	if (session->send_flag == 0)
	{
		PostQueuedCompletionStatus(this->completion_port, c2::constant::SEND_SIGN, (ULONG_PTR)session_id, (LPOVERLAPPED)0);
	}

	this->release_session_ownership(session_id);
}





void OuterServer::set_custom_last_error(c2::enumeration::ErrorCode err_code)
{
	this->custom_last_os_error = err_code;
}

void OuterServer::setup_dump()
{
	SetUnhandledExceptionFilter(c2::diagnostics::exception_filter);
}

Session* OuterServer::acquire_session_ownership(int64_t index)
{
	Session* session = sessions[(uint16_t)index];
	if (0 >= InterlockedIncrement64(&session->refer_count))
	{
		debug_code(printf("%s:%s \n", __FILE__, __LINE__));
		c2::util::crash_assert();
	}

	if (index != session->session_id) // 동일한 녀석인지 확인.
	{
		// 문제가 발생한 상황.
		inline_decrease_refer(session, c2::enumeration::DisconnectReason::DR_NONE);
		
		c2::util::crash_assert();

		return nullptr;
	}

	return session;
}

void OuterServer::release_session_ownership(int64_t session_id)
{
	Session* session = sessions[(uint16_t)session_id];
	uint64_t ret_val = InterlockedDecrement64(&session->refer_count);

	if (1 > ret_val)
	{
		//if (0 == ret_val)
		//{
			if (0 == InterlockedExchange(&session->release_flag, 1))
			{
				request_disconnection(session->session_id, c2::enumeration::DisconnectReason::DR_NONE);
			}

			return;
		//}
		// else
		//{
		//	c2::util::crash_assert();
		//}*/

	}
}

/////////////////////// getter / setter 


size_t OuterServer::get_total_recv_bytes()
{
	return  InterlockedExchange64(&this->total_recv_bytes, 0);
	//size_t total = 0;

	//for (size_t n = 0; n < this->concurrent_thread_count; ++n)
	//{
	//	if (total_recv_bytes[n] != nullptr)
	//	{
	//		total += *total_recv_bytes[n];
	//	}
	//}

	//return total;
}

size_t OuterServer::get_total_sent_bytes()
{
	//size_t total = 0;
	return  InterlockedExchange64(&this->total_sent_bytes, 0);
	/*for (size_t n = 0; n < this->concurrent_thread_count; ++n)
	{
		if (total_sent_bytes[n] != nullptr)
		{
			total += *total_sent_bytes[n];
		}
	}*/

	//return total;
}

size_t OuterServer::get_total_recv_count()
{
	return  InterlockedExchange64(&this->total_recv_count, 0);

}

size_t OuterServer::get_total_sent_count()
{
	return  InterlockedExchange64(&this->total_sent_count, 0);
}

constexpr size_t OuterServer::get_ccu() const
{
	return maximum_accept_count;
}

HANDLE OuterServer::get_completion_port() const
{
	return completion_port;
}

const wchar_t* OuterServer::get_version() const
{
	return version;
}

const c2::enumeration::ErrorCode OuterServer::get_os_last_error() const
{
	return this->custom_last_os_error;
}

const c2::enumeration::ErrorCode OuterServer::get_server_last_error() const
{
	return this->custom_last_server_error;
}



void OuterServer::load_config_using_json(const wchar_t* file_name)
{
	c2::util::JsonParser json_parser;

	if (false == json_parser.load_json(file_name))
		c2::util::crash_assert();

	if (false == json_parser.get_raw_wstring(L"server_version", this->version, count_of(version)))
		c2::util::crash_assert();

	if (false == json_parser.get_boolean(L"enable_nagle_opt", this->enable_nagle_opt))
		c2::util::crash_assert();

	if (false == json_parser.get_boolean(L"enable_keep_alive_opt", this->enable_keep_alive_opt))
		c2::util::crash_assert();

	if (false == json_parser.get_uint64(L"concurrent_thread_count", c2::global::concurrent_io_thread_count))
		c2::util::crash_assert();
	
	this->concurrent_thread_count = c2::global::concurrent_io_thread_count;


	if (false == json_parser.get_uint16(L"server_port", this->port))
		c2::util::crash_assert();

	if (false == json_parser.get_raw_wstring(L"server_ip", this->ip, count_of(ip)))
		c2::util::crash_assert();

	if (false == json_parser.get_uint16(L"capacity", this->capacity))
		c2::util::crash_assert();

	if (false == json_parser.get_uint16(L"maximum_listening_count", this->maximum_listening_count))
		c2::util::crash_assert();

	if (false == json_parser.get_uint16(L"maximum_accept_count", this->maximum_accept_count))
		c2::util::crash_assert();

	if(false == on_load_config(&json_parser))
		c2::util::crash_assert();



	return;
}

bool OuterServer::on_load_config(c2::util::JsonParser* parser)
{
	return true;
}




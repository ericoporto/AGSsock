/*************************************************************
 * Socket interface -- See header file for more information. *
 *************************************************************/

#include <cstring>

#include "Pool.h"
#include "Socket.h"

namespace AGSSock {

using namespace AGSSockAPI;

using std::string;

//------------------------------------------------------------------------------

Pool *pool;

void Initialize()
{
	pool = new Pool();
}

void Terminate()
{
	// We assume that all managed objects will be disposed of at this point.
	// That means the pool is or soon will be empty thus the read loop stops.
	// Deleting the pool gives it two seconds to do it nicely or else just
	// kills it.
	
	delete pool;
	pool = nullptr;
}

//==============================================================================

int AGSSocket::Dispose(const char *ptr, bool force)
{
	Socket *sock = (Socket *) ptr;
	
	if (sock->id != SOCKET_ERROR)
	{
		// Invalidate socket, forced close.
		pool->remove(sock);
		closesocket(sock->id);
		sock->id = SOCKET_ERROR;
	}
	
	if (sock->local != nullptr)
		AGS_RELEASE(sock->local);
	
	if (sock->remote != nullptr)
		AGS_RELEASE(sock->remote);
		
	delete sock;
	return 1;
}

//------------------------------------------------------------------------------

#pragma pack(push, 1)
	struct AGSSocketSerial
	{
		long domain, type, protocol;
		long error;
		int local, remote;
	};
#pragma pack(pop)

//------------------------------------------------------------------------------
// Note: Sockets will not survive serialization, they are but a distant memory.
// We do store the address info so a developer could potentially resuscitate
// them.

int AGSSocket::Serialize(const char *ptr, char *buffer, int length)
{
	Socket *sock = (Socket *) ptr;
	AGSSocketSerial serial =
	{
		sock->domain,
		sock->type,
		sock->protocol,
		sock->error,
		AGS_TO_KEY(sock->local),
		AGS_TO_KEY(sock->remote)
	};
	
	int size = MIN(length, sizeof (AGSSocketSerial));
	memcpy(buffer, &serial, size);
	return sock->tag.copy(buffer + size, length - size) + size;
}

//------------------------------------------------------------------------------
// Note: if unserialization happens in the wrong order we get a potential
// memory leak: if the addresses are unserialized after the socket the socket
// will get null references to them while the adresses are still in the pool 
// and are thus never released.
// This needs to be tested someday eventhough saving sockets is generally a
// bad idea!!!

void AGSSocket::Unserialize(int key, const char *buffer, int length)
{
	AGSSocketSerial serial;
	int size = MIN(length, sizeof (AGSSocketSerial));
	memcpy(&serial, buffer, size);
	
	Socket *sock = new Socket;
	sock->id = INVALID_SOCKET;
	sock->domain = serial.domain;
	sock->type = serial.type;
	sock->protocol = serial.protocol;
	sock->error = serial.error;
	sock->local = AGS_FROM_KEY(SockAddr, serial.local);
	sock->remote = AGS_FROM_KEY(SockAddr, serial.remote);
	if (length - size > 0)
		sock->tag = string(buffer + size, length - size);
	
	AGS_RESTORE(Socket, sock, key);
}

//==============================================================================

Socket *Socket_Create(long domain, long type, long protocol)
{
	Socket *sock = new Socket;
	AGS_OBJECT(Socket, sock);
	sock->id = socket(domain, type, protocol);
	sock->error = GET_ERROR();
	
	// The entire plugin is nonblocking except for:
	//     1. connections in sync mode (async = false)
	//     2. address lookups
	setblocking(sock->id, false);
	
	sock->domain = domain;
	sock->type = type;
	sock->protocol = protocol;
	
	sock->local = nullptr;
	sock->remote = nullptr;
	
	return sock;
}

//------------------------------------------------------------------------------

Socket *Socket_CreateUDP()
{
	return Socket_Create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

//------------------------------------------------------------------------------

Socket *Socket_CreateTCP()
{
	return Socket_Create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

//------------------------------------------------------------------------------

Socket *Socket_CreateUDPv6()
{
	return Socket_Create(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
}

//------------------------------------------------------------------------------

Socket *Socket_CreateTCPv6()
{
	return Socket_Create(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
}

//==============================================================================

long Socket_get_Valid(Socket *sock)
{
	return (sock->id != INVALID_SOCKET ? 1 : 0);
}

//------------------------------------------------------------------------------

const char *Socket_get_Tag(Socket *sock)
{
	return AGS_STRING(sock->tag.c_str());
}

//------------------------------------------------------------------------------

void Socket_set_Tag(Socket *sock, const char *str)
{
	sock->tag = str;
}

//------------------------------------------------------------------------------

inline void Socket_update_Local(Socket *sock)
{
	int addrlen = sizeof (SockAddr);
	getsockname(sock->id, (sockaddr *) sock->local, &addrlen);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

SockAddr *Socket_get_Local(Socket *sock)
{
	if (sock->local == nullptr)
	{
		sock->local = SockAddr_Create(sock->type);
		AGS_HOLD(sock->local);
		
		Socket_update_Local(sock);
		sock->error = GET_ERROR();
	}
	return sock->local;
}

//------------------------------------------------------------------------------

inline void Socket_update_Remote(Socket *sock)
{
	int addrlen = sizeof (SockAddr);
	getpeername(sock->id, (sockaddr *) sock->remote, &addrlen);
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

SockAddr *Socket_get_Remote(Socket *sock)
{
	if (sock->remote == nullptr)
	{
		sock->remote = SockAddr_Create(sock->type);
		AGS_HOLD(sock->remote);
		
		Socket_update_Remote(sock);
		sock->error = GET_ERROR();
	}
	return sock->remote;
}

//------------------------------------------------------------------------------

const char *Socket_ErrorString(Socket *sock)
{
	#ifdef _WIN32
		LPSTR msg = nullptr;
		FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		              0, sock->error, 0, (LPSTR)&msg, 0, 0);
		const char *str = AGS_STRING(msg);
		LocalFree(msg);
		return str;
	#else
		return AGS_STRING(strerror(sock->error));
	#endif
}

//==============================================================================

long Socket_Bind(Socket *sock, const SockAddr *addr)
{
	long ret = (bind(sock->id, (sockaddr *) addr, sizeof (SockAddr)) == SOCKET_ERROR ? 0 : 1);
	sock->error = GET_ERROR();
	if (sock->local != nullptr)
		Socket_update_Local(sock);
	return ret;
}

//------------------------------------------------------------------------------

long Socket_Listen(Socket *sock, long backlog)
{
	if (backlog < 0)
		backlog = SOMAXCONN;
	long ret = (listen(sock->id, backlog) == SOCKET_ERROR ? 0 : 1);
	sock->error = GET_ERROR();
	return ret;
}

//------------------------------------------------------------------------------
// This will also work for UDP since Berkeley sockets fake a connection for UDP
// by binding a remote address to the socket. We will complete this illusion by
// adding the socket to the pool.

long Socket_Connect(Socket *sock, const SockAddr *addr, long async)
{
	int ret;
	
	if (!async) // Sync mode: do a blocking connect
	{
		setblocking(sock->id, true);
		ret = connect(sock->id, (sockaddr *) addr, sizeof (SockAddr));
		setblocking(sock->id, false);
	}
	else
		ret = connect(sock->id, (sockaddr *) addr, sizeof (SockAddr));
	
	// In async mode: returning false but with error == 0 is: try again
	sock->error = GET_ERROR();
	#ifdef _WIN32
		// Note: rumours are that timeouts are reported incorrectly: TEST THIS
		if (sock->error == WSAEALREADY
		|| sock->error == WSAEINVAL || sock->error == WSAEWOULDBLOCK)
			sock->error = 0;
	#else
		if (sock->error == EINPROGRESS || sock->error == EALREADY)
			sock->error = 0;
	#endif
	
	if (ret != SOCKET_ERROR)
	{
		if (sock->remote != nullptr)
			Socket_update_Remote(sock);
		pool->add(sock);
	}
		
	return (ret == SOCKET_ERROR ? 0 : 1);
}

//------------------------------------------------------------------------------
// Accept is nonblocking:
// If it returns nullptr and the error is also 0: try again!
Socket *Socket_Accept(Socket *sock)
{
	SockAddr addr;
	int addrlen = sizeof (SockAddr);
	
	SOCKET conn = accept(sock->id, (sockaddr *) &addr, &addrlen);
	sock->error = GET_ERROR();
	if (WOULD_BLOCK(sock->error))
		sock->error = 0;

	if (conn == INVALID_SOCKET)
		return nullptr;
	
	Socket *sock2 = new Socket;
	AGS_OBJECT(Socket, sock2);
	
	sock2->id = conn;
	sock2->error = 0;
	
	sock2->domain = sock->domain;
	sock2->type = sock->type;
	sock2->protocol = sock->protocol;
	
	// It might be more efficient to use the local and returned address but I
	// rather let the API re-resolve them when needed (less error prone).
	sock2->local = nullptr;
	sock2->remote = nullptr;
	
	setblocking(sock2->id, false);
	pool->add(sock2);
	
	return sock2;
}

//------------------------------------------------------------------------------

void Socket_Close(Socket *sock)
{
	if (sock->protocol == IPPROTO_TCP)
	{
		// Graceful shutdown, the poolthread will detect if it succeeded.
		shutdown(sock->id, SD_SEND);
		
		// Wait for a response to prevent race conditions
		fd_set read;
		timeval timeout = {0, 500}; // Half a second fudge time
		FD_ZERO(&read);
		
		FD_SET(sock->id, &read);
		if (select(sock->id + 1, &read, nullptr, nullptr, &timeout) > 0)
			return;
			
		// Select failed or timeout: we force close
	}
	
	// Invalidate socket
	closesocket(sock->id);
	sock->id = INVALID_SOCKET;
	sock->error = GET_ERROR();
}

//==============================================================================

// Send is nonblocking:
// If it returns 0 and the error is also 0: try again!

inline long send_impl(Socket *sock, const char *buf, size_t count)
{
	long ret = 0;
	
	while (count > 0)
	{
		ret = send(sock->id, buf, count, 0);
		if (ret == SOCKET_ERROR)
			break;
		buf += ret;
		count -= ret;
	}
	
	sock->error = GET_ERROR();
	if (WOULD_BLOCK(sock->error))
		sock->error = 0;

	return (ret == SOCKET_ERROR ? 0 : 1);
}

long Socket_Send(Socket *sock, const char *str)
{
	return send_impl(sock, str, strlen(str));
}

long Socket_SendData(Socket *sock, const SockData *data)
{
	return send_impl(sock, data->data.data(), data->data.size());
}

//------------------------------------------------------------------------------

inline long sendto_impl(Socket *sock, const SockAddr *addr,
	const char *buf, size_t count)
{
	long ret = 0;
	
	while (count > 0)
	{
		ret = sendto(sock->id, buf, count, 0,
			(sockaddr *) addr, sizeof (SockAddr));
		if (ret == SOCKET_ERROR)
			break;
		buf += ret;
		count -= ret;
	}
	
	sock->error = GET_ERROR();
	if (WOULD_BLOCK(sock->error))
		sock->error = 0;
	
	return (ret == SOCKET_ERROR ? 0 : 1);
}

long Socket_SendTo(Socket *sock, const SockAddr *addr, const char *str)
{
	return sendto_impl(sock, addr, str, strlen(str));
}

long Socket_SendDataTo(Socket *sock, const SockAddr *addr, const SockData *data)
{
	return sendto_impl(sock, addr, data->data.data(), data->data.size());
}

//------------------------------------------------------------------------------

// Receives and removes a chunk of data from a buffer and returns it
// Comes in a AGS String and SockData flavour
template <typename T> inline T *recv_extract(Buffer &buffer, bool stream);

template <> inline const char *recv_extract(Buffer &buffer, bool stream)
{
	// Get a truncated (zero terminated) version of the received data.
	const char *data = AGS_STRING(buffer.front().c_str());

	// If the connection is streaming we clear the buffer till the first
	// zero-character; otherwise we are dealing with packets: we remove the
	// current packet from the buffer.
	if (stream)
		buffer.extract();
	else
		buffer.pop();

	return data;
}

template <> inline SockData *recv_extract(Buffer &buffer, bool stream)
{
	// For SockData output, we don't have to worry about zero-characters,
	// thus we receive everything and then clear the buffer.
	SockData *data = new SockData();
	AGS_OBJECT(SockData, data);
	data->data.swap(buffer.front());
	buffer.pop();
	return data;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

// Returns whether a chunk of data is empty (the empty string) or not
// Comes in a AGS String and SockData flavour
template <typename T> inline bool recv_empty(T *data);

template <> inline bool recv_empty(const char *data)
{
	return data[0] == '\0';
}

template <> inline bool recv_empty(SockData *data)
{
	return data->data.empty();
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

template <typename T> inline T *recv_impl(Socket *sock)
{
	T *data;
	
	{
		Mutex::Lock lock(*pool);
	
		if (sock->incoming.empty())
		{
			// Read buffer is empty: either nothing or an error occurred.
			// In both cases we return null, the error code will tell.
			sock->error = sock->incoming.error;
			
			if (sock->error)
			{
				// Invalidate socket in case of error
				closesocket(sock->id);
				sock->id = INVALID_SOCKET;
				// The read loop itself will remove it from the pool
			}
			
			return nullptr;
		}
		
		data = recv_extract<T>(sock->incoming, sock->protocol == IPPROTO_TCP);
	}
	
	sock->error = 0;

	if (recv_empty(data) && sock->protocol == IPPROTO_TCP)
	{
		// TCP socket was closed, invalidate it.
		closesocket(sock->id);
		sock->id = INVALID_SOCKET;
		// The read loop itself will remove it from the pool
	}
	
	return data;
}

const char *Socket_Recv(Socket *sock)
{
	// An empty string indicates 'end of stream' but an input starting with a
	// zero-character false-triggers this; we still close the socket in this
	// case. NB: the `RecvData` function does not have this limitation. So,
	// in case a protocol may send zero-characters point users to the
	// `RecvData` function. However, most protocols do not.
	return recv_impl<const char>(sock);
}

SockData *Socket_RecvData(Socket *sock)
{
	return recv_impl<SockData>(sock);
}

//------------------------------------------------------------------------------

template <typename T> inline T *recvfrom_return(const char *buf, size_t count);

template <> inline const char *recvfrom_return(const char *buf, size_t count)
{
	return AGS_STRING(buf);
}

template <> inline SockData *recvfrom_return(const char *buf, size_t count)
{
	SockData *data = new SockData();
	AGS_OBJECT(SockData, data);
	data->data = string(buf, count);
	return data;
}

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 

template <typename T> inline T *recvfrom_impl(Socket *sock, SockAddr *addr)
{
	char buffer[65536];
	buffer[sizeof (buffer) - 1] = 0;

	int addrlen = sizeof (SockAddr);
	long ret = recvfrom(sock->id, buffer, sizeof (buffer) - 1, 0,
		(sockaddr *) addr, &addrlen);
	sock->error = GET_ERROR();
	
	if (ret == SOCKET_ERROR)
		return nullptr;
	
	return recvfrom_return<T>(buffer, ret);
}

const char *Socket_RecvFrom(Socket *sock, SockAddr *addr)
{
	return recvfrom_impl<const char>(sock, addr);
}

SockData *Socket_RecvDataFrom(Socket *sock, SockAddr *addr)
{
	return recvfrom_impl<SockData>(sock, addr);
}

//==============================================================================

long Socket_GetOption(Socket *, long level, long option)
{
	return 0; // Todo: implement
}

//------------------------------------------------------------------------------

void Socket_SetOption(Socket *, long level, long option, long value)
{
	// Todo: implement
}

//------------------------------------------------------------------------------

} /* namespace AGSSock */

//..............................................................................

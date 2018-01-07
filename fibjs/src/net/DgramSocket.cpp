/*
 * DgramSocket.cpp
 *
 *  Created on: Jan 1, 2018
 *      Author: lion
 */

#include "object.h"
#include "ifs/dgram.h"
#include "DgramSocket.h"
#include "Buffer.h"
#include <fcntl.h>

namespace fibjs {

DECLARE_MODULE(dgram);

result_t _createSocket(exlib::string type, bool reuseAddr, obj_ptr<DgramSocket_base>& retVal)
{
    int32_t _type;

    if (type == "udp4")
        _type = net_base::_AF_INET;
    else if (type == "udp6")
        _type = net_base::_AF_INET6;
    else
        return CHECK_ERROR(CALL_E_INVALIDARG);

    obj_ptr<DgramSocket> s = new DgramSocket();
    result_t hr = s->create(_type, reuseAddr);
    if (hr < 0)
        return hr;

    retVal = s;
    return 0;
}

result_t dgram_base::createSocket(exlib::string type, obj_ptr<DgramSocket_base>& retVal)
{
    return _createSocket(type, false, retVal);
}

result_t dgram_base::createSocket(exlib::string type, v8::Local<v8::Function> callback,
    obj_ptr<DgramSocket_base>& retVal)
{
    result_t hr = createSocket(type, retVal);
    if (hr < 0)
        return hr;

    v8::Local<v8::Object> r;
    retVal->on("message", callback, r);

    return 0;
}

result_t dgram_base::createSocket(v8::Local<v8::Object> opts, obj_ptr<DgramSocket_base>& retVal)
{
    Isolate* isolate = Isolate::current();
    result_t hr;

    bool reuseAddr = false;
    hr = GetConfigValue(isolate->m_isolate, opts, "reuseAddr", reuseAddr);
    if (hr < 0 && hr != CALL_E_PARAMNOTOPTIONAL)
        return hr;

    exlib::string type;
    hr = GetConfigValue(isolate->m_isolate, opts, "type", type);
    if (hr < 0)
        return hr;

    hr = _createSocket(type, reuseAddr, retVal);
    if (hr < 0)
        return hr;

    int32_t size;

    hr = GetConfigValue(isolate->m_isolate, opts, "recvBufferSize", size);
    if (hr != CALL_E_PARAMNOTOPTIONAL) {
        if (hr < 0)
            return hr;
        hr = retVal->setRecvBufferSize(size);
        if (hr < 0)
            return hr;
    }

    hr = GetConfigValue(isolate->m_isolate, opts, "sendBufferSize", size);
    if (hr != CALL_E_PARAMNOTOPTIONAL) {
        if (hr < 0)
            return hr;
        hr = retVal->setSendBufferSize(size);
        if (hr < 0)
            return hr;
    }

    return 0;
}

#ifdef _WIN32
extern HANDLE s_hIocp;
#endif

result_t dgram_base::createSocket(v8::Local<v8::Object> opts, v8::Local<v8::Function> callback,
    obj_ptr<DgramSocket_base>& retVal)
{
    result_t hr = createSocket(opts, retVal);
    if (hr < 0)
        return hr;

    v8::Local<v8::Object> r;
    retVal->on("message", callback, r);

    return 0;
}

result_t DgramSocket::create(int32_t type, bool reuseAddr)
{
    m_aio.m_family = type;

    if (type == net_base::_AF_INET)
        type = AF_INET;
    else if (type == net_base::_AF_INET6)
        type = AF_INET6;

#ifdef _WIN32
    m_aio.m_fd = WSASocketW(type, SOCK_DGRAM, IPPROTO_IP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (m_aio.m_fd == INVALID_SOCKET)
        return CHECK_ERROR(SocketError());

    CreateIoCompletionPort((HANDLE)m_aio.m_fd, s_hIocp, 0, 0);
#else
    m_aio.m_fd = socket(type, SOCK_DGRAM, 0);
    if (m_aio.m_fd == INVALID_SOCKET)
        return CHECK_ERROR(SocketError());

    fcntl(m_aio.m_fd, F_SETFL, fcntl(m_aio.m_fd, F_GETFL, 0) | O_NONBLOCK);
    fcntl(m_aio.m_fd, F_SETFD, FD_CLOEXEC);
#endif

#ifdef Darwin
    int32_t set_option = 1;
    setsockopt(m_aio.m_fd, SOL_SOCKET, SO_NOSIGPIPE, &set_option,
        sizeof(set_option));
#endif

    if (reuseAddr) {
        int32_t on = 1;
        setsockopt(m_aio.m_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
    }

    return 0;
}

result_t DgramSocket::bind(int32_t port, exlib::string addr, AsyncEvent* ac)
{
    if (ac->isSync())
        return CHECK_ERROR(CALL_E_NOSYNC);
    else if (m_bound)
        return CHECK_ERROR(CALL_E_INVALID_CALL);

    inetAddr addr_info;

    addr_info.init(m_aio.m_family);
    addr_info.setPort(port);
    if (addr_info.addr(addr) < 0)
        return CHECK_ERROR(CALL_E_INVALIDARG);

    if (m_bound)
        return CHECK_ERROR(CALL_E_INVALID_CALL);

    if (m_aio.m_family == net_base::_AF_INET6) {
        int32_t on = 0;
        setsockopt(m_aio.m_fd, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&on, sizeof(on));
    }

    if (::bind(m_aio.m_fd, (struct sockaddr*)&addr_info, addr_info.size()) == SOCKET_ERROR)
        return CHECK_ERROR(SocketError());

    m_bound = true;

    return 0;
}

result_t DgramSocket::bind(v8::Local<v8::Object> opts, AsyncEvent* ac)
{
    if (ac->isSync()) {
        Isolate* isolate = holder();

        result_t hr;

        int32_t port;
        hr = GetConfigValue(isolate->m_isolate, opts, "port", port);
        if (hr < 0)
            return hr;

        exlib::string addr;
        hr = GetConfigValue(isolate->m_isolate, opts, "address", addr);
        if (hr < 0)
            return hr;

        ac->m_ctx.resize(2);
        ac->m_ctx[0] = port;
        ac->m_ctx[1] = addr;
    }

    int32_t port = (int32_t)ac->m_ctx[0];
    exlib::string addr = ac->m_ctx[1].string();

    return bind(port, addr, ac);
}

result_t DgramSocket::close()
{
    class asyncClose : public AsyncState {
    public:
        asyncClose(DgramSocket* pThis)
            : AsyncState(NULL)
            , m_pThis(pThis)
        {
            set(close);
        }

    public:
        static int32_t close(AsyncState* pState, int32_t n)
        {
            asyncClose* pThis = (asyncClose*)pState;

            pThis->set(closed);
            return pThis->m_pThis->m_aio.close(pThis);
        }

        static int32_t closed(AsyncState* pState, int32_t n)
        {
            asyncClose* pThis = (asyncClose*)pState;

            pThis->m_pThis->_emit("close", NULL, 0);
            return pThis->done();
        }

        virtual int32_t error(int32_t v)
        {
            m_pThis->_emit("error", NULL, 0);
            return v;
        }

    private:
        obj_ptr<DgramSocket> m_pThis;
    };

    if (m_closed)
        return CHECK_ERROR(CALL_E_INVALID_CALL);
    m_closed = true;

    (new asyncClose(this))->apost(0);
    return 0;
}

result_t DgramSocket::close(v8::Local<v8::Function> callback)
{
    v8::Local<v8::Object> r;
    on("close", callback, r);
    return close();
}

result_t DgramSocket::getRecvBufferSize(int32_t& retVal)
{
    return 0;
}

result_t DgramSocket::getSendBufferSize(int32_t& retVal)
{
    return 0;
}

result_t DgramSocket::setRecvBufferSize(int32_t size)
{
    return 0;
}

result_t DgramSocket::setSendBufferSize(int32_t size)
{
    return 0;
}

result_t DgramSocket::ref(obj_ptr<DgramSocket_base>& retVal)
{
    return 0;
}

result_t DgramSocket::unref(obj_ptr<DgramSocket_base>& retVal)
{
    return 0;
}
}
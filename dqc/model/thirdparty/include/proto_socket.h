#pragma once
#include "proto_types.h"
#include "socket_address.h"
namespace dqc{
class Socket{
public:
    virtual ~Socket(){}
    virtual int Bind(const char *ip,uint16_t port){ return 0;}
    virtual int SendTo(const char*buf,size_t size,SocketAddress &dst){
        return 0;
    }
    virtual int RecvFrom(char*buf,size_t size,SocketAddress &peer){
        return 0;
    }
};
class UdpSocket:public Socket{
public:
    UdpSocket(){}
    ~UdpSocket();
    int Bind(const char *ip,uint16_t port) override;
    int SendTo(const char*buf,size_t size,SocketAddress &dst) override;
    int RecvFrom(char*buf,size_t size,SocketAddress &src) override;
private:
    bool sock_created_{false};
    SocketAddress local_;
    su_socket fd_;
};
}

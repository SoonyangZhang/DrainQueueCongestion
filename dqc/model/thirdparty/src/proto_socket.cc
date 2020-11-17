#include "proto_socket.h"
namespace dqc{
UdpSocket::~UdpSocket(){
    if(sock_created_){
        su_socket_destroy(fd_);
    }
}
int UdpSocket::Bind(const char *ip,uint16_t port){
    if(su_udp_create(ip,port,&fd_)!=0){
        return -1;
    }
    sock_created_=true;
    local_=SocketAddress(ip,port);
    su_socket_noblocking(fd_);
    return 0;
}
int UdpSocket::SendTo(const char*buf,size_t size,SocketAddress &dst){
    su_addr peer;
    dst.ToSockAddr(&peer);
    return su_udp_send(fd_,&peer,(void*)buf,uint32_t(size));
}
int UdpSocket::RecvFrom(char*buf,size_t size,SocketAddress &peer){
    int ret=0;
    su_addr dst;
    ret=su_udp_recv(fd_,&dst,(void*)buf,(uint32_t)size,0);
    if(ret>=0){
        char ip[40];
        uint16_t port;
        su_addr_to_iport(&dst,ip,40,&port);
        std::string host(ip,strlen(ip));
        peer=SocketAddress(host,port);
    }
    return ret;
}
}

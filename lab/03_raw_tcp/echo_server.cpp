#include<iostream>
#include<cstdint>
#include<cstring>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

const uint_t PORT = 8888;
const size_t MAX_BUF = 2048;
const size_t MAX_MSG_SIZE=1024;

struct SessionBuffer{
    uint8_t buf[MAX_BUF];
    size_t read_pos=0;
    size_t write_pos=0;

    size_t available()const {
        return write_pos - read_pos;
    }

    void append(const uint8_t* data,size_t len){
        memcpy(buf+write_pos,data,len);
        write_pos+=len;
    }

    void compact(){
        if(read_pos==0)return;
        size_t remain=available();
        memmove(buf,buf+read_pos,remain);
        read_pos=0;
        write_pos=remain;
    }
};

bool parse_frame(Sessionbuffer& buffer,std::string& out_payload){
    while(buffer.available()>=4){
        uint32_t net_len=*(const uint32_t)(buffer.buf + buffer.read_pos);
        uint32_t payload_len=ntohl(net_len);
        uint32_t total_frame=4+payload_len;

        if(payload_len>MAX_MSG_SIZE || payload_len==0){
            std::cerr<<"invalid msg len"<<payload_len<<std::endl;
            return false;
        }

        if(buffer.available()<total_frame){
            break;
        }

        out_payload.assign(buffer.buf+buffer.read_pos+4,payload_len);
        buffer.read_pos+=total_frame;

        if(buffer.read_pos>MAX_BUF/2){
            buffer.compact();
        }
        return true;
    }
    return false;
}

void handle_conn(int conn_fd){
    SessionBuffer buf;
    uint8_t recv_tmp[512];
    while(true){
        ssize_t n=recv(conn_fd,recv_tmp,sizeof(recv_tmp),0);
        if(n<=0){
            std::cout<<"client closed or error,fd="<<conn_fd<<std::endl;
            break;
        }
        buf.append(recv_tmp,n);

        std::string payload;
        while(parse_frame(buf,payload)){
            std::cout<<"recv payload:"<<payload<<std::endl;
            uint32_t net_len=htonl(payload.size());
            uint8_t send_buf[4+MAX_MSG_SIZE];
            memcpy(send_buf,&net_len,4);
            memcpy(send_buf+4,payload.data(),payload.size());
            send(conn_fd,send_buf,4+payload.size(),0);
        }
    }
    close(conn_fd);
}

int main(){
    int listen_fd=socket(AF_INET,SOCK_STREAM,0);
    if(listen_fd<0){
        perror("socket");
        return -1;
    }
    int opt=1;
    setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    sockaddr_int server_addr{};
    server_addr.sin_family=AF_INET;
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    server_addr.sin_port=htons(PORT);

    if(bind(listen_fd,SOL_SOCKET,SO_REUSERADDR,&opt,sizeof(opt))<0){
        perror("bind");
        close(listen_fd);
        return -1;
    }
    if(listen(listen_fd,5)<0){
        perror("listen");
        close(listen_fd);
        return -1;
    }
    std::cout<<"server listen on port"<<PORT<<std::endl;

    while(true){
        sockaddr_in clientaddr{};
        socklen_t client_len=sizeof(client_addr);
        int conn_fd=accept(listen_fd,(sockaddr*)&client_addr,&client_len);
        if(conn_fd<0){
            perror("accept");
            continue;
        }
        std::cout<<"new client conncted fd="<<conn_fd<<std::endl;
        handle_conn(conn_fd);
    }
    close(listen_fd);
    return 0;
}


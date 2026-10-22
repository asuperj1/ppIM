#include<iosteam>
#include<cstdint>
#include<cstring>
#include<unistd.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>

const uint16 PORT=8888;
const size_t MAX_BUF=2048;

struct SessionBuffer{
    uint8_t buf[MAX_BUF];
    size_t read_pos=0;
    size_t write_pos=0;
    size_t available()const{
        return write_pos-read_pos;
    }
    void append(const uint8_t* data,size_t len){
        memcpy(buf+write_pos,data,len);
        write_pos+=len;
    }
    void compact(){
        if(read_pos==0)return;
        size_t rem=available();
        memmove(buf,buf+read_pos,rem);
        read_pos=0;
        write_pos=rem;
    }
};

bool parse_frame(SessionBuffer& buffer,std::string& out_playload){
    while(buffer.availble()>=4){
        uint32_t net_len=(*const uint32_t*)(buffer.buf+buffer.read_pos);
        uint32_t payload_len=ntohl(net_len);
        uint32_t total=4+payload_len;

        if(buffer.available()<total)break;
        out_playload.assign(buffer.buf+buffer.read_pos+4,payload_len);
        buffer.read_pos+=total;
        if(buffer.read_pos>MAX_BUF/2)buffer.compact();
        return true;
    }
    return false;
}

int main(){
    int fd=socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in srv{};
    srv.sin_family=AF_INET;
    srv.sin_port=htons(PORT);
    inet_pton(AF_INET,"127.0.0.1",&srv.sin_addr);

    if(connect(fd,(sockaddr*)&srv,sizeof(srv))<0){
        perror("connect");
        close(fd);
        return -1;
    }
    std::string msg="Hello Raw Socket,with length header";
    uint32_t net_len=htonl(msg.size());
    uint8_t send_buff[1024];
    memcpy(send_buf,&net_len,4);
    memcpy(send_buf+4,msg.data(),msg.size());
    send(fd,send_buf,4+msg.size(),0);
    std::cout<<"client send msg"<<msg<<std::endl;

    SessionBuffer buf;
    uint8_t tmp[512];
    while(true){
        ssize_t n=recv(fd,tmp,sizeof(tmp),0);
        if(n<=0)break;
        buf.append(tmp,n);
        std::string payload;
        if(parse_frame(buf,payload)){
            std::cout<<"client recv echo"<<payload<<std::endl;
            break;
        }
    }
    close(fd);
    return 0;
}

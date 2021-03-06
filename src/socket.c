#include "base.h"
#include "socket.h"
#include "client.h"

int max_connections = 0;

void close_fd(int fd)
{
    if (fd)
    {
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
    }
}

int load_wsa()
{
#ifdef _WIN32
    WSADATA wsData = {0}; 
    if(0 != WSAStartup(0x202, &wsData))
    {    
        DEBUG("WSAStartup  fail");
        WSACleanup();
        return ERROR;
    }    
#endif
    return SUCCESS;
}

void unload_wsa()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

unsigned char read_msg_syn(unsigned char* buf) 
{
    return *(unsigned char*)&buf[DATA_SYN_OFFSET];
}

unsigned short read_msg_order(unsigned char * buf) 
{
    return *(unsigned short *)&buf[DATA_ORDER_OFFSET];
}

int read_msg_size(unsigned char * buf) 
{
    return *(int*)&buf[DATA_LEN_OFFSET];
}


void set_request_head(char *buf, char encrypt_flag, short cmd, int data_size)
{
    if(!buf)
        return;
    req_head *head = (req_head *)buf;
    head->syn = 0xff;
    head->encrypt_flag = encrypt_flag;
    head->cmd = cmd; 
    head->data_size = data_size;
}

/* 发送请求 */
int send_request(struct client *cli)
{
    int ret;
    if(!cli || !cli->fd)
        return ERROR;

    ret = send_msg(cli->fd, cli->send_head, HEAD_LEN);
    if(cli->send_buf && cli->send_size)
	{
        ret = send_msg(cli->fd, cli->send_buf, cli->send_size);
		free(cli->send_buf);
		cli->send_buf = NULL;
	}
    return ret; 
}


/* 发送数据 */
int send_msg(const int fd, const char *buf, const int len)
{
    const char *tmp = buf;
    int cnt = 0;
    int send_cnt = 0;
    while (send_cnt != len)
    {   
        cnt = send(fd, tmp + send_cnt, len - send_cnt, 0); 
        if (cnt < 0)
        {   
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return ERROR;
        }   
        send_cnt += cnt;
    }   
    return SUCCESS;
}

/* 接收数据 */
int recv_msg(const int fd, char *buf, const int len)
{
    char *tmp = buf;
    int cnt = 0;
    int read_cnt = 0;
    while (read_cnt != len)
    {
        cnt = recv(fd, tmp + read_cnt, len - read_cnt, 0);
        if (cnt == 0)
        {
            return ERROR;
        }
        else
        {
            if (cnt < 0)
            {
                if (errno == EINTR || errno == EAGAIN)
                {
                    continue;
                }
                return ERROR;
            }
        }
        read_cnt += cnt;
    }
    return SUCCESS;
}
int frame_count = 0;

void h264_send_data(char *data, int len, int fd) 
{
    int pending = len; 
    int dataLen = 0; 
    char *ptr = data;
    char buf[DATA_SIZE + 8] = {0}; 
    buf[0] = 0xff;
    buf[1] = 0xff;
    *(unsigned short *)(&buf[2]) = frame_count++;
    *(unsigned int *)(&buf[4]) = len; 
    int first = 1; 
    while(pending > 0) 
    {    
        dataLen = min(pending, DATA_SIZE);

        if(first)  //+8个字节头 1 flag 4 datalen
        {    
            memcpy(buf + 8, ptr, dataLen);
            ptr += dataLen;
            send(fd, buf,  dataLen + 8, 0);
            first = 0; 
        }    
        else 
        {    
            memcpy(buf, ptr, dataLen);
            ptr += dataLen;
            send(fd, buf,  dataLen, 0);
        }    
        pending -= dataLen;
    }    
}



int create_udp_client(const char *ip, const int port)
{
    int fd = -1; 
    struct sockaddr_in send_addr, recv_addr;
    int sock_opt = 0;

    fd = socket(AF_INET, SOCK_DGRAM, 0); 
    if(INVALID_SOCKET == fd) 
    {   
        DEBUG("unable to create udp socket");
        return -1; 
    }

    memset(&send_addr, 0, sizeof(struct sockaddr_in));
    memset(&recv_addr, 0, sizeof(struct sockaddr_in));

    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(port);
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if(NULL != ip)
    {
        send_addr.sin_addr.s_addr = inet_addr(ip);
        recv_addr.sin_port = htons(0);

        sock_opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&sock_opt, sizeof(sock_opt)) < 0)
        {
            DEBUG("setsocksock_sock_sock_opt SO_REUSEADDR");
        }

        sock_opt = 521 * 1024; //设置为512K
        if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&sock_opt, sizeof(sock_opt)) == -1)
        {
            DEBUG("IP_MULTICAST_LOOP set fail!");
        }

        sock_opt = 512 * 1024; //设置为512K
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&sock_opt, sizeof(sock_opt)) == -1)
        {
            DEBUG("IP_MULTICAST_LOOP set fail!");
        }
    #ifdef _WIN32
        sock_opt = 1;
        if (ioctlsocket(fd, FIONBIO, (u_long *)&sock_opt) == SOCKET_ERROR)
        {
            DEBUG("fcntl F_SETFL fail");
        }
    #endif

        /* 绑定自己的端口和IP信息到socket上 */
        if(bind(fd, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0)
        {
            DEBUG("bind port %d error %s", port, strerror(errno));
            goto run_out;
        }

        if(connect(fd, (struct sockaddr *)&send_addr, sizeof(send_addr)) < 0)
        {
            DEBUG("connect connfd : %d ip %s port %d error", fd, ip, port);
            goto run_out;
        }
    }
    return fd;
run_out:
    close_fd(fd);
    return -1;
}



int create_udp_server(const char *host, const int port, struct sockaddr_in *recv_addr)
{
    int fd = -1;
    int sock_opt = 0;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(INVALID_SOCKET == fd)
    {
        DEBUG("unable to create udp socket");
        return -1;
    }
    memset(recv_addr, 0, sizeof(struct sockaddr_in));
    recv_addr->sin_family = AF_INET;
	if(host)
		recv_addr->sin_addr.s_addr = inet_addr(host);
	else
    	recv_addr->sin_addr.s_addr = htonl(INADDR_ANY);
    recv_addr->sin_port = htons(port);
    sock_opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&sock_opt, sizeof(sock_opt)) < 0)
    {
        DEBUG("setsocksock_sock_sock_opt SO_REUSEADDR");
    }
    sock_opt = 521 * 1024; //设置为512K
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char *)&sock_opt, sizeof(sock_opt)) == -1)
    {
        DEBUG("IP_MULTICAST_LOOP set fail!");
    }
   
    sock_opt = 512 * 1024; //设置为512K
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char *)&sock_opt, sizeof(sock_opt)) == -1)
    {
        DEBUG("IP_MULTICAST_LOOP set fail!");
    }

#ifdef _WIN32
    sock_opt = 1;
    if (ioctlsocket(fd, FIONBIO, (u_long *)&sock_opt) == SOCKET_ERROR)
    {
        DEBUG("fcntl F_SETFL fail");
    }
#else
    if (fcntl(fd, F_SETFD, 1) == -1)
    {
        DEBUG("can't set close-on-exec on server socket!");
        goto run_out;
    }
    if ((sock_opt = fcntl(fd, F_GETFL, 0)) < -1)
    {
        DEBUG("can't set close-on-exec on server socket!");
        goto run_out;
    }
    if (fcntl(fd, F_SETFL, sock_opt | O_NONBLOCK) == -1)
    {
        DEBUG("fcntl: unable to set server socket to nonblocking");
        goto run_out;
    }
#endif

    if(bind(fd, (struct sockaddr *)recv_addr, sizeof(struct sockaddr_in)) < 0)
    {
        DEBUG("bind sockfd %d local port 0 error", fd);
        goto run_out;
    }
    return fd;
run_out:
    close_fd(fd);
    return -1;
}


int create_boardcast_recv(const char *host, const char *ip, const int port)
{
    int fd = -1;        
    struct sockaddr_in recv_addr;
    int sock_opt = 0;

    /* 广播 */
    if(ip == NULL)
    {   
        fd = create_udp_server(host, port, &recv_addr);
        sock_opt = 1;
        if(setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (char *)&sock_opt, sizeof(sock_opt)) < 0)
        {   
            DEBUG("setsocksock_sock_sock_opt SO_BROADCAST");
        }   
    }   
    else            //组播
    {   
        fd = create_udp_server(host, port, &recv_addr);
            
        /* 加入组播 */ 
        struct ip_mreq mreq;
        /* 设置要加入组播的地址 */
        memset(&mreq, 0, sizeof (struct ip_mreq));
        mreq.imr_multiaddr.s_addr = inet_addr(ip);
        /* 设置发送组播消息的源主机的地址信息 */
        mreq.imr_interface.s_addr = htonl (INADDR_ANY);
                
        /* 把本机加入组播地址，即本机网卡作为组播成员，只有加入组才能收到组播消息 */
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP , (char *)&mreq,sizeof (struct ip_mreq)) == -1) 
        {   
            DEBUG ("setsockopt IP_ADD_MEMBERSHIP error: %s", strerror(errno));
            goto run_out;
        }   
    }   
    return fd; 

run_out:
    close_fd(fd);
    return -1; 
}



int create_tcp()
{  
    int fd = -1, sock_opt;
    int keepAlive = 1;    //heart echo open
    int keepIdle = 15;    //if no data come in or out in 15 seconds,send tcp probe, not send ping
    int keepInterval = 3; //3seconds inteval
    int keepCount = 5;    //retry count

    fd = socket(AF_INET, SOCK_STREAM, 0); 
    if (INVALID_SOCKET == fd) 
    {   
        DEBUG("unable to create tcp socket");
        goto run_out;
    }   
   
    sock_opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt)) != 0)
        goto run_out;
#ifdef _WIN32

#else
    if (fcntl(fd, F_SETFD, 1) == -1) 
    {   
        DEBUG("can't set close-on-exec on server socket!");
        goto run_out;
    }   
    if ((sock_opt = fcntl(fd, F_GETFL, 0)) < -1) 
    {   
        DEBUG("can't set close-on-exec on server socket!");
        goto run_out;
    }   
    if (fcntl(fd, F_SETFL, sock_opt | O_NONBLOCK) == -1) 
    {   
        DEBUG("fcntl: unable to set server socket to nonblocking");
        goto run_out;
    }   
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive)) != 0)
        goto run_out;
    if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void *)&keepIdle, sizeof(keepIdle)) != 0)
        goto run_out;
    if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void *)&keepInterval, sizeof(keepInterval)) != 0)
        goto run_out;
#endif
    return fd; 
run_out:
    close_fd(fd);
    return -1; 
}

int bind_socket(const char * host, int fd, int port)
{   
    DEBUG("tcp bind port %d", port);
    
    struct sockaddr_in server_sockaddr;
    memset(&server_sockaddr, 0, sizeof server_sockaddr);
    
    server_sockaddr.sin_family = AF_INET;
	
	if(host)
    	server_sockaddr.sin_addr.s_addr = inet_addr(host);
	else
    	server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_sockaddr.sin_port = htons(port);
    
    /* internet family-specific code encapsulated in bind_server()  */
    if (bind(fd, (struct sockaddr *)&server_sockaddr,
             sizeof(server_sockaddr)) == -1)
    {   
        DEBUG("unable to bind");
        goto run_out;
    }
    
    if(max_connections < 1)
    {
#ifndef _WIN32 
        struct rlimit rl;
        
        /* has not been set explicitly */
        int c = getrlimit(RLIMIT_NOFILE, &rl);
        if (c < 0)
        {   
            DEBUG("getrlimit");
            goto run_out;
        }   
        max_connections = rl.rlim_cur;
#endif  
        if(max_connections < 1)
        {   
            max_connections = 1024;
        }
    }
    
    /* listen: large number just in case your kernel is nicely tweaked */
    if (listen(fd, max_connections) == -1)
    {   
        DEBUG("listen max_connect: %d error", max_connections);
        goto run_out;
    }
    
    return SUCCESS;
run_out:
    close_fd(fd);
    return ERROR;
}

/* 连接服务器 */
int connect_server(int fd, const char *ip, int port)
{
    int count = 10;
    struct sockaddr_in client_sockaddr;
    memset(&client_sockaddr, 0, sizeof client_sockaddr);

    client_sockaddr.sin_family = AF_INET;
    client_sockaddr.sin_addr.s_addr = inet_addr(ip);
    client_sockaddr.sin_port = htons(port);

    while (connect(fd, (struct sockaddr *)&client_sockaddr, sizeof(client_sockaddr)) != 0)
    {
        DEBUG("connection failed reconnection after 1 seconds");
        sleep(1);
        if (!count--)
        {
            return ERROR;
        }
    }
    return SUCCESS;
}


int create_tcp_server(const char *host, const int port)
{
	int sockfd = -1;
	sockfd = create_tcp();	
	if(INVALID_SOCKET == sockfd)
	{
		return INVALID_SOCKET;
	}

	if(bind_socket(host, sockfd, port) == INVALID_SOCKET)
	{
		close_fd(sockfd);
		return INVALID_SOCKET;
	}
	return sockfd;
}


int create_tcp_client(const char *ip, const int port)
{
	int sockfd = -1;
	sockfd = create_tcp();	
	if(INVALID_SOCKET == sockfd)
	{
		return INVALID_SOCKET;
	}

	if(connect_server(sockfd, ip,  port) == ERROR)
	{
		close_fd(sockfd);
		return INVALID_SOCKET;
	}
	return sockfd;
}




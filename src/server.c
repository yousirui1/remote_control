#include "base.h"
#include "client.h"
#include "rtsp.h"
#include "server.h"

static int server_s = -1;
static pthread_t pthread_sdl, pthread_tcp, pthread_udp;
int total_connections = 0;
struct client **clients = NULL;
struct client *control_cli;

extern struct rtsp_cli *rtsp;

static int send_done(struct client *cli);
static int send_control(struct client *cli);
static int send_play(struct client *cli);

extern int control_flag;

void *thread_ffmpeg_video_decode(void *param);
//pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

extern QUEUE *vids_queue;

void exit_server()
{
	void *tret = NULL;

    pthread_join(pthread_sdl, &tret); //等待线程同步
    DEBUG("pthread_exit %d display", (int)tret);
    pthread_join(pthread_tcp, &tret);  //等待线程同步
    DEBUG("pthread_exit %d tcp", (int)tret);
}

static int close_client(struct client *cli)
{
	if(!cli)
		return ERROR;
	
	if(cli->send_buf)
		free(cli->send_buf);

	if(cli->recv_buf)
		free(cli->recv_buf);
	
	cli->send_buf = NULL;

	cli->recv_buf = NULL;

	if(cli->chn >= 0)
		close_rtsp_chn(cli->chn);

	close_fd(cli->fd);

	cli->chn = -1;
	cli->fd = -1;

	free(cli);
	return SUCCESS;
}

static int free_clients()
{
	int i;
	for(i = 1; i < max_connections; i++)
	{
		if(clients[i])
		{
			close_client(clients[i]);
			clients[i] = NULL;
		}
	}
	free(clients);
}

static int convert_model(int chn)
{
	int i;
	if(!rtsp[chn].cli || !rtsp[chn].is_running)
		return SUCCESS;


	for(i = 0; i < max_conn; i++)
	{
		if(!rtsp[i].cli)
			continue;

		if(!control_flag)  //PLAY -> CONTROL
		{
			if(i == chn)
				rtsp[i].cli->status = CONTROL;
			else
				rtsp[i].cli->status = READY;
		}
		else				//CONTROL - > PLAY
		{
			rtsp[i].cli->status = PLAY;	
		}
		send_done(rtsp[i].cli);	
	}
	control_flag = !control_flag;

	control_cli = NULL;
	clear_texture();
	return SUCCESS;
}

static int recv_done(struct client *cli)
{
	int ret;
	switch(cli->status)
	{
		case PLAY:
			ret = send_play(cli);
			break;
		case CONTROL:
			ret = send_control(cli);
			break;
		case READY:
			ret = SUCCESS;
			break;
		default:
			ret = ERROR;	
			break;
	}
	return ret;
}

static int send_done(struct client *cli)
{
	DEBUG("cli->status %d chn %d is_running %d", cli->status, cli->chn, rtsp[cli->chn].is_running);
	if(rtsp[cli->chn].is_running)
	{
		stop_rtsp_chn(cli->chn);
	}
	cli->send_size = 0;
	set_request_head(cli->send_head, 0, DONE_MSG, cli->send_size);
    return send_request(cli);
}

static int recv_control(struct client *cli)
{
	struct request *req = (struct request *)cli->recv_buf;
    if(req->code == 200)
    {   
		if(rtsp[cli->chn].is_running)
		{
			return ERROR;
		}
		control_cli = cli;
		return start_rtsp_chn(cli->chn, CONTROL);		
    }   
    else
    {   
        DEBUG("control error code %d msg %s", req->code, req->msg);
        return ERROR;
    }
}

static int send_control(struct client *cli)
{
	int ret;
  	cli->send_size = sizeof(rtp_format) + 1;
    cli->send_buf = malloc(cli->send_size);
    if(!cli->send_buf)
        return ERROR;

    rtp_format *fmt = (rtp_format *)cli->send_buf;	

	fmt->video_fmt.width = screen_width;
	fmt->video_fmt.height = screen_height;
	fmt->video_fmt.fps = 16;
	fmt->video_fmt.bps = 400000;

	fmt->model = CONTROL;
	fmt->video_port = rtsp[cli->chn].video_port;
	
	set_request_head(cli->send_head, 0, CONTROL_MSG, cli->send_size);
    return send_request(cli);
}

static int recv_play(struct client *cli)
{   
   struct request *req = (struct request *)cli->recv_buf;
    if(req->code == 200)
    {   
		if(rtsp[cli->chn].is_running)
		{
			return ERROR;
		}
		return start_rtsp_chn(cli->chn, PLAY);		
    }   
    else
    {   
        DEBUG("play error code %d msg %s", req->code, req->msg);
        return ERROR;
    }
}

static int send_play(struct client *cli)
{
	int ret;
  	cli->send_size = sizeof(rtp_format) + 1;
    cli->send_buf = malloc(cli->send_size);
    if(!cli->send_buf)
        return ERROR;

	DEBUG("sizeof(rtp_format) %d", sizeof(rtp_format));
	
    rtp_format *fmt = (rtp_format *)cli->send_buf;	

	fmt->video_fmt.width = vids_width;
	fmt->video_fmt.height = vids_height;
	fmt->video_fmt.fps = 12;
	fmt->video_fmt.bps = 200000;

	fmt->model = PLAY;
	fmt->video_port = rtsp[cli->chn].video_port;
	DEBUG("video_port %d 1111111111111111111", fmt->video_port);
	
	set_request_head(cli->send_head, 0, PLAY_MSG, cli->send_size);
    return send_request(cli);
}

static int send_options(struct client *cli)
{
    int ret;

    cli->send_size = sizeof(struct request) + 1;
    cli->send_buf = (unsigned char *)malloc(cli->send_size);

    if(!cli->send_buf)
        return ERROR;

    struct request *req = (struct request *)cli->send_buf;
    
    cli->status = READY;
    req->code = 200;
    set_request_head(cli->send_head, 0, OPTIONS_MSG, cli->send_size);
    send_request(cli);

	if(control_flag)
		return SUCCESS;
	else
		return send_play(cli);
}

static int recv_options(struct client *cli)
{
	if(cli->status != OPTIONS)
		return ERROR;

	int free_chn = rtspd_get_freechn();
	if(free_chn == -1)
	{
		return ERROR;
	}
	
	DEBUG("free_chn %d", free_chn);
	rtsp[free_chn].cli = cli;	
	cli->chn = free_chn;
	
	//rtp_format *fmt = (rtp_format *)cli->recv_buf;
	//if(fmt)
	//	memcpy(&rtsp[free_chn].video_fmt, fmt, sizeof(rtp_format));
	return send_options(cli);	
}

static int send_login(struct client *cli)
{
	cli->send_size = sizeof(struct request) + 1;
	cli->send_buf = (unsigned char *)malloc(cli->send_size);

	if(!cli->send_buf)
		return ERROR;

	struct request *req = (struct request *)cli->send_buf;
	
	req->code = 200;
	cli->status = OPTIONS;
	set_request_head(cli->send_head, 0, LOGIN_MSG, cli->send_size);
	return send_request(cli);
}

static int recv_login(struct client *cli)
{
   	int server_major = 0, server_minor = 0;
    int client_major = 0, client_minor = 0;
	
	get_version(&server_major, &server_minor);
	sscanf(cli->recv_buf, VERSIONFORMAT, &client_major, &client_minor);
	if(server_major == client_major && server_minor == client_minor)
	{
		return send_login(cli);
	}
	return ERROR;
}

int process_server_msg(struct client *cli)
{
	int ret;
	DEBUG("read_msg_order(cli->recv_head) %d", read_msg_order(cli->recv_head));
	switch(read_msg_order(cli->recv_head))
	{
		case LOGIN_MSG:
			ret = recv_login(cli);
			break;
		case OPTIONS_MSG:
			ret = recv_options(cli);
			break;
		case PLAY_MSG:
			ret = recv_play(cli);
			break;
		case CONTROL_MSG:
			ret = recv_control(cli);
			break;
		case DONE_MSG:
			ret = recv_done(cli);
			break;
		default:
			ret = ERROR;
			break;	
	}
	return ret;
}

static void tcp_loop(int listenfd)
{
    int maxfd = 0, connfd, sockfd;
    int nready, ret, i, maxi = 0; 

    fd_set reset, allset;

    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    
    struct timeval tv;
    tv.tv_sec = 1; 
    tv.tv_usec = 0; 
    
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    FD_SET(pipe_tcp[0], &allset);   

    maxfd = maxfd > listenfd ? maxfd : listenfd;
    maxfd = maxfd > pipe_tcp[0] ? maxfd : pipe_tcp[0];

	struct client pipe_cli = {0};
	clients[0] = &pipe_cli;	
	pipe_cli.fd = pipe_tcp[0];
	struct client *cli = NULL;
	time_t last_time = current_time;

	DEBUG("maxfd %d listenfd %d pipe_tcp[0] %d", maxfd, listenfd, pipe_tcp[0]);
	for(;;)
	{
        tv.tv_sec = 1;
        reset = allset;
        ret = select(maxfd + 1, &reset, NULL, NULL, &tv);
        if(ret == -1)
        {    
             if(errno == EINTR)
                continue; 
            else if(errno != EBADF)
            {   
                DEBUG("select %d %s ", errno, strerror(errno));
                return;
            }
        }
        nready = ret;

	   	(void)time(&current_time);
        if(current_time - last_time >= TIME_OUT)
        {   
            last_time = current_time;
        } 
	
        /* new connect */
        if(FD_ISSET(listenfd, &reset))
        {    
            connfd = accept(listenfd, (struct sockaddr*)&cliaddr, &clilen);
            if(connfd < 0) 
                continue;
            cli = (struct client *)malloc(sizeof(struct client));
            if(!cli)
            {    
                DEBUG("new connect and malloc struct client error :%s", strerror(errno));
                continue;
            }    
            memset(cli, 0, sizeof(struct client));
            cli->fd = connfd;
            cli->recv_size = HEAD_LEN;
			cli->chn = -1;
#ifdef _WIN32
#if 0
			ret = 1;
    		if(ioctlsocket(connfd, FIONBIO, (u_long *)&ret) == SOCKET_ERROR)
    		{   
        		DEBUG("fcntl F_SETFL fail");
    		} 
#endif
            memcpy(cli->ip,inet_ntoa(cliaddr.sin_addr), sizeof(cli->ip));
#else
            ret = fcntl(connfd, F_GETFL, 0);
            if(ret < 0) 
            {    
                DEBUG("fcntl connfd: %d  F_GETFL error :%s", connfd, strerror(errno));
                close_fd(connfd);
                free(cli);
                continue;
            }    

            if(fcntl(connfd, F_SETFL, ret | O_NONBLOCK) < 0) 
            {    
                DEBUG("fcntl connfd: %d F_SETFL O_NONBLOCK error :%s", connfd, strerror(errno));
                close_fd(connfd);
                free(cli);
                continue;
            }    
            /* recode client ip */
            if(inet_ntop(AF_INET, &cliaddr.sin_addr, cli->ip, sizeof(cli->ip)) == NULL)
            {    
                DEBUG("connfd: %d inet_ntop error ",connfd, strerror(errno));
                close_fd(connfd);
                free(cli);
                continue;
            }    
#endif
            FD_SET(connfd, &allset);
            for(i = 1; i < max_connections; i++)
            {
                if(clients[i] == NULL)
				{
                    clients[i] = cli;
                	break;
				}
            }
            total_connections ++;
            if(i >= maxi)
                maxi = i;
            if(connfd > maxfd)
                maxfd = connfd;

            DEBUG("client index: %d total_connections: %d maxi: %d connfd %d ip: %s",i, total_connections, maxi, connfd, cli->ip);
            if(--nready <= 0)
                continue;
        }
    
		for(i = 0; i <= maxi; i++)
		{
            if(clients[i] == NULL || (sockfd = clients[i]->fd) < 0)     
                continue;
            if(FD_ISSET(sockfd, &reset))
            {    
                if(clients[i]->has_read_head == 0)
                {    
                    if((ret = recv(sockfd, (void *)clients[i]->recv_head + clients[i]->pos, 
                                        HEAD_LEN - clients[i]->pos, 0)) <= 0)     
                    {    
                        if(ret < 0) 
                        {    
                            if(errno == EINTR || errno == EAGAIN)
                                continue;
                        }    
                        DEBUG("client close index: %d ip: %s port %d", i,
                                    clients[i]->ip, clients[i]->port);

                        FD_CLR(clients[i]->fd, &allset);
                        close_client(clients[i]);
                        clients[i] = NULL;
                        total_connections--;
                        continue;
                    }    
                    clients[i]->pos += ret; 
                    if(clients[i]->pos != HEAD_LEN)
                        continue;
     
                    if(read_msg_syn(clients[i]->recv_head) != DATA_SYN)
                    {    
                        DEBUG(" %02X client send SYN flag error close client index: %d ip: %s port %d", 
                            read_msg_syn(clients[i]->recv_head), i, clients[i]->ip, 
                            clients[i]->port);
                        FD_CLR(clients[i]->fd, &allset);
                        close_client(clients[i]);
                        clients[i] = NULL;
                        total_connections--;
                        continue;
                    }    
                    clients[i]->has_read_head = 1; 
                    clients[i]->recv_size = read_msg_size(clients[i]->recv_head);
                    clients[i]->pos = 0; 
                    if(clients[i]->recv_size >= 0 && clients[i]->recv_size < CLIENT_BUF)
                    {    
                        clients[i]->recv_buf = (unsigned char*)malloc(clients[i]->recv_size + 1);
                        if(!clients[i]->recv_buf)
                        {    
                            DEBUG("malloc data buf error: %s close client index: %d ip: %s port %d",
                                    strerror(errno), i, clients[i]->ip, clients[i]->port);
                            FD_CLR(clients[i]->fd, &allset);
                            close_client(clients[i]);
                            clients[i] = NULL;
                            total_connections--;
                            continue;
                        }
                    }
                    else
                    {
                        DEBUG("client send size: %d error close client index: %d ip: %s port %d",
                                clients[i]->recv_size, i, clients[i]->ip, clients[i]->port);
                        FD_CLR(clients[i]->fd, &allset);
                        close_client(clients[i]);
                        clients[i] = NULL;
                        total_connections--;
                        continue;
                    }
                }
                if(clients[i]->has_read_head == 1)
                {
                    if(clients[i]->pos < clients[i]->recv_size)
                    {
                        if((ret = recv(sockfd,clients[i]->recv_buf+clients[i]->pos,
								clients[i]->recv_size - clients[i]->pos, 0)) <= 0)
                        {
                            if(ret < 0)
                            {
                                if(errno == EINTR || errno == EAGAIN)
                                    continue;
                                DEBUG("client close index: %d ip: %s port %d",i, clients[i]->ip, clients[i]->port);
                                FD_CLR(clients[i]->fd, &allset);
                                close_client(clients[i]);
                                clients[i] = NULL;
                                total_connections--;
                                continue;
                            }
                        }
                        clients[i]->pos += ret;
                    }
                    if(clients[i]->pos == clients[i]->recv_size)
                    {
						switch(read_msg_order(clients[i]->recv_head))
						{
							case EXIT_PIPE:
								goto run_out;
							case CONVERT_MODE_PIPE:
								if(clients[i]->recv_size)	
									ret = convert_model(*(int *)clients[i]->recv_buf);	
								else
									ret = convert_model(0);										
								break;
							case CLOSE_ALL_CLIENT_PIPE:
							{
								int j;
								for(j = 1; j <= maxi; j++)
								{
            						if(clients[j] == NULL || (clients[j]->fd) < 0)     
										continue;	

									FD_CLR(clients[j]->fd, &allset);
                                	close_client(clients[j]);
                                	clients[j] = NULL;
								}
                                total_connections = 0;
								ret = SUCCESS;
								break;	
							}	
							default:
								ret = process_server_msg(clients[i]);
								break;
						}
												
						if(ret != SUCCESS)
						{
                            DEBUG("process msg error client index: %d ip: %s port %d",
									i, clients[i]->ip, clients[i]->port);
                            FD_CLR(clients[i]->fd, &allset);
                            close_client(clients[i]);
                            clients[i] = NULL;
                            total_connections--;
                            continue;
                        }
                        memset(clients[i]->recv_head, 0, HEAD_LEN);
                        clients[i]->recv_size = HEAD_LEN;
                        clients[i]->pos = 0;
                        if(clients[i]->recv_buf)
                            free(clients[i]->recv_buf);
                        clients[i]->recv_buf = NULL;
                        clients[i]->has_read_head = 0;
                    }
                    if(clients[i]->pos > clients[i]->recv_size)
                    {
                        DEBUG("loss msg data client index: %d ip: %s port %d", i, clients[i]->ip, clients[i]->port);
                        FD_CLR(clients[i]->fd, &allset);
                        close_client(clients[i]);
                        clients[i] = NULL;
                        total_connections--;
                        continue;
                    }
                }
#if RTSP
				if((ret = recv(sockfd, (void *)cli->rtsp_buf, sizeof(cli->rtsp_buf), 0)) <= 0)     
                {    
                    if(ret < 0) 
                    {    
                        if(errno == EINTR || errno == EAGAIN)
                            continue;
                    }    
                    DEBUG("client close index: %d ip: %s port %d", i,
                                clients[i]->ip, clients[i]->port);
                    FD_CLR(clients[i]->fd, &allset);
                    close_client(clients[i]);
                    clients[i] = NULL;
                    total_connections--;
                    continue;
                }    
				cli->recv_size = ret;
				//DEBUG("%s", cli->rtsp_buf);
 				if(rtsp_cmd_match(cli) < 0)
				{
				    FD_CLR(clients[i]->fd, &allset);
                    close_client(clients[i]);
                    clients[i] = NULL;
                    total_connections--;
				}
#endif
                if(--nready <= 0)
                    break;
			}
		}
	}

run_out:
	DEBUG("tcp loop end");
	return ;
}

void destory_thread_tcp()
{
	void *tret;	

	DEBUG("destory server tcp thread");
	pthread_cancel(pthread_tcp);				
    pthread_join(pthread_tcp, &tret);  //等待线程同步

    DEBUG("pthread_exit %d udp", (int)tret);
	free_clients();
	close_fd(server_s);

	pthread_cancel(pthread_udp);				
    pthread_join(pthread_udp, &tret);  //等待线程同步

    DEBUG("pthread_exit %d udp", (int)tret);
}



static void *thread_tcp(void *param)
{
    int ret;
    pthread_attr_t st_attr;
    struct sched_param sched;

	int sockfd = *(int *)param;

    //pthread_detach(pthread_self());
    ret = pthread_attr_init(&st_attr);
    if(ret)
    {   
        DEBUG("thread server tcp attr init warning ");
    }   
    ret = pthread_attr_setschedpolicy(&st_attr, SCHED_FIFO);
    if(ret)
    {   
        DEBUG("thread server tcp set SCHED_FIFO warning");
    }   
    sched.sched_priority = SCHED_PRIORITY_DECODE;
    ret = pthread_attr_setschedparam(&st_attr, &sched);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);     //线程可以被取消掉
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL);//立即退出
    //pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);//立即退出  PTHREAD_CANCEL_DEFERRED 

	tcp_loop(sockfd);
    return (void *)0;
}

static void udp_loop()
{
    int i, ret, nready;
    struct timeval tv;
    fd_set fds;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    int sockfd = -1;
    socklen_t socklen = sizeof (struct sockaddr_in);
    unsigned int total_size = 0;
    unsigned int offset = 0;
    unsigned char *tmp;
    unsigned short count = 0;
	int maxfd = -1;	

    fd_set allset;
    FD_ZERO(&allset);
    for(i = 0; i < max_conn; i++)
    {
		rtsp[i].video_fd = create_udp_server(NULL, rtsp[i].video_port, &(rtsp[i].recv_addr));
        FD_SET(rtsp[i].video_fd, &allset);
        maxfd = maxfd > rtsp[i].video_fd ? maxfd : rtsp[i].video_fd;
    }

	for(;;)
    {
        fds = allset;
        tv.tv_sec = 1;

        ret = select(maxfd + 1, &fds, NULL, NULL, &tv);
        if(ret == 0)
            continue;

        nready = ret;
        for(i = 0; i < max_conn; i++)
        {
            if((sockfd = rtsp[i].video_fd) < 0)
                continue;
            if(FD_ISSET(sockfd, &fds))
            {
                ret = recvfrom(sockfd, (char *)rtsp[i].frame_buf + rtsp[i].frame_pos, MAX_VIDSBUFSIZE, 0,
                    (struct sockaddr*)&(rtsp[i].recv_addr), &socklen);

				DEBUG("recvfrom %d", ret);
				
                tmp = &(rtsp[i].frame_buf[rtsp[i].frame_pos]);
                rtsp[i].frame_pos += ret; 
                if(tmp[0] == 0xff && tmp[1] == 0xff)
                {    
                    rtsp[i].frame_size = *((unsigned int *)&tmp[4]);
                }    
                if(rtsp[i].frame_pos == rtsp[i].frame_size + 8) 
                {    
                    //en_queue(&rtsp[i].video_fmt.vids_queue, rtsp[i].frame_buf + 8,  rtsp[i].frame_pos - 8, 0x0);
					en_queue(&vids_queue[i], rtsp[i].frame_buf + 8,  rtsp[i].frame_pos - 8, 0x0);
                    rtsp[i].frame_pos = 0; 
                    rtsp[i].frame_size = 0; 
                }    
                else if(rtsp[i].frame_pos > rtsp[i].frame_size + 8 || rtsp[i].frame_pos >= MAX_VIDSBUFSIZE)
                {    
                    rtsp[i].frame_pos = 0; 
                    rtsp[i].frame_size = 0; 
                }  
				
                if(ret < 0)
                    continue;
                
                if(--nready <= 0)
                  break;
           }
        }
    }
}

static void *thread_udp(void *param)
{
    int ret;
    pthread_attr_t st_attr;
    struct sched_param sched;

    //pthread_detach(pthread_self());
    ret = pthread_attr_init(&st_attr);
    if(ret)
    {   
        DEBUG("thread server tcp attr init warning ");
    }   
    ret = pthread_attr_setschedpolicy(&st_attr, SCHED_FIFO);
    if(ret)
    {   
        DEBUG("thread server tcp set SCHED_FIFO warning");
    }   
    sched.sched_priority = SCHED_PRIORITY_DECODE;
    ret = pthread_attr_setschedparam(&st_attr, &sched);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);     //线程可以被取消掉
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,NULL);//立即退出
    //pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);//立即退出  PTHREAD_CANCEL_DEFERRED 

	udp_loop();
    return (void *)0;
}


static void usage()
{
    DEBUG("\nYZY REMOTE CONTROL SERVER VERSION :\n"
           "\n"
           );
}

int init_server()
{
	int ret;
	
	usage();

    if(window_size <= 0 || window_size > 5)
    {   
        DEBUG("window size: %d error", window_size);
        return ERROR;
    }   

	ret = init_rtsp_chn();
	if(ret != SUCCESS)
	{
		DEBUG("rtsp chn init error");
		return ERROR;
	}
	DEBUG("rtsp chn SUCCESS");

	server_s = create_tcp_server(NULL, client_port);
	if(server_s == -1)
	{
		free(clients);
		return ERROR;
	}

	DEBUG("create tcp sockfd SUCCESS");
    clients = (struct client **)malloc(sizeof(struct client *) * max_connections);
    if(!clients)
    {   
        DEBUG("clients malloc error %s max_connections %d", strerror(errno), max_connections);
        return ERROR;
    }   
	memset(clients, 0, sizeof(struct client *) * max_connections);
	DEBUG("clients malloc %d SUCCESS", max_connections);
	
	/* socket */	
	ret = pthread_create(&pthread_tcp, NULL, thread_tcp, &server_s);
	if(SUCCESS != ret)
	{
		free(clients);
		close_fd(server_s);
		return ERROR;
	}

	DEBUG("create thread tcp SUCCESS");
	ret = pthread_create(&pthread_udp, NULL, thread_udp, NULL);
	if(SUCCESS != ret)
	{
		free(clients);
		close_fd(server_s);
		return ERROR;
	}
	/* event */	
	ret = pthread_create(&pthread_sdl, NULL, thread_sdl, NULL);
	if(SUCCESS != ret)
	{
		pthread_cancel(pthread_tcp);				
		free(clients);
		close_fd(server_s);
		return ERROR;
	}
	DEBUG("create thread sdl SUCCESS");
	return SUCCESS;	
}


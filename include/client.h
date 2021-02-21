#ifndef __CLIENT_H__
#define __CLIENT_H__

//#include "rbtree.h"

#define IPADDR_LEN 36

struct client
{
    unsigned int status;

    /* red black node */
    //struct rb_node rb_node;

    /* tcp sock fd */
    int fd; 
    char ip[IPADDR_LEN];
	//int ip;
    int port;
        
    /** has read msg head or not ,0 :not 1: yes**/
    int has_read_head;

    unsigned char send_head[HEAD_LEN + 1]; 
    unsigned char recv_head[HEAD_LEN + 1]; 

    unsigned char *send_buf;
    unsigned char *recv_buf;

    unsigned int send_size;
    unsigned int recv_size;

	rtp_format fmt;

	unsigned char rtsp_buf[DATA_SIZE];
	unsigned char sdp_buf[DATA_SIZE];
	unsigned char host_name[128];
	unsigned char file_name[128];
	uint32_t rtsp_cseq;
	int32_t payload_type;
	int32_t session_id;
	uint32_t ssrc;
	uint16_t rtp_port;
	uint16_t rtcp_port;
	uint8_t nalu_buffer[1448];
	int is_tcp;



    /** current data position **/
    int pos;
    /** curreant data size **/
    int data_size;
    /** max alloc size **/
    int max_size;

    time_t last_time;
};



#endif //__CLIENT_H__ 


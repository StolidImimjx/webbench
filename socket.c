#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> /*include AF_INET \htons()...*/
#include <netdb.h>      /*include gethostbyname() ...*/
#include <arpa/inet.h>  /*include inet_addr()\inet_ntop()...*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * 根据 host & cport 连接服务器
 * 成功返回 套接字句柄， 失败返回 -1
 */
int Socket(const char* host, int cport)
{
    int sockfd;
    in_addr_t inaddr;
    struct sockaddr_in addr;
    struct hostent* hp;

    /* 初始化地址结构 */
    memset(&addr, 0, sizeof(addr));

    inaddr = inet_addr(host);
    if(inaddr != INADDR_NONE) //INADDR_NONE: in.h： #define INADDR_NONE ((in_addr_t) 0xffffffff)； //in_addr_t：typedef uint32_t in_addr_t
    {
        memcpy(&addr.sin_addr, &inaddr, sizeof(inaddr));
    }
    else
    {
        hp = gethostbyname(host);
        if(NULL == hp)
            return -1;
        memcpy(&addr.sin_addr, hp->h_addr_list[0], hp->h_length);
    }
    
    addr.sin_family = AF_INET;

    addr.sin_port = htons(cport);

    /* 创建套接字 */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == sockfd)
        return -1;
    
    /* 连接服务 */
    if(connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        return -1;
    
    return sockfd;
}
#include "socket.c"
#include <rpc/types.h>
#include <sys/param.h>  /* include MAXHOSTNAMELEN*/
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <time.h>
#include <signal.h>

volatile int expiredflag = 0;  /* 时间失效标志， 0为未失效 */
int success = 0;  /* 成功进程数 */
int failed   = 0;  /* 失败进程数 */
int bytes   = 0;  /* 接受数据字节数 */

enum httptype{Http09, Http10, Http11};      
int httptp = Http10;  /* http协议版本号 */

enum httpmethod{M_GET, M_HEAD, M_OPTIONS, M_TRACE}; 
int method = M_GET;   /* http报文请求方式 */

#define PROGRAM_VERSION "1.0"

int   clients   = 1;  /* 测试的进程数，默认为 1 ， 可通过参数 -c <> 指定 */
int   benchtime = 30; /* 执行时间，默认为 30 sec，可由参数 -t <> 指定， 时间结束时，结束测试 */
int   force     = 1;  /*  */
int   force_reload = 1; /*  */


char* proxyhost = NULL;  /* 代理服务器主机号， 默认不使用， 可由参数 -p <> 指定 */
int   proxyport = 80;    /* 代理服务器端口号 */
char  host[MAXHOSTNAMELEN]; /*  */

int   mypipe[2];  /*管道句柄， 用于子进程与父进程间通信*/

#define MAXREQUESTBUFSIZE 2048
char requestbuf[MAXREQUESTBUFSIZE];  /* http请求报文信息buffer*/

/* 参数项列表配置 */
static const struct option long_options[]=
{
    {"force", no_argument, &force, 0},
    {"reload", no_argument, &force_reload, 0},
    {"time", required_argument,NULL,'t'},
    {"help", no_argument, NULL, '?'},
    {"http09",no_argument,NULL,'9'},
    {"http10",no_argument,NULL,'1'},
    {"http11",no_argument,NULL,'2'},
    {"get",no_argument,&method,M_GET},
    {"head",no_argument,&method,M_HEAD},
    {"options",no_argument,&method,M_OPTIONS},
    {"trace",no_argument,&method,M_TRACE},
    {"version",no_argument,NULL,'V'},
    {"proxy",required_argument,NULL,'p'},
    {"clients",required_argument,NULL,'c'},
    {NULL,0,NULL,0}
};
const char* shortoptstr = "912Vfrt:p:c:?h";

/* 使用说明函数 */
static void usage(void)
{   
    fprintf(stderr, 
        "webbench [option]... URL\n"
        "  -f|--force               Don't wait for reply from server.\n"
        "  -r|--reload              Send reload request - Pragma: no-cache.\n"
        "  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
        "  -p|--proxy <server:port> Use proxy server for request.\n"
        "  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
        "  -9|--http09              Use HTTP/0.9 style requests.\n"
        "  -1|--http10              Use HTTP/1.0 protocol.\n"
        "  -2|--http11              Use HTTP/1.1 protocol.\n"
        "  --get                    Use GET request method.\n"
        "  --head                   Use HEAD request method.\n"
        "  --options                Use OPTIONS request method.\n"
        "  --trace                  Use TRACE request method.\n"
        "  -?|-h|--help             This information.\n"
        "  -V|--version             Display program version.\n"
	);
}

/* 时间结束信号接受函数 */ 
static void alam_handler(int signal)
{
    expiredflag = 1;
}

/* 填充请求报文 函数定义*/
static void set_request(const char* url);

/* 测试工具之内核 函数定义*/
static void bench_core(const char* host, const int port, const char* req);

/* 进入测试 函数定义*/
static int bench(void);

int main(int argc, char *argv[])
{
    int opt = 0;     /* 参数项 */
    int opt_idx = 0; /* 参数项列表索引 */
    char* proxytmp = NULL;
    if(argc < 2)
    {
        usage();
        return 2;
    }

    /* 获取参数 */
    while( (opt = getopt_long(argc, argv, shortoptstr, long_options, &opt_idx )) != EOF ){
       switch(opt)
       {
           case  0  : break;
           case 'f' : force = 0;break;
           case 'r' : force_reload = 0;break;
           case 't' : benchtime = atoi(optarg);break;
           case 'p' :
                proxyhost = optarg;
                /*变量 optarg, 系统定义, 如果有参数，则包含当前选项参数字符串。*/
                proxytmp = strrchr(optarg, ':');  /* proxytmp指向代理服务器地址中最后一次出现字符':'的位置 */
                if(NULL == proxytmp)
                {
                    break;
                }
                if(proxytmp == optarg)  /* 只有端口号 */
                {
                    fprintf(stderr, "Error in option -p %s: Missing hostname.\n", optarg);
                    return 2;
                }
                if(proxytmp == (optarg+strlen(optarg)-1) )  /* 只有主机号 */
                {
                    fprintf(stderr,"Error in option -p %s Port number is missing.\n", optarg);
		            return 2;
                }
                *proxytmp = '\0';  /* 截断 hostname 与 port  */
                proxyport = atoi(proxytmp+1); /* 代理端口号 */
                break;
           case 'c' : clients   = atoi(optarg);break;
           case '9' : httptp = Http09;break;
           case '1' : httptp = Http10;break;
           case '2' : httptp = Http11;break;
           case ':':
           case 'h' :
           case '?' : usage();return 2;
           case 'V' : printf(PROGRAM_VERSION"\n");exit(0);
       }
    }

    if(argc == optind) /* 缺少 Url 参数 */
    {/* 变量 optind, 系统定义, argv的当前索引值。当getopt_long函数在while循环中使用时，剩下的字符串为操作数，下标从optind到argc-1*/
        fprintf(stderr,"webbench: Missing URL!\n");
        usage();
        return 2;
    }

    if(clients <= 0) clients = 1;
    if(benchtime <= 0) benchtime = 30;

    /* 工具说明 */
    fprintf(stderr,"Webbench "PROGRAM_VERSION" - 简单的网站压力测试工具\n"
	    "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );

    /* 写入请求报文 */
    set_request(argv[optind]);

    /* 打印压力测试信息 */
    printf("\n压力测试信息：");
    switch (method)
    {
        default: 
        case M_GET: printf("GET");break;
        case M_OPTIONS: printf("OPTIONS");break;
        case M_HEAD: printf("HEAD");break;
        case M_TRACE: printf("TRACE");break;
    }
    printf(" %s",argv[optind]);
    switch (httptp)
    {
        case Http09: printf(" (using HTTP/0.9)");break;
        case Http11: printf(" (using HTTP/1.1)");break;
    }
    printf("\n");
    if (clients==1) printf("1 client");
    else printf("%d clients",clients);

    printf(", running %d sec", benchtime);
    if(force) printf(", early socket close");
    if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
    if(force_reload) printf(", forcing reload");
    printf(".\n");

    /* 进入测试 */
    return bench();
}


/* 填充请求报文 函数实现*/
static void set_request(const char* url)
{
    char urltmp[10];
    int idx = 0;

    /*初始化缓冲区*/
    bzero(&host, MAXHOSTNAMELEN);
    bzero(&requestbuf, MAXREQUESTBUFSIZE);

    /*http协议版本适配*/
    if ((force_reload) && (proxyhost!=NULL) && (httptp<Http10)) httptp = Http10;
    if ((method==M_HEAD) && (httptp<Http10)) httptp = Http10;
    if ((method==M_OPTIONS) && (httptp<Http11)) httptp = Http11;
    if ((method==M_TRACE) && httptp<Http11) httptp = Http11;

    /* 1 写入请求报文-请求方法类型 + 空格 */
    switch (method)
    {
        default:
        case M_GET: strcpy(requestbuf,"GET");break;
        case M_HEAD: strcpy(requestbuf,"HEAD");break;
        case M_OPTIONS: strcpy(requestbuf,"OPTIONS");break;
        case M_TRACE: strcpy(requestbuf,"TRACE");break;
    }
    strcat(requestbuf, " ");

    if (NULL == strstr(url, "://"))  /* 不存在界定符"://" */
    {
        fprintf(stderr, "\n%s: is not a valid URL.\n",url);
        exit(2);
    }

    if (strlen(url)>1500)   /* url太长 */
    {
        fprintf(stderr,"URL is too long.\n");
        exit(2);
    }
    
    
    if (NULL == proxyhost)
        if (0 != strncasecmp("http://", url, 7))  /* 不是使用http协议 */
        {
            fprintf(stderr, "\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
            exit(2);
        }

    idx = strstr(url,"://")- url + 3; /*idx 为 服务器主机号的第一个字符的索引 */

    if (NULL == strchr(url+idx, '/')) /* url未以’/‘作为结尾 */
    {
        fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
        exit(2);
    }

    if (NULL == proxyhost) { /* 未使用代理服务器时 */
        /* 获取url中的 主机号 与 端口号 */
        if ( index(url+idx, ':') != NULL &&
             index(url+idx, ':') < index(url+idx, '/'))
        {
            strncpy(host, url+idx, strchr(url+idx, ':')-url-idx);
            bzero(&urltmp, 10);
            strncpy(urltmp, index(url+idx, ':')+1, strchr(url+idx,'/')-index(url+idx,':')-1);
            proxyport = atoi(urltmp);
            if (0 == proxyport) proxyport=80;
        }
        else
        {
            strncpy(host, url+idx, strcspn(url+idx, "/"));
        }
        // printf("Host=%s : Post=%d\n", host, proxyport);
        /* 2 写入请求报文 - url  */
        strcat(requestbuf+strlen(requestbuf), url+idx+strcspn(url+idx,"/"));
    }
    else /* 使用代理服务器时 */
    {
        // printf("Proxyhost=%s : Proxyport=%d\n", host, proxyport);
        /* 2 写入请求报文 - url  */
        strcat(requestbuf,url);             // maybe error
    }

    /* 3 写入请求报文 - 空格 + http协议版本号 + 回车换行 */
    // if (httptp == Http09)
    //     strcat(requestbuf, " HTTP/0.9");
    if (httptp == Http10)
        strcat(requestbuf, " HTTP/1.0");
    else if (httptp == Http11)
        strcat(requestbuf, " HTTP/1.1");
    strcat(requestbuf, "\r\n");


    /* 写入请求报文 - 首部字段名：+空格+值+回车换行 */

    if (httptp > 0)
        strcat(requestbuf, "User-Agent: WebBench "PROGRAM_VERSION"\r\n");
    if ((NULL==proxyhost) &&(httptp>0))
    {
        strcat(requestbuf, "Host: ");
        strcat(requestbuf, host);
        strcat(requestbuf, "\r\n");
    }
    if (force_reload && proxyhost!=NULL)
    {
        strcat(requestbuf,"Pragma: no-cache\r\n");
    }
    if (httptp > Http10)
        strcat(requestbuf,"Connection: close\r\n");
    /* add empty line at end */
    if(httptp > Http09) strcat(requestbuf,"\r\n"); 
    // printf("Req=%s\n",requestbuf);
}

/* 测试工具之内核 函数实现*/
static void bench_core(const char* host, const int port, const char* req)
{
    int wlen;
    char buf[1500];
    int sockfd,rlen;
    struct sigaction sa;

    /* 设置时钟信号函数 */
    sa.sa_handler = alam_handler;
    sa.sa_flags   = 0;
    if (sigaction(SIGALRM, &sa, NULL))
        exit(3);
    alarm(benchtime);

    wlen = strlen(req);
nexttry:
    while (1)
    {
        /* 时间结束，结束测试 */
        if (expiredflag)
        {
            if(failed > 0)
                failed--;
            return;
        }
        /* 连接 */
        sockfd = Socket(host, port);
        if (sockfd < 0)
        {
            failed++;
            continue;
        }
        /* 发送req */
        if (wlen != write(sockfd,req,wlen)) 
        {
            failed++;
            close(sockfd);
            continue;
        }

        if (httptp == Http09) 
	        if (shutdown(sockfd, 1))
            { 
                failed++;
                close(sockfd);
                continue;
            }
        
        if (force == 1) 
        {
            /* read all available data from socket */
            while (1)
            {
                if (expiredflag) break; 
                rlen = read(sockfd, buf, 1500);
                /* fprintf(stderr,"%d\n",rlen); */
                if (rlen < 0) 
                { 
                    failed++;
                    close(sockfd);
                    goto nexttry;
                }
                else if (rlen == 0) break;
                else bytes += rlen;
            }
        }
        
        if (close(sockfd))
        {
            failed++;
            continue;
        }

        success++;
    }
}

/* 进入测试 函数实现*/
static int bench(void)
{
    int i, j, k;
    pid_t pid=0;
    FILE *fp;

    /* 检查目标服务是否可用 */
    i = Socket( ((NULL==proxyhost)?host:proxyhost), proxyport);
    if (-1 == i)
    {
        fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
        return 1;
    }
    close(i);

    /* 创建管道 */
    if (pipe(mypipe))
    {
        perror("pipe failed.");
	    return 3;
    }

    /* 创建子进程 */
    for (i=0; i<clients; i++)
    {
        pid = fork();
        if (pid <= (pid_t)0)
        {
            sleep(1); 
            break;
        }
    }

    /* fork失败 */
    if (pid < (pid_t)0)
    {
        fprintf(stderr,"problems forking worker no. %d\n",i);
        perror("fork failed.");
        return 3;
    }

    if(pid == (pid_t)0)  /* 子进程 */
    {
        if(proxyhost==NULL)
            bench_core(host,proxyport,requestbuf);
        else
            bench_core(proxyhost,proxyport,requestbuf);

        /* 打开管道写口 */
        fp = fdopen(mypipe[1], "w");
        if(NULL == fp)
        {
            perror("open pipe for writing failed.");
            return 3;
        }
        /* 将结果写入管道写口 */
        fprintf(fp,"%d %d %d\n", success, failed, bytes);
        /* 关闭管道写口 */
        fclose(fp);
        return 0;
    }
    else  /* 父进程 */
    {
        /* 打开管道读口 */
        fp=fdopen(mypipe[0], "r");
        if (NULL == fp)
        {
            perror("open pipe for reading failed.");
            return 3;
        }
        setvbuf(fp, NULL, _IONBF, 0);  /* #define _IONBF 2 */
        success = 0;
        failed = 0;
        bytes = 0;
        while (1)
        {
            /* 从管道读口读取结果 */
            pid=fscanf(fp,"%d %d %d", &i, &j, &k);
            if (pid < 2)
            {
                fprintf(stderr,"Some of our childrens died.\n");
                break;
            }
            success += i;
            failed += j;
            bytes += k;
            /* fprintf(stderr,　"*Knock* %d %d read=%d\n",success,　failed,　pid); */
            if(--clients==0) break;
        }
        /* 关闭管道读口 */
        fclose(fp);
        
        /* 输出测试结果 */
        printf("\n Speed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
            (int)((success+failed)/(benchtime/60.0f)),
            (int)(bytes/(float)benchtime),
            success,
            failed);
    }
     return i;
}
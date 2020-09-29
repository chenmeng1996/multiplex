#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>

#define IPADDR      "127.0.0.1"
#define PORT        8787
#define MAXLINE     1024
#define LISTENQ     5
#define SIZE        10

typedef struct server_context_st {
    int cli_cnt;        /*客户端个数*/
    int clifds[SIZE];   /*客户端的个数*/
    fd_set allfds;      /*句柄集合*/
    int maxfd;          /*句柄最大值*/
} server_context_st;
static server_context_st *s_srv_ctx = NULL;

/*===========================================================================
 * fd_set select文件句柄的集合
 * FD_ZERO 清空这个集合
 * FD_SET 往这个集合加入一个文件句柄
 * FD_CLR 把文件句柄从集合中删除
 * FD_ISSET 查看某一个文件句柄是否在集合中
 *
 * accept 用于从已完成连接队列返回下一个已完成连接，服务端连接客户端（TCP连接）
 * ==========================================================================*/
/**
 * 创建服务端进程，监听端口
 * @param ip
 * @param port
 * @return
 */
static int create_server_proc(const char *ip, int port) {
    int fd;
    //套接字数据结构
    struct sockaddr_in servaddr;
    //新建socket
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf(stderr, "create socket fail,erron:%d,reason:%s\n",
                errno, strerror(errno));
        return -1;
    }

    /*一个端口释放后会等待两分钟之后才能再被使用，SO_REUSEADDR是让端口释放后立即就可以被再次使用。*/
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
        return -1;
    }

    //初始化 套接字数据结构
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    //ip地址转换成数字
    inet_pton(AF_INET, ip, &servaddr.sin_addr);
    //端口转换成 ？
    servaddr.sin_port = htons(port);

    //将套接字 与 套接字数据结构绑定，即与ip:port绑定
    if (bind(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1) {
        perror("bind error: ");
        return -1;
    }

    //套接字开始监听端口
    listen(fd, LISTENQ);

    return fd;
}

/**
 * 监听新的客户端请求，将连接描述符放入轮询数组
 * @param srvfd
 * @return
 */
static int accept_client_proc(int srvfd) {
    struct sockaddr_in cliaddr;
    socklen_t cliaddrlen;
    cliaddrlen = sizeof(cliaddr);
    int clifd = -1;

    printf("accpet clint proc is called.\n");

    ACCEPT:
    //服务端从已连接队列中取出下一个已连接套接字
    clifd = accept(srvfd, (struct sockaddr *) &cliaddr, &cliaddrlen);

    if (clifd == -1) {
        if (errno == EINTR) {
            goto ACCEPT;
        } else {
            fprintf(stderr, "accept fail,error:%s\n", strerror(errno));
            return -1;
        }
    }

    fprintf(stdout, "accept a new client: %s:%d\n",
            inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port);

    //将新的连接描述符添加到数组中
    int i = 0;
    for (i = 0; i < SIZE; i++) {
        if (s_srv_ctx->clifds[i] < 0) {
            s_srv_ctx->clifds[i] = clifd;
            s_srv_ctx->cli_cnt++;
            break;
        }
    }

    if (i == SIZE) {
        fprintf(stderr, "too many clients.\n");
        return -1;
    }
}

static int handle_client_msg(int fd, char *buf) {
    assert(buf);
    printf("recv buf is :%s\n", buf);
    //向已连接套接字中写数据（即向客户端发送数据）
    write(fd, buf, strlen(buf) + 1);
    return 0;
}

/**
 * 遍历处理所有客户端的消息
 * @param readfds
 */
static void recv_client_msg(fd_set *readfds) {
    int i = 0, n = 0;
    int clifd;
    char buf[MAXLINE] = {0};
    //遍历所有已连接套接字
    for (i = 0; i <= s_srv_ctx->cli_cnt; i++) {
        clifd = s_srv_ctx->clifds[i];
        if (clifd < 0) {
            continue;
        }
        /*判断客户端套接字是否有数据*/
        if (FD_ISSET(clifd, readfds)) {
            //接收客户端发送的信息
            n = read(clifd, buf, MAXLINE);
            if (n <= 0) {
                /*n==0表示读取完成，客户都关闭套接字*/
                FD_CLR(clifd, &s_srv_ctx->allfds);
                close(clifd);
                s_srv_ctx->clifds[i] = -1;
                continue;
            }
            handle_client_msg(clifd, buf);
        }
    }
}

static void handle_client_proc(int srvfd) {
    int clifd = -1;
    int retval = 0;
    fd_set *readfds = &s_srv_ctx->allfds;
    struct timeval tv;
    int i = 0;

    while (1) {
        /*每次调用select前都要重新设置文件描述符和时间，因为事件发生后，文件描述符和时间都被内核修改啦*/
        FD_ZERO(readfds);
        /*添加服务端监听套接字*/
        FD_SET(srvfd, readfds);
        s_srv_ctx->maxfd = srvfd;

        tv.tv_sec = 30;
        tv.tv_usec = 0;
        /*添加客户端套接字*/
        for (i = 0; i < s_srv_ctx->cli_cnt; i++) {
            clifd = s_srv_ctx->clifds[i];
            /*去除无效的客户端句柄*/
            if (clifd != -1) {
                FD_SET(clifd, readfds);
            }
            s_srv_ctx->maxfd = (clifd > s_srv_ctx->maxfd ? clifd : s_srv_ctx->maxfd);
        }
        printf("111");
        /*调用select函数，
         * 将fd_set拷贝到内核，注册回调函数。
         * 内核开始轮询接收处理服务端和客户端套接字，调用其对应的poll方法，
         * 如果没有连接或者连接没有数据，poll方法会调用回调函数，将当前线程挂到设备的等待队列；如果有连接或者数据，poll会调用回调函数，唤醒等待队列上睡眠的线程，然后poll返回该连接的mask掩码*/
        retval = select(s_srv_ctx->maxfd + 1, readfds, NULL, NULL, &tv);
        printf("222");
        if (retval == -1) {
            fprintf(stderr, "select error:%s.\n", strerror(errno));
            return;
        }
        if (retval == 0) {
            fprintf(stdout, "select is timeout.\n");
            continue;
        }
        //当有新的客户端发起连接，select函数唤醒调用者，服务端监听套接字还在fd_set中
        if (FD_ISSET(srvfd, readfds)) {
            /*处理新的客户端连接*/
            accept_client_proc(srvfd);
        } else {
            /*当已连接的客户端发来消息，select函数唤醒调用者，此时fd_set被内核修改了，只有有数据的连接套接字在fd_set中，所以服务端监听套接字不在fd_set中。此时处理有消息的连接*/
            recv_client_msg(readfds);
        }
    }
}

static void server_uninit() {
    if (s_srv_ctx) {
        free(s_srv_ctx);
        s_srv_ctx = NULL;
    }
}

/**
 * 初始化
 * @return
 */
static int server_init() {
    //申请内存
    s_srv_ctx = (server_context_st *) malloc(sizeof(server_context_st));
    if (s_srv_ctx == NULL) {
        return -1;
    }

    //为新申请的内存做初始化
    memset(s_srv_ctx, 0, sizeof(server_context_st));

    int i = 0;
    for (; i < SIZE; i++) {
        s_srv_ctx->clifds[i] = -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int srvfd; //服务端句柄
    /*初始化服务端context*/
    if (server_init() < 0) {
        return -1;
    }
    /*创建服务,开始监听客户端请求*/
    srvfd = create_server_proc(IPADDR, PORT);
    if (srvfd < 0) {
        fprintf(stderr, "socket create or bind fail.\n");
        goto err;
    }
    /*开始接收并处理客户端请求*/
    handle_client_proc(srvfd);
    server_uninit();
    return 0;
    err:
    server_uninit();
    return -1;
}
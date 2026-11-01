/* Exercise real loopback HTTP requests and real media decoder teardown.
 * No audio device, microphone, camera or screen capture is opened. */
#include "media/media.h"
#include "net/net.h"
#include "base/util.h"
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef NDEBUG
#error "Assertions must remain enabled"
#endif

typedef struct {
    int listener, mode, error;
    unsigned short port;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool ready, release, fallback;
} Server;
/* A small, valid PCM WAV: 16 mono samples, 8 kHz, signed 16-bit. */
static const unsigned char wav[76] = {
    'R','I','F','F',68,0,0,0,'W','A','V','E','f','m','t',' ',
    16,0,0,0,1,0,1,0,0x40,0x1f,0,0,0x80,0x3e,0,0,2,0,16,0,
    'd','a','t','a',32,0,0,0
};
static struct timespec deadline_ms(long ms) {
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
    t.tv_sec+=ms/1000;t.tv_nsec+=(ms%1000)*1000000;
    if(t.tv_nsec>=1000000000){t.tv_sec++;t.tv_nsec-=1000000000;}return t;
}
static int send_all(int fd,const void *data,size_t size) {
    const unsigned char *p=data;
    while(size){ssize_t n=send(fd,p,size,0);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;size-=(size_t)n;}return 0;
}
static void *serve(void *arg) {
    Server *s=arg;struct pollfd pollfd={s->listener,POLLIN,0};
    if(poll(&pollfd,1,3000)!=1){s->error=1;return NULL;}
    int fd=accept(s->listener,NULL,NULL);if(fd<0){s->error=2;return NULL;}
    struct timeval timeout={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
    char request[4096]={0};size_t used=0;
    while(used<sizeof request-1&&!strstr(request,"\r\n\r\n")){
        ssize_t n=recv(fd,request+used,sizeof request-1-used,0);
        if(n<0&&errno==EINTR)continue;if(n<=0){s->error=3;close(fd);return NULL;}used+=(size_t)n;request[used]=0;
    }
    const char *head="HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: 76\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
    size_t sent=0;
    if(s->mode==2){send_all(fd,head,strlen(head));send_all(fd,wav,16);sent=16;}
    pthread_mutex_lock(&s->mutex);s->ready=true;pthread_cond_broadcast(&s->cond);
    if(s->mode==1||s->mode==2){
        struct timespec limit=deadline_ms(1500);
        while(!s->release){int r=pthread_cond_timedwait(&s->cond,&s->mutex,&limit);if(r==ETIMEDOUT){s->fallback=true;break;}assert(r==0);}
    }
    pthread_mutex_unlock(&s->mutex);
    if(s->mode==4){const char *error="HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_all(fd,error,strlen(error));}
    else {if(!sent)send_all(fd,head,strlen(head));send_all(fd,wav+sent,sizeof wav-sent);}
    close(fd);return NULL;
}
static void server_start(Server *s,int mode) {
    memset(s,0,sizeof *s);s->mode=mode;pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->cond,NULL);
    s->listener=socket(AF_INET,SOCK_STREAM,0);assert(s->listener>=0);
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    assert(bind(s->listener,(struct sockaddr *)&addr,sizeof addr)==0);socklen_t n=sizeof addr;
    assert(getsockname(s->listener,(struct sockaddr *)&addr,&n)==0);s->port=ntohs(addr.sin_port);
    assert(listen(s->listener,1)==0);assert(pthread_create(&s->thread,NULL,serve,s)==0);
}
static void wait_ready(Server *s) {
    pthread_mutex_lock(&s->mutex);struct timespec limit=deadline_ms(4000);
    while(!s->ready){int r=pthread_cond_timedwait(&s->cond,&s->mutex,&limit);assert(r==0);}
    pthread_mutex_unlock(&s->mutex);
}
static void release_server(Server *s) {
    pthread_mutex_lock(&s->mutex);s->release=true;pthread_cond_broadcast(&s->cond);pthread_mutex_unlock(&s->mutex);
}
static void drain(void) {
    double limit=now_ms()+4000;
    do{net_poll();if(!net_pending())return;usleep(1000);}while(now_ms()<limit);
    fprintf(stderr,"network completion queue failed to drain\n");assert(!net_pending());
}
static int run_case(const char *name,int mode,bool before_dispatch) {
    Server server;server_start(&server,mode);char url[256];
    snprintf(url,sizeof url,"http://127.0.0.1:%u/%s",server.port,name);
    MediaPlayer *player=mp_new(NULL);assert(player);mp_open_url(player,url);wait_ready(&server);
    bool pass=true;
    if(mode==1||mode==2){
        double start=now_ms();mp_free(player);double elapsed=now_ms()-start;
        pthread_mutex_lock(&server.mutex);bool fallback=server.fallback;pthread_mutex_unlock(&server.mutex);
        /* Success means teardown returned while the server was still holding
         * the response, not merely before a relaxed external timeout. */
        pass=!fallback;release_server(&server);
        fprintf(stderr,"%s: free_ms=%.3f server_needed_fallback=%d %s\n",name,elapsed,fallback,pass?"PASS":"FAIL");
    }else{
        pthread_join(server.thread,NULL);server.thread=0;
        if(before_dispatch){usleep(50000);mp_free(player);}
        else {
            MpState state={0};double limit=now_ms()+4000;
            do{net_poll();mp_state(player,&state);if(mode==4?state.error:state.ready>0)break;usleep(1000);}while(now_ms()<limit);
            pass=mode==4?state.error:(state.ready>0&&isfinite(state.duration)&&state.duration>0);
            fprintf(stderr,"%s: ready=%d error=%d duration=%g %s\n",name,state.ready,state.error,state.duration,pass?"PASS":"FAIL");mp_free(player);
        }
    }
    if(server.thread)pthread_join(server.thread,NULL);assert(!server.error);close(server.listener);
    pthread_cond_destroy(&server.cond);pthread_mutex_destroy(&server.mutex);drain();
    if(before_dispatch)fprintf(stderr,"%s: completion after player destruction drained PASS\n",name);
    return !pass;
}
int main(void) {
    net_init(2);net_set_proxy(NULL);int failures=0;
    failures+=run_case("stalled-headers",1,false);
    failures+=run_case("stalled-body",2,false);
    failures+=run_case("successful-wave",3,false);
    failures+=run_case("network-error",4,false);
    for(int i=0;i<10;i++)failures+=run_case("completion-after-free",3,true);
    drain();net_shutdown();usleep(10000);
    fprintf(stderr,"media-download-lifecycle: 14 cases, %d failures\n",failures);return failures?1:0;
}

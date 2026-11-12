#define PI_QUEUE_CAPTURE 1
#include <assert.h>
#include <stdio.h>
#include "../../components/pi_link/pi_link.c"
static unsigned char output[16];
static void reset(void){s_id=42;s_raw_left=s_line_length=s_meta_length=s_meta_received=s_received=s_total=0;s_done=s_failed=s_bad_line=raw_ask=false;memset(&incoming_ask,0,sizeof(incoming_ask));s_capacity=sizeof(output);s_body=output;memset(output,0,sizeof(output));atomic_store(&s_disconnected,false);}
static void feed(const char *s){received((const uint8_t*)s,strlen(s),NULL);}
int main(void){
 reset();feed("HEL");feed("LO pi-zero 2.0.0 run=3\nSTAT up=5 ip=192.168.1.2\n");assert(s_status.online && !strcmp(s_status.host,"pi-zero") && !strcmp(s_status.ip,"192.168.1.2"));
 feed("REPLY 42 200 3 5\nx=yPART 42 2\na\nPART 42 3\nbcd");assert(s_done && !s_failed && s_received==5 && !memcmp(output,"a\nbcd",5) && !strcmp(s_meta,"x=y"));
 reset();feed("REPLY 42 200 0 0\n");assert(s_done && !s_failed);
 reset();feed("REPLY 42 200 384 1\n");assert(s_done && s_failed && atomic_load(&s_disconnected));
 reset();feed("REPLY 42 200 0 17\n");assert(s_failed);
 reset();feed("REPLY 99 200 0 1\n");assert(s_failed);
 reset();feed("PART 42 1\n");assert(s_failed);
 reset();feed("REPLY 42 200 0 2\nPART 42 3\n");assert(s_failed);
 reset();for(unsigned i=0;i<300;i++)feed("x");feed("\nHELLO pi 2.0.0\n");assert(!s_bad_line && !strcmp(s_status.host,"pi"));
 reset();feed("REPLY 42 200 0 5\nPART 42 5\n");s_id=0;s_raw_left=0;s_body=NULL;s_capacity=0;feed("PART 42 5\nabcde");assert(s_failed);
 reset();feed("BYE\n");assert(s_status.bye && !s_status.online);
 asks=xQueueCreate(2,sizeof(ask_t));ask_t ask;
 reset();feed("ASK 11 state 0\n");assert(xQueueReceive(asks,&ask,0)&&ask.id==11&&!strcmp(ask.command,"state")&&!ask.arg[0]);
 reset();feed("ASK 12 install 9\n0 foo");assert(raw_ask&&!s_done);feed(".bin");assert(!raw_ask&&xQueueReceive(asks,&ask,0)&&!strcmp(ask.arg,"0 foo.bin"));
 reset();feed("ASK 13 install 1025\n");assert(atomic_load(&s_disconnected));
 reset();feed("ASK 14 install 3\n");const uint8_t bad[]={0,'a','b'};received(bad,3,NULL);assert(atomic_load(&s_disconnected)&&!raw_ask);
 reset();feed("REPLY 42 200 0 2\nASK 15 state 0\nPART 42 2\nok");assert(xQueueReceive(asks,&ask,0)&&ask.id==15&&s_done&&!s_failed&&!memcmp(output,"ok",2));
 puts("Pi framing: fragmented lines/raw, binary newlines, limits, wrong IDs, late replies and BYE passed.");
}

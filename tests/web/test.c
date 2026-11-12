#include "web_security.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 const char *token="12345678";web_session_t s;
 web_session_begin(&s,token,100);assert(web_session_accept(&s,token,8,101));
 for(unsigned i=0;i<8;i++)assert(!web_session_accept(&s,"bad",3,102));
 assert(!web_session_accept(&s,token,8,103));assert(web_session_accept(&s,token,8,30000103));
 assert(!web_session_accept(&s,token,8,s.expires));
 web_session_begin(&s,token,0);assert(!web_session_accept(&s,NULL,0,1));
 char out[64];assert(web_query_decode("%2Fsdcard%2Fteste+um.bin",out,sizeof(out)) && !strcmp(out,"/sdcard/teste um.bin"));
 const char *bad[]={"%00","abc%","%0A","%7f","%GG","a\r"};for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!web_query_decode(bad[i],out,sizeof(out)));
 assert(!web_query_decode("1234",out,4));assert(web_query_decode("%252e%252e",out,sizeof(out)) && !strcmp(out,"%2e%2e"));
 assert(web_path_is_private("/sdcard/launcher/private/wifi.dat"));
 assert(web_path_is_private("/sdcard/LAUNCHER/PRIVATE/wifi.dat"));
 assert(web_path_is_private("/sdcard/launcher./private /wifi.dat"));
 assert(!web_path_is_private("/sdcard/launcher/web/index.html"));
 assert(!web_path_is_private("/sdcard/launcher/config.json"));
 memset(&s,0,sizeof(s));assert(!web_session_accept(&s,token,8,0));
 web_login_t logins[WEB_LOGIN_SESSIONS];web_logins_reset(logins);const char*secret="0123456789abcdef0123456789abcdef";web_login_issue(logins,secret,1);assert(web_login_accept(logins,secret,2)==0);char cookie[33];assert(web_cookie_extract("other=x; tab5_session=0123456789abcdef0123456789abcdef; another=y",cookie)&&!strcmp(cookie,secret));assert(!web_cookie_extract("tab5_session=short",cookie));assert(web_login_accept(logins,secret,2LL*3600*1000000+3)==-1);web_login_issue(logins,secret,1);for(unsigned i=1;i<8;i++)assert(web_login_accept(logins,secret,i*3600LL*1000000)>=0);assert(web_login_accept(logins,secret,8LL*3600*1000000+2)==-1);
 char destination[512];assert(web_upload_path("/sdcard","pasta/sub/ação.bin",destination,sizeof(destination)));assert(!strcmp(destination,"/sdcard/pasta/sub/ação.bin"));const char*badpaths[]={"../x","a/../../x","/absolute","a\\b","a/./b","a//b","launcher/private/token","a./b","a /b","a/"};for(unsigned i=0;i<sizeof(badpaths)/sizeof(*badpaths);i++)assert(!web_upload_path("/sdcard",badpaths[i],destination,sizeof(destination)));
 puts("Web: token match, expiry, rate limits, revoked session, single-pass decoding and NUL/control rejection passed.");
}

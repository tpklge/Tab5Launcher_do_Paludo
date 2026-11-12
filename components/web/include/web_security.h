#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WEB_TOKEN_LENGTH 8
typedef struct {char token[WEB_TOKEN_LENGTH+1];int64_t expires,blocked_until;unsigned failures;} web_session_t;
void web_session_begin(web_session_t *session,const char token[WEB_TOKEN_LENGTH+1],int64_t now);
bool web_session_accept(web_session_t *session,const char *token,size_t length,int64_t now);
bool web_query_decode(const char *encoded,char *out,size_t size);

/* FAT paths are case-insensitive and may accept trailing dots/spaces. */
bool web_path_is_private(const char *path);
#define WEB_LOGIN_SESSIONS 4
typedef struct { char secret[33];int64_t created,last; } web_login_t;
void web_logins_reset(web_login_t sessions[WEB_LOGIN_SESSIONS]);
int web_login_issue(web_login_t sessions[WEB_LOGIN_SESSIONS],const char secret[33],int64_t now);
int web_login_accept(web_login_t sessions[WEB_LOGIN_SESSIONS],const char *cookie,int64_t now);
bool web_cookie_extract(const char *header,char out[33]);
/* Relative file path plus parent directory; no traversal or FAT aliases. */
bool web_upload_path(const char *directory,const char *relative,char *path,size_t size);

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define DEFAULT_HTTP_PORT 80

#define DEBUG
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

void doit(int fd);
int parse_request(rio_t *rp, char *hostname, char *path, int *port, char *send_req);
void form_request(rio_t *rp, char *request, char *hostname, char *method, char *path, int *port, char *send_req);
void *thread(void *vargp);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
		 
int main(int argc, char **argv){
	int listenfd, port;
	int *connfd;
	socklen_t clientlen;
    struct sockaddr_in clientaddr;
	struct hostent *hp;
	char *haddrp;
	pthread_t tid;
	
	/* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }
	
	port = atoi(argv[1]);
	dbg_printf("listenning on port %d\n", port);fflush(stdout);
	listenfd = Open_listenfd(port);
	//dbg_printf("listenfd: %d\n", listenfd);fflush(stdout);
	
	while (1) {
		clientlen = sizeof(struct sockaddr_in);
		connfd = Malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
		//dbg_printf("connfd: %d\n", connfd);
		hp = Gethostbyaddr((const char*)&clientaddr.sin_addr.s_addr, 
							sizeof(clientaddr.sin_addr.s_addr), AF_INET);
		haddrp = inet_ntoa(clientaddr.sin_addr);
		printf("### proxy connected to client host %s (%s)\n", hp->h_name,haddrp);
		
		Pthread_create(&tid, NULL, thread, connfd);
	}

    return 0;
}

/*
 * thread - .
 */
void *thread(void *vargp){
	int connfd = *((int *)vargp);
	Pthread_detach(pthread_self());
	Free(vargp);
	doit(connfd);
	Close(connfd);
	return NULL;
}

/*
 * doit - handle one HTTP request/response transaction
 */
 void doit(int connfd){
    rio_t rio_conn, rio_cli;
    int port = 0;
	char hostname[MAXLINE],path[MAXLINE],send_req[MAXLINE],response[MAXLINE];
	int clientfd;
	
    Rio_readinitb(&rio_conn, connfd);
	
    /* Parse URI from GET request */
    parse_request(&rio_conn, hostname, path, &port, send_req);
	//dbg_printf("hostname: %s, path: %s, port: %d, send_req: %s \n", hostname,path,port, send_req);
	fflush(stdout);
	
	/* establish connection to the appropriate web server  */
	clientfd = Open_clientfd(hostname, port);
	dbg_printf("### proxy establish connection with server: %s \n", hostname);
	Rio_readinitb(&rio_cli, clientfd);
	Rio_writen(clientfd,send_req,MAXLINE); /* send the request*/
	dbg_printf("### Proxy send request to server: %s \n", send_req);
	
	/* read the server’s response*/
	dbg_printf("### proxy receive response from server:  \n" );
	size_t n = Rio_readlineb(&rio_cli,response,MAXLINE);
	printf(">%s", response);
	fflush(stdout);
	Rio_writen(connfd,response,n);
	while(n > 0) {
		n = Rio_readlineb(&rio_cli,response,MAXLINE);
		printf(">%s", response);
		fflush(stdout);
		Rio_writen(connfd,response,n);
	}
	
	Close(clientfd);
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_request(rio_t *rio_conn, char *hostname, char *path, int *port, char *send_req) {
    char *ptr;
    char request[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
	int i = 0;

    /* Read request line */
    size_t n = Rio_readlineb(rio_conn, request, MAXLINE);
	
	/* ignore empty request */
	if (n <= 0){
        clienterror(rio_conn->rio_fd, "Bad request", "0", "Empty request", "Empty request");
        return -1;
    }
	dbg_printf("### Proxy receive request from client: %s \n",request);
	fflush(stdout);
	
	/* this proxy only support GET */
    sscanf(request, "%s %s %s", method, url, version);
    if (strcasecmp(method, "GET")) { 
		clienterror(rio_conn->rio_fd, method, "501", "Not Implemented",
                "Does not support this method");
        return -1;
    }
	
	ptr = strstr(url,"://");
	if (ptr == NULL){
		clienterror(rio_conn->rio_fd, "Bad request", "1", "Wrong Url", "Wrong Url");
        return -1;
	}
	
	/* hostname */
	ptr += 3;
	for (i=0; *ptr != '\0' && *ptr != '/'; ptr++, i++) {
        hostname[i] = *ptr;
    }
	hostname[i+1] = '\0';
	
	/* path pointer */
	if (*ptr == '\0')
        strcat(url, "/");
    path = ptr;
	
	/* port */
	ptr = strstr(hostname, ":");
	if (!ptr)
		*port = DEFAULT_HTTP_PORT;
    else{
        *ptr = '\0';
        ptr++;
        *port = atoi(ptr);
    }
	
	form_request(rio_conn, request, hostname, method, path, port, send_req);
	return 0;
}

/*
 * form_request - form the request to be sent to the server.
 */
void form_request(rio_t *rio_conn, char *request, char *hostname, char *method, char *path, int *port, char *send_req){
	sprintf(send_req, "%s %s HTTP/1.0\r\n", method, path);
	while(strcmp(request, "\r\n")) {
        Rio_readlineb(rio_conn, request, MAXLINE);
        if (strstr(request, ":") && !strstr(request, "User-Agent:") && !strstr(request, "Accept:")
            && !strstr(request, "Accept-Encoding:") && !strstr(request, "Connection:") 
			&& !strstr(request, "Proxy-Connection:")) {
            strcat(send_req, request);
        }
        
        printf(">%s", request);
        fflush(stdout);
    }
    if (!strstr(send_req, "Host:"))
        sprintf(send_req, "%sHost: %s\r\n",send_req,hostname);
	strcat(send_req, user_agent_hdr);
    strcat(send_req, accept_hdr);
    strcat(send_req, accept_encoding_hdr);
    strcat(send_req, connection_hdr);
    strcat(send_req, proxy_connection_hdr);
    strcat(send_req, "\r\n");
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg){
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
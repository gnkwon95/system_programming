#include <stdio.h>
#include "csapp.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

//operation related to proxy values
sem_t mutex;
static int set_num, line_num;

void *thread(void *vargp);
void run(int fd);
void read_requesthdrs(rio_t *rp);
static void request_hdr(char *buf, char *buf2server, char *hostname);
void parse_uri(char *uri, char *hostname, int *port);
int powerten(int i);

//cache-related functions
static int load_cache(char *tag, char *response);
static void save_cache(char *tag, char *response);
static void update_use(int *cache_use, int current, int len);

struct cache_line{
	int valid;
	char *tag;
	char *block;
};

struct cache_set{ // cache set with a line
	struct cache_line *line;
	int *use;
};

struct cache{
	struct cache_set *set;
};

static struct cache cache;



int main(int argc, char **argv)
{
	if (argc != 2){
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}
	
	signal(SIGPIPE, SIG_IGN); // ignore sigpipe instruction

	int listenfd;
	int *connfd; 	// same as instructed

	char hostname[MAXLINE], port[MAXLINE];
	//socklen_t clientlen = sizeof(struct sockaddr_in);
	socklen_t clientlen;
	struct sockaddr_in clientaddr;
	pthread_t tid;

	//establish proxy
	sem_init(&mutex, 0, 1);
	set_num = 1;
	line_num = 10;
	
	//initialize cache
	cache.set = malloc(sizeof (struct cache_set) * set_num);
	for (int i=0; i<set_num; i++){
		cache.set[i].line = malloc(sizeof(struct cache_line) * line_num);
		cache.set[i].use = malloc(sizeof(int) * line_num);
		for (int j=0; j<line_num; j++){
			cache.set[i].use[j] = j;
			cache.set[i].line[j].valid = 0;
			cache.set[i].line[j].tag = malloc(MAXLINE);
			cache.set[i].line[j].block = malloc(MAX_OBJECT_SIZE);
		}
	}

	printf(" cache init complete\n");

	listenfd = Open_listenfd(argv[1]); // set given port number as listening
//	printf(" listening...\n");
	while (1){
		printf("-=======listening...\n");
		clientlen = sizeof(clientaddr);
		connfd = malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
		printf("connected to %s %s\n", hostname, port);
		pthread_create(&tid, NULL, thread, connfd); // set thread to allow concurrent requests
	}

	//first listen for incoming connections on port
	// - how to get info from commandline with this main?
	// 1. determine if HTTP is valid
	// establish own connection to appropriate web server, request object
	// read server's response and forward it to client
   // printf("%s", user_agent_hdr);
    return 0;
}

void *thread(void *vargp)
{
	int connfd = *((int *)vargp);
	Pthread_detach(pthread_self()); //run on detached mode
	Free(vargp);
	run(connfd); //main function that parse and run input
	Close(connfd);
	return NULL;
}

void run(int connfd)
{
	int serverfd, len, object_len;

	int *port;
	char port2[10];
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char cache_buf[MAX_OBJECT_SIZE];
	char filename[MAXLINE];
	char hostname[MAXBUF];
	char buf2server[MAXLINE];
	char serverResponse[MAXLINE];
	rio_t rio, rio_server;

	port = malloc(sizeof(int));
	*port = 80; 	//default port set to 80

	memset(buf2server, 0, sizeof(buf2server));
	memset(filename, 0, sizeof(filename));
	memset(hostname, 0, sizeof(hostname));
	memset(serverResponse, 0, sizeof(serverResponse));
	memset(uri, 0, sizeof(uri));	
	memset(method, 0, sizeof(method));
	memset(buf, 0, sizeof(buf));	
	memset(version, 0, sizeof(version));
	memset(cache_buf, 0, sizeof(cache_buf));

	// part 1 - obtain request and parse
	Rio_readinitb(&rio, connfd);
	if (!Rio_readlineb(&rio, buf, MAXLINE)) return;
	printf("request from client: %s\n", buf);

	//parse input to three parts (method  = GET). Version should be 1.1
	sscanf(buf, "%s %s %s", method, uri, version);


	if (!strcmp(version, "HTTP/1.1")) { // try testing if strcmp works...
		strcpy(version, "HTTP/1.0");
	}

	printf("new ver = %s\n", version);

	//method should be get
	if (strcasecmp(method, "GET")){
	//	printf("exit\n");
		return;
	}

	read_requesthdrs(&rio); // read HTTP request

//	printf( " check1\n");

	parse_uri(uri, hostname, port); //parse uri.... what's return?
//	printf("parsed \n");
	strcpy(filename, uri);
	sprintf(buf2server, "%s %s %s\r\n", method, filename, version);
	printf("proxy to server: %s\n", buf2server);

	printf("\n\n buf before = %s\n", buf);
	request_hdr(buf, buf2server, hostname);
	printf("\n\n buf to server = %s\n", buf2server);

	// check cache first
	if (load_cache(uri, cache_buf) == 1) {
		//cache hit
		printf("@@@@@@@\n\n cache buf = %s\n", cache_buf);
		printf( "cache hit \n");
		if (rio_writen(connfd, cache_buf, sizeof(cache_buf))<0 ){
			fprintf(stderr, "Error: cache load\n");
			return;
		}
		memset(cache_buf, 0, sizeof(cache_buf));
	} else{ // cache miss, forward request to server
		printf("no cache hit\n");
		sprintf(port2, "%d", *port);
		if((serverfd = open_clientfd(hostname, port2))<0){ //open_client is safe
			fprintf(stderr, "open server fd error\n");
			return;
		}
		Rio_readinitb(&rio_server, serverfd);

		//send request to server
		Rio_writen(serverfd, buf2server, strlen(buf2server));

		// receive response from server and save data in cache
		memset(cache_buf, 0, sizeof(cache_buf));
		object_len = 0;

		while ((len = rio_readnb(&rio_server, serverResponse, 
			sizeof(serverResponse))) >0 ) {
			Rio_writen(connfd, serverResponse, len);
		
			strcat(cache_buf, serverResponse);
			object_len += len;
			memset(serverResponse, 0, sizeof(serverResponse));
		}
		if (object_len <= MAX_OBJECT_SIZE){
			P(&mutex);
			save_cache(uri, cache_buf);
			V(&mutex);
		}
		close(serverfd);
	}
	printf("run complete\n");
}



void read_requesthdrs(rio_t *rp){
	char buf[MAXLINE];
	
	Rio_readlineb(rp, buf, MAXLINE);
	printf("read_requesthdrs\n");
	printf("%s", buf);
	while(strcmp(buf, "\r\n")){
		Rio_readlineb(rp, buf, MAXLINE);
		printf("--");
		printf("%s", buf);
	}
	return;
}

static void request_hdr(char *buf, char *buf2server, char *hostname)
{
	printf("request header %s\n", buf);
	if (strcmp(buf, "host")){ // always send Host header
	//	printf("host found\n");
		strcat(buf2server, "Host: ");
		strcat(buf2server, hostname);
		strcat(buf2server, "\r\n");
	}

	if (strcmp(buf, "User-Agent:")) { // optional - send user-agent
	//	printf("user-agent... \n");
		strcat(buf2server, user_agent_hdr);
	}

	if (strcmp(buf, "Connection:")){ // always send connection
	//	printf("connection..\n");
		strcat(buf2server, "Connection: close\r\n");
	}
	if (strcmp(buf, "Proxy-Connection:")) { //always send proxy-connection
	//	printf("proxy-connection\n");
		strcat(buf2server, "Proxy-Connection: close\r\n");
	}

//	if (strcmp(buf, "Accept:")){
//		strcat(buf2server, accept_hdr);
//	}
//	if (strcmp(buf, "Accept-Encoding:")){
//		strcat(buf2server, accept_encoding_hdr);
//	}
	
	
	memset(buf, 0, sizeof(buf));
	strcat(buf2server, "\r\n");
}

void parse_uri(char *uri, char *hostname, int *port){
	char local_uri[MAXLINE];
	char *buf;
	char *endbuf;
	int local_port[10];
	char num[2];

	buf = local_uri; // buffer is initially zero
	for (int i=0; i<10; i++){ // initialize local port as 0
		local_port[i] = 0;
	}
	strncpy(buf, uri, MAXLINE); // copy content of uri to buffer

	endbuf = buf + strlen(buf); // point to where uri string ends
	buf += 7; //start reading after http
	while (buf < endbuf){
		if (buf >= endbuf) break;
		if (*buf == ':'){ // case port number found
			buf++;
			*port = 0;
			int i = 0;
			while (*buf != '/') {
				num[0] = *buf; // get int by single digit value as character
				num[1] = '\0'; // and end with \9
				local_port[i] = atoi(num);
					//what happens if replace with atoi(*buf)?
				buf++;
				i++;
			}
			int j=0;
			while (i > 0){
				*port += local_port[j] * powerten(i-1);
				j++;
				i--;
			}
		}
		if (*buf != '/'){
			sprintf(hostname, "%s%c", hostname, *buf);
		//	strcat(hostname, *buf);	
		} else{
			strcat(hostname, "\0");
			strcpy(uri, buf);
			break;
		}
		buf++;
	}
}

int powerten(int i){
	int num = 1;
	while (i>0){
		num*=10;
		i--;
	}
	return num;
}

// cache related operations
static int load_cache(char *tag, char *response){
	int idx=0;
	int  cnt;
	for (cnt=0; cnt<line_num; cnt++){ // line_num = 10
		if (cache.set[idx].line[cnt].valid==1 && 
			(strcmp(cache.set[idx].line[cnt].tag, tag) == 0)){
			P(&mutex);
			update_use(cache.set[idx].use, cnt, line_num);
			V(&mutex);
			strcpy(response, cache.set[idx].line[cnt].block); //get response from update_use loaded to response
			break;
	//		if (i==line_num) return 0;
//			else return 1;
		}
	}
	if (cnt==line_num) return 0;
//	if (i==line_num) return 0; // read through all cache, and no available cache
	return 1;
}
//save data from server in cache
static void save_cache(char *tag, char *response){
	int idx, tmp;
	idx = 0;
	tmp = cache.set[idx].use[line_num-1]; 
	
	strcpy(cache.set[idx].line[tmp].tag, tag);
	strcpy(cache.set[idx].line[tmp].block, response);

	if (cache.set[idx].line[tmp].valid == 0){
		cache.set[idx].line[tmp].valid = 1;
	}
	update_use(cache.set[idx].use, tmp, line_num);
}

static void update_use(int *cache_use, int current, int len){
	/*
	for (int i=0; i<len; i++){
		if (cache_use[i] == current){ // found currently using cache
			for (int j=i; j>0; j--){ //shift cache use one by one
				cache_use[j] = cache_use[j-1];
			}
		}			
	}*/
	int i, j;
	for (i=0; i<len; i++){
		if (cache_use[i] == current) break;
	}
	for (j=1; j>0; j--){
		cache_use[j] = cache_use[j-1];
	}
	cache_use[0] = current; // set 0th cache to current
}

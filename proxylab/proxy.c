/*
proxy-lab assignment 
By: Kinam Kwon

Progress description: Cache fails for not known reason
Caching seems to work when tested personally, but fails to meet grading requirement
I could not find the difference regarding this part, although the problem seems 
to be related to cache size 

In terms of effort to synchronize cache, semaphore was used.


*/
#include <stdio.h>
#include "csapp.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

//operation related to proxy values
sem_t mutex;
static int block_num;

void *thread(void *vargp);
void run(int fd);
void read_requesthdrs(rio_t *rp);
static void request_hdr(char *buf, char *buf2server, char *hostname);
void parse_uri(char *uri, char *hostname, int *port);

//cache-related functions
static int load_cache(char *uri, char *response);
static void save_cache(char *uri, char *response);
static void update_cache();
//static void update_use(int *cache_use, int current, int len);

struct cache_block{
	int valid; //each line has valid bit, tag, and block
	char *uri; //  = uri
	char *response; // = uri_content
};

struct cache_set{ // cache set with a line
	struct cache_block *cache_block; //initialize with 10 lines
	int using_block; //tracks how many blocks are in use
};

static struct cache_set cache;



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
	socklen_t clientlen;
	struct sockaddr_in clientaddr;
	pthread_t tid;

	//establish proxy and cache related input
	sem_init(&mutex, 0, 1);
	block_num = 10; 
	
	//initialize cache
	cache.using_block = 0;
	cache.cache_block = malloc(sizeof(struct cache_block) * block_num);
	for (int i=0; i<block_num; i++){
		cache.cache_block[i].valid = 0;
		cache.cache_block[i].uri = malloc(MAXLINE);
		cache.cache_block[i].response = malloc(MAX_OBJECT_SIZE);
	}


	listenfd = Open_listenfd(argv[1]); // set given port number as listening
	while (1){
		clientlen = sizeof(clientaddr);
		connfd = malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
		Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
		printf("accepted connection from (%s, %s)\n", hostname, port);
		pthread_create(&tid, NULL, thread, connfd); // set thread to allow concurrent requests
	}

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

	//obtain request and parse
	Rio_readinitb(&rio, connfd);
	if (!Rio_readlineb(&rio, buf, MAXLINE)) return;

	//parse input to three parts (method  = GET). Version should be 1.1
	sscanf(buf, "%s %s %s", method, uri, version);


	if (!strcmp(version, "HTTP/1.1")) { 
		strcpy(version, "HTTP/1.0");
	}


	//method should be get
	if (strcasecmp(method, "GET")){
		return;
	}

	read_requesthdrs(&rio); // read HTTP request, same as tiny.c

	parse_uri(uri, hostname, port); //parse uri.... what's return?


	strcpy(filename, uri);

	sprintf(buf2server, "%s %s %s\r\n", method, filename, version);


	//organize command to send to server (mostly fixed excpet hostname)
	request_hdr(buf, buf2server, hostname);

	// check cache first
	if (load_cache(uri, cache_buf)) {
		if (rio_writen(connfd, cache_buf, sizeof(cache_buf))<0 ){
			fprintf(stderr, "Error: cache load\n");
			return;
		}
		memset(cache_buf, 0, sizeof(cache_buf));
	} else{ // cache miss, forward request to server
		sprintf(port2, "%d", *port);
		if((serverfd = open_clientfd(hostname, port2))<0){ //safe to open server
			fprintf(stderr, "open server fd error\n");
			return;
		}


		Rio_readinitb(&rio_server, serverfd);
		Rio_writen(serverfd, buf2server, strlen(buf2server));

		// receive response from server and save data in cache
		memset(cache_buf, 0, sizeof(cache_buf));
		object_len = 0;
		len = 0;


		//cycle to read response. Similar to textbook
		while ((len = rio_readnb(&rio_server, serverResponse, MAX_OBJECT_SIZE)) >0 ) {
			Rio_writen(connfd, serverResponse, len);

			if (object_len+len < MAX_OBJECT_SIZE){
				void *writeto = (serverResponse + object_len);
				memcpy((char *)writeto, cache_buf, len);
			}
			object_len += len;
			memset(serverResponse, 0, sizeof(serverResponse)); //clear response
		}


		//if result can be saved to cache, work on cache
		if (object_len <= MAX_OBJECT_SIZE){
			P(&mutex); //work on cache
			save_cache(uri, cache_buf);
			V(&mutex);
		} 
		close(serverfd);
	}
}


//read request header, same as tiny
void read_requesthdrs(rio_t *rp){
	char buf[MAXLINE];
	
	Rio_readlineb(rp, buf, MAXLINE);
	while(strcmp(buf, "\r\n")){
		Rio_readlineb(rp, buf, MAXLINE);
	}
	return;
}

//add header to be sent to server.
//Mostly fixed, except for hostname and use_agent_hdr
static void request_hdr(char *buf, char *buf2server, char *hostname)
{
	strcat(buf2server, "Host:" );
	strcat(buf2server, hostname);
	strcat(buf2server, "\r\n");

	strcat(buf2server, user_agent_hdr);
	strcat(buf2server, "Connection: close\r\n");
	strcat(buf2server, "Proxy-Connection: close\r\n");
	memset(buf, 0, sizeof(buf)); //empty buf
	strcat(buf2server, "\r\n");
}

void parse_uri(char *uri, char *hostname, int *port){
	char local_uri[MAXLINE];
	char *buf;
	char *endbuf;
	char num[2];

	buf = local_uri; // buffer is initially zero
	strncpy(buf, uri, MAXLINE); // copy content of uri to buffer

	endbuf = buf + strlen(buf); // point to where uri string ends
	buf += 7; //start reading after http

	while (buf < endbuf){
		if (buf >= endbuf) break;
		if (*buf == ':'){ // case port number found
			buf++;
			*port = 0;
			while (*buf != '/') {
				num[0] = *buf; // get int by single digit value as character
				num[1] = '\0'; // and end with \0 to get atoi (need to be string)
				*port *=10;
				*port+= atoi(num);
				buf++;
			}
		}
		if (*buf != '/'){ // continue adding to host name
			sprintf(hostname, "%s%c", hostname, *buf);
		} else{ //if / is found, the collection ends
			strcat(hostname, "\0");
			strcpy(uri, buf);
			break;
		}
		buf++;
	}
}


// cache related operations
static int load_cache(char *uri, char *response){

	int found = 0;
	int cnt = 0;
	for (; cnt<cache.using_block; ){ // line_num = 10
		if ((strcmp(cache.cache_block[cnt].uri, uri) == 0)){ //match found
			struct cache_block storage = cache.cache_block[cnt];
			strcpy(response, storage.response);
			
			P(&mutex); // operation related to updating cache structure - block extern access
			
			for (int i=cnt; i>0; i--){
				if (cnt == 0) i++;
				cache.cache_block[i] = cache.cache_block[i-1];
			}
			cache.cache_block[0] = storage;

			V(&mutex);
			found = 1;
			break;
		}
		cnt++;
	}



	if (found == 0){
		return 0;
	}
	else{
		 return 1;
	}
}
//save data from server in cache
static void save_cache(char *uri, char *response){
	struct cache_block curr;

	curr.uri = malloc(MAXLINE);
	curr.response = malloc(MAX_OBJECT_SIZE);
	curr.valid = 1;
	strcpy(curr.uri, uri);
	strcpy(curr.response, response);
	

	if (cache.using_block == 0){
		cache.cache_block[0] = curr;
		cache.using_block++;
		return;
	}
	
	int i = cache.using_block-1;
	if (cache.using_block == 10) i--;
	for (; i>=0; i--){
		cache.cache_block[i+1] = cache.cache_block[i];
	}
	//one more cache block is now being used
	if (cache.using_block != 10){
		cache.using_block++;
	}
	cache.cache_block[0] = curr;
	update_cache();
}


static void update_cache(){
	for (int i=cache.using_block-1; i>=1; i--){
		for (int j=i-1; j>=0; j--){
			if (strcmp(cache.cache_block[i].uri, cache.cache_block[j].uri) == 0){
				for (int k=i; k<cache.using_block; k++){
					cache.cache_block[k] = cache.cache_block[k+1];
				}
				
				cache.using_block--;
				return;
			}
		}
	}
}



// same code as tiny.c
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
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

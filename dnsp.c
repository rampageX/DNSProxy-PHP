/*
 * Copyright (c) 2013-2018 Massimiliano Fantuzzi HB3YOE <superfantuz@gmail.com>

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

//#define _GNU_SOURCE
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/timeb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
//#include <spawn.h>
#include <stdarg.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <signal.h>
#include <semaphore.h>
#include <pthread.h>
//#include <omp.h>

#ifndef SIGCLD
#   define SIGCLD SIGCHLD
#endif

#define errExit(msg)		do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define handle_error(msg)	do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define VERSION           "1.5"

/* Stack size for cloned child */
#define STACK_SIZE (1024 * 1024)    
/* DELAY for CURL to wait ? do not remember, needs documentation */
#define DELAY		      0
/* how can it be influenced, this CURL parameter ? kept for historical reasons, will not be useful in threaded model */
#define MAXCONN               4
#define UDP_DATAGRAM_SIZE   256
#define DNSREWRITE          256
#define HTTP_RESPONSE_SIZE 4096
#define URL_SIZE            256
#define DNS_MODE_ANSWER       0
#define DNS_MODE_ERROR        1
#define DEFAULT_LOCAL_PORT   53
#define DEFAULT_WEB_PORT     80
#define DEFAULT_PRX_PORT   1080

/* for threaded model, experimental options, not in use at the moment */
#define NUMT	              2
#define NUM_THREADS           2
#define NUM_HANDLER_THREADS   1

//#define LOC_PREEMPT_DLD     1
//#define TYPEQ	    	    2
//#define DEBUG	    	    0

pthread_key_t glob_var_key_ip;
pthread_key_t glob_var_key_client;
//static pthread_mutex_t mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//pthread_mutex_t mutex = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;
pthread_mutexattr_t MAttr;

#ifdef TLS
__thread int i;
#else
 pthread_key_t key_i;
#endif
 pthread_t *tid;

struct readThreadParams {
    size_t xrequestlen;
    char* xproxy_user;
    char* xproxy_pass;
    char* xproxy_host;
    int xproxy_port;
    char* xlookup_script;
    char* xtypeq;
    int xwport;
    int sockfd;
    int xsockfd;
    //int digit;
    //char* input;
    struct dns_request *xhostname;
    struct sockaddr_in *xclient;
    struct sockaddr_in *yclient;
    struct dns_request *xdns_req;
    struct dns_request *dns_req;
};

struct thread_info {    	/* Used as argument to thread_start() */
    pthread_t thread_id;        /* ID returned by pthread_create() */
    int       thread_num;       /* Application-defined thread # */
    char     *argv_string;      /* From command-line argument */
};

/*
void start_thread(pthread_t *mt)
{
    mystruct *data = malloc(sizeof(*data));
    ...;
    pthread_create(mt, NULL, do_work_son, data);
}
*/

/*
void start_thread(pthread_t *mt)
{
    //mystruct local_data = {};
    //mystruct *data = malloc(sizeof(*data));
    struct readThreadParams local_data = {};
    struct readThreadParams *data = malloc(sizeof(*data));
    *data = local_data;
    //pthread_create(mt, NULL, threadFunc,readParams);
    pthread_create(mt, NULL, thread_start,data);
    //pthread_create(mt, NULL, threadFunc,data);
    //ret = pthread_create(&pth[i],NULL,threadFunc,readParams);
    //pthread_create(&pth[i],NULL,threadFunc,readParams);
}
*/

static void *
thread_start(void *arg)
{
   struct thread_info *tinfo = arg;
   char *uargv, *p;

   printf("Thread %d: top of stack near %p; argv_string=%s\n",
            tinfo->thread_num, &p, tinfo->argv_string);

   uargv = strdup(tinfo->argv_string);
   if (uargv == NULL)
        handle_error("strdup");

   for (p = uargv; *p != '\0'; p++)
        *p = toupper(*p);

   return uargv;
}

//struct thread_data{
//	int threads;
//	int thread_id;
//	int exec; //total number of executions 
//	int max_req_client;
//	int random; //1=yes 0=no whether requests are the max or randomdouble
//	int ssl; //1=yes 0=no
//	int uselogin; //1=yes 0=no
//	char domain[256];
//	char login[256];
//	char password[256];
//};

int DEBUG, DEBUGCURL, LOC_PREEMPT_DLD;
char* substring(char*, int, int);

struct dns_request
{
    uint16_t transaction_id,
             questions_num,
             flags,
             qtype,
             qclass;
    char hostname[256],
             query[256];
    size_t hostname_len;
};  

struct dns_reponse
{
    size_t lenght;
    char *payload;
};

/* usage */
void usage(void)
{
    fprintf(stderr, "\n dnsp %s, copyright @ 2018 Massimiliano Fantuzzi, HB3YOE, MIT License\n\n"
                       " usage: dnsp [-l [local_host]] [-p [local_port:53,5353,..]] [-H [proxy_host]] [-r [proxy_port:8118,8888,3128,9500..]] \n"
		       "\t\t [-w [lookup_port:80,443,..]] [-s [lookup_script]]\n\n"
                       " OPTIONS:\n"
                       "      -l\t\t Local server address	(optional)\n"
                       "      -p\t\t Local server port	(optional, defaults to 53)\n"
                       "      -H\t\t Cache proxy address	(strongly suggested)\n"
                       "      -r\t\t Cache proxy port	(strongly suggested)\n"
                       "      -u\t\t Cache proxy username	(optional)\n"
                       "      -k\t\t Cache proxy password	(optional)\n"
                       "      -s\t\t Lookup script URL	(mandatory option)\n"
                       "      -w\t\t Lookup port		(obsolete, defaults to 80/443 for HTTP/HTTPS)\n"
                       "      -t\t\t Stack size in format	0x1000000 (MB)\n"
                       "\n"
                       " TESTING/DEV OPTIONS:\n"
                       "      -v\t\t Enable DEBUG\n"
                       "      -C\t\t Enable CURL VERBOSE, useful to spot cache issues or dig down into HSTS/HTTPS quirks\n"
                       "      -I\t\t Upgrade Insecure Requests, HSTS work in progress\n"
                       "      -R\t\t Enable CURL resolve mechanism, avoiding extra gethostbyname, work in progress\n"
                       "\n"
		       " Example HTTPS direct :  dnsp -s https://php-dns.appspot.com/\n"
		       " Example HTTP direct  :  dnsp -s http://www.fantuz.net/nslookup.php\n"
                       " Example HTTP w/cache :  dnsp -r 8118 -H http://myproxy.example.com/ -s http://www.fantuz.net/nslookup.php\n\n"
    ,VERSION);
    exit(EXIT_FAILURE);
}

/* Prints an error message and exit */
void error(const char *msg)
{
    fprintf(stderr," *** %s: %s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

/* Prints debug messages */
void debug_msg(const char* fmt, ...)
{
    va_list ap;

    if (DEBUG) {
        fprintf(stdout, " [%d]> ", getpid());
        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap); 
        va_end(ap);
    }
}

/* Return the length of the pointed buffer */
size_t memlen(const char *buff)
{
    size_t len = 0;
    
    while (1) {
        if (buff[len] == 0) break;
        len ++;       
    }

    return len;
}

/* Parses the dns request and returns the pointer to dns_request struct. Returns NULL on errors */
struct dns_request *parse_dns_request(const char *udp_request, size_t request_len)
{
    struct dns_request *dns_req;
    
    dns_req = malloc(sizeof(struct dns_request));

    /* Transaction ID */
    dns_req->transaction_id = (uint8_t)udp_request[1] + (uint16_t)(udp_request[0] << 8);
    udp_request+=2;
    
    /* Flags */
    dns_req->flags = (uint8_t)udp_request[1] + (uint16_t)(udp_request[0] << 8);
    udp_request+=2;

    /* Questions num */
    dns_req->questions_num = (uint8_t)udp_request[1] + (uint16_t)(udp_request[0] << 8); 
    udp_request+=2;

    /* WHERE IS EDNS ?? */

    /* Skipping 6 not interesting bytes, override with shortened answers (one of the initial purpose of DNSP software) */
    /*
       uint16_t Answers number 
       uint16_t Records number 
       uint16_t Additionals records number 
    */

    udp_request+=6;
    
    /* Getting the dns query */
    bzero(dns_req->query, sizeof(dns_req->query));
    memcpy(dns_req->query, udp_request, sizeof(dns_req->query)-1);
    
    /* Hostname */
    bzero(dns_req->hostname, sizeof(dns_req->hostname));
    dns_req->hostname_len = 0;
    while (1) {
        uint8_t len = udp_request[0]; /* Length of the next label */
        if (len == 0) {
            udp_request++;
            break;
        }
        udp_request++;
        if (dns_req->hostname_len + len >=  sizeof(dns_req->hostname)) {
            free(dns_req);
	    printf("CORE: size issue !!\n");
            return NULL;
        }
        strncat(dns_req->hostname, udp_request, len); /* Append the current label to dns_req->hostname */
        strncat(dns_req->hostname, ".", 1); /* Append a '.' */
        dns_req->hostname_len+=len+1;
        udp_request+=len;
    }

    /* Qtype */
    dns_req->qtype = (uint8_t)udp_request[1] + (uint16_t)(udp_request[0] << 8); 
    udp_request+=2;

    /* Qclass */
    dns_req->qclass = (uint8_t)udp_request[1] + (uint16_t)(udp_request[0] << 8); 
    udp_request+=2;
        
    return dns_req;
}

/*  * Builds and sends the dns response datagram */
void build_dns_response(int sd, struct sockaddr_in *yclient, struct dns_request *dns_req, const char *ip, int mode, size_t xrequestlen)
{
    char *rip = malloc(256 * sizeof(char));
    //struct dns_request *dns_req;
    //struct sockaddr_in *client;
    //char *str, *arg;
    //struct readThreadParams *params = malloc(sizeof(struct readThreadParams));
    //struct readThreadParams *params = (struct readThreadParams*)arg;
    //str=(char*)arg;
    
    int sockfd; // = params->xsockfd;
    int xsockfd; // = params->xsockfd;
    //struct dns_request *xhostname = (struct dns_request *)xhostname->hostname;
    char *response,
	 *qhostname, // = dns_req->hostname,
         *token,
         *pch,
	 *maxim,
         *response_ptr;
	 //*ppch,
	 //*typeq,
    //uint i,ppch;
    int i,ppch;
    ssize_t bytes_sent;


    if (DEBUG) {
	    /*
	    printf("BUILD-xhostname-int				: %u\n", (uint32_t)strlen(xhostname));
	    printf("BUILD-req-query				: %s\n", dns_req->query);
	    */
	    
	    printf("BUILD-yclient->sin_addr.s_addr		: %u\n", (uint32_t)(yclient->sin_addr).s_addr);
	    printf("BUILD-yclient->sin_port			: %u\n", (uint32_t)(yclient->sin_port));
	    printf("BUILD-yclient->sin_family		: %d\n", (uint32_t)(yclient->sin_family));
	    printf("BUILD-xrequestlen			: %d\n", (uint32_t)(xrequestlen));
	    printf("BUILD-xsockfd				: %u\n", xsockfd);
	    printf("BUILD-sockfd				: %d\n", sockfd);
	    printf("BUILD-hostname				: %s\n", dns_req->hostname);
	
	    /*
	    printf("BUILD-client->sa_family			: %u\n", (struct sockaddr)&xclient->sa_family);
	    printf("BUILD-client->sa_data			: %u\n", (uint32_t)client->sa_data);
	    printf("BUILD-hostname				: %s\n", qhostname);
	    printf("build-qry = %s\n",(xdns_req->query));
    	    printf("build-host %s\n",(char *)(xdns_req->hostname));
    	    printf("build-answer %s\n", rip);
    	    printf("build-answer-mode %d\n", DNS_MODE_ANSWER);
	    */
	    printf("\n");
    }

    response = malloc (UDP_DATAGRAM_SIZE);
    bzero(response, UDP_DATAGRAM_SIZE);

    maxim = malloc (DNSREWRITE);
    bzero(maxim, DNSREWRITE);

    //maxim_ptr = maxim;
    response_ptr = response;

    /* Transaction ID */
    response[0] = (uint8_t)(dns_req->transaction_id >> 8);
    response[1] = (uint8_t)dns_req->transaction_id;
    response+=2;
    /*

    TXT, SRV, SOA, PTR, NS, MX, DS, DNSKEY, AAAA, A, unused
    
    A IPv4 host address 0x0001
    AAAA IPv6 host address 0x001c
    NS authoritative name server 0x0002
    CNAME alias canonical name 0x0005
    SOA start of zone authority 0x0006
    PTR Domain name pointer 0x000c
    HINFO host info 0x000d
    MINFO mailbox/mail list info 0x000e
    MX mail exchange 0x000f
    AXFR zone transfer 0x00fc 
    
    */

    if (mode == DNS_MODE_ANSWER) {
        /* Default flags for a standard query (0x8580) */
	/* Shall it be authoritative answer... or not ? :) */

        //response[0] = 0x81;
        response[0] = 0x85;
        response[1] = 0x80;
        response+=2;
        /* Questions 1 */
        response[0] = 0x00;
        response[1] = 0x01;
        response+=2;
        /* Answers 1 */
        response[0] = 0x00;
        response[1] = 0x01;
        response+=2;
        
    } else if (mode == DNS_MODE_ERROR) {
        
	/* DNS_MODE_ERROR should truncate message instead of building it up ...  */

        /* Server failure (0x8182), but what if we want NXDOMAIN (0x....) ???*/
	    
	/*
	     * NOERROR (RCODE:0) : DNS Query completed successfully
	     * FORMERR (RCODE:1) : DNS Query Format Error
	     * SERVFAIL (RCODE:2) : Server failed to complete the DNS request
	     * NXDOMAIN (RCODE:3) : Domain name does not exist
	     * NOTIMP (RCODE:4) : Function not implemented
	     * REFUSED (RCODE:5) : The server refused to answer for the query
	     * YXDOMAIN (RCODE:6) : Name that should not exist, does exist
	     * XRRSET (RCODE:7) : RRset that should not exist, does exist
	     * NOTAUTH (RCODE:9) : Server not authoritative for the zone
	     * NOTZONE (RCODE:10) : Name not in zone
	     * 11-15           available for assignment
	     * 16    BADVERS   Bad OPT Version             
	     * 16    BADSIG    TSIG Signature Failure      
	     * 17    BADKEY    Key not recognized          
	     * 18    BADTIME   Signature out of time window
	     * 19    BADMODE   Bad TKEY Mode               
	     * 20    BADNAME   Duplicate key name          
	     * 21    BADALG    Algorithm not supported     
	     * 22-3840         available for assignment
	     *   0x0016-0x0F00
	     * 3841-4095       Private Use
	     *   0x0F01-0x0FFF
	     * 4096-65535      available for assignment
	     *   0x1000-0xFFFF
	*/

        response[0] = 0x81;
        response[1] = 0x82;
        response+=2;
        /* Questions 1 */
        response[0] = 0x00;
        response[1] = 0x01;
        response+=2;
        /* Answers 0 */
        response[0] = 0x00;
        response[1] = 0x00;
        response+=2;

	/* Are we into "No such name" ?... just an NXDOMAIN ?? */ 
	//*response++ = 0x00;
    }
    
    /* Authority RRs 0 */
    response[0] = 0x00;
    response[1] = 0x00;
    response+=2;
    
    /* Additional RRs 0 */
    response[0] = 0x00;
    response[1] = 0x00;
    response+=2;

    /* Query */
    strncat(response, dns_req->query, dns_req->hostname_len);
    response+=dns_req->hostname_len+1;
    
    /* Type */
    response[0] = (uint8_t)(dns_req->qtype >> 8);
    response[1] = (uint8_t)dns_req->qtype;
    response+=2;
    
    /* Class */
    response[0] = (uint8_t)(dns_req->qclass >> 8);
    response[1] = (uint8_t)dns_req->qclass;
    response+=2;

    /* Answer */
    if (mode == DNS_MODE_ANSWER) {
        /* Pointer to host name in query section */
        response[0] = 0xc0;
        response[1] = 0x0c;
        response+=2;
        
	if (dns_req->qtype == 0x0f) {
		// MX
        	response[0] = 0x00;
	        response[1] = 0x0f;
        	response+=2;
	} else if (dns_req->qtype == 0xFF) {
		// ALL
        	response[0] = 0x00;
	        response[1] = 0xFF;
        	response+=2;
	} else if (dns_req->qtype == 0x01) {
		// A
		*response++ = 0x00;
		*response++ = 0x01;
	} else if (dns_req->qtype == 0x05) {
		// CNAME
        	response[0] = 0x00;
	        response[1] = 0x05;
        	response+=2;
	} else if (dns_req->qtype == 0x0c) {
		// PTR
        	response[0] = 0x00;
	        response[1] = 0x0c;
        	response+=2;
	} else if (dns_req->qtype == 0x02) {
		// NS
        	response[0] = 0x00;
	        response[1] = 0x02;
        	response+=2;
	} else {
		//return;
	}
        
        /* Class IN */
	*response++ = 0x00;
	*response++ = 0x01;

       	/* TTL (4 hours) */
	*response++ = 0x00;
	*response++ = 0x00;
	*response++ = 0x38;
	*response++ = 0x40;
	
	/*
	 * 0x08 - backspace	\010 octal
	 * 0x09 - horizontal tab
	 * 0x0a - linefeed
	 * 0x0b - vertical tab	\013 octal
	 * 0x0c - form feed
	 * 0x0d - carriage return
	 * 0x20 - space
	*/ 
	
	/* TYPES */
	if (dns_req->qtype == 0x0c) {
        	// PTR
	        /* Data length (4 bytes)*/
	        response[0] = 0x00;
	        response[1] = 0x04;
	        response+=2;
		response[0] = 0xc0;
		response[1] = 0x0c;
	       	response+=2;

	} else if (dns_req->qtype == 0x02) { 
		// NS
	        response[0] = 0x00;
		response[1] = (strlen(ip)-1);
        	response+=2;

		pch = strtok((char *)ip,". \r\n\t");

		while (pch != NULL)
		{
			ppch = strlen(pch);
			*response++ = strlen(pch);
			for (i = 0; i < strlen(pch); ++i) {
				*response++ = pch[i];
				maxim[i] = pch[i];
			}

    			pch = strtok(NULL, ". ");
			
			//if (pch != ' ' && pch != '\t' && pch != NULL) {
			//if (pch == ' ' || pch == '\t' || pch == NULL || pch == '\n' || pch == '\r') {
			if (pch == NULL) {
				for (i = 0; i < ppch+1; ++i) {
					response--;
				}
                                *response++ = ppch-3;
				for (i = 0; i < ppch-3; ++i) {
        	                	*response++ = maxim[i];
                	        }
				
			}
		}

		*response++ = 0x00;

	} else if (dns_req->qtype == 0x05) {
	       	// CNAME
        	response[0] = 0x00;
		response[1] = (strlen(ip)-1);
        	response+=2;

		pch = strtok((char *)ip,". \r\n\t");

		while (pch != NULL)
		{
			ppch = strlen(pch);
			*response++ = strlen(pch);
			for (i = 0; i < strlen(pch); ++i) {
				*response++ = pch[i];
				maxim[i] = pch[i];
			}

    			pch = strtok (NULL, ". ");
			
			if (pch == NULL) {
				for (i = 0; i < ppch+1; ++i) {
					response--;
				}
                                *response++ = ppch-3;
	                        for (i = 0; i < ppch-3; ++i) {
        	                	*response++ = maxim[i];
                	        }
			}
			
		}

		*response++ = 0x00;

	} else if (dns_req->qtype == 0x0f) {
	       	//MX RECORD
	        /* Data length accounting for answer plus final dot and termination field */
        	response[0] = 0x00;
		response[1] = (strlen(ip)+3);
        	response+=2;

	        /* PRIO (4 bytes)*/
		response[0] = 0x00;
		response[1] = 0x0a;
        	response+=2;

	        /* POINTER, IF YOU ARE SO BRAVE OR ABLE TO USE IT (4 bytes) -> do not use label-mode then...
		 * in that case, you should re-write the code to have super-duper minimal responses.
		 * That code would also need to implement domain comparison to check if suffix can be appended */

		//response[0] = 0xc0;
		//response[1] = 0x0c;
        	//response+=2;

		pch = strtok((char *)ip,".\r\n\t");
		while (pch != NULL)
		{
			//maxim = NULL;
			ppch = strlen(pch);
			*response++ = strlen(pch);
			for (i = 0; i < strlen(pch); ++i) {
				*response++ = pch[i];
				//maxim[0] += 0x00;
				maxim[i] = pch[i];
			}
			//strcat(response, pch);
			//*response++ = *maxim;
    			pch = strtok (NULL, ".");
			if (pch == NULL) {
				for (i = 0; i < ppch+1; ++i) {
					response--;
				}
                                *response++ = ppch-1;
	                        for (i = 0; i < ppch-1; ++i) {
        	                	*response++ = maxim[i];
                	        }
			}
		}

		*response++ = 0x00;
		
	} else if (dns_req->qtype == 0x01) { // A RECORD 

	        /* Data length (4 bytes)*/
		*response++ = 0x00;
		*response++ = 0x04;

        	token = strtok((char *)ip,".");
        	if (token != NULL) response[0] = atoi(token);
        	else return;

        	token = strtok(NULL,".");
        	if (token != NULL) response[1] = atoi(token);
        	else return;

        	token = strtok(NULL,".");
        	if (token != NULL) response[2] = atoi(token);
        	else return;

        	token = strtok(NULL,".");
        	if (token != NULL) response[3] = atoi(token);
        	else return;

		response+=4;
		
      	} else {
        	fprintf(stdout, "DNS_MODE_ISSUE\n");
		//return;
	}
	
	//*response++=(unsigned char)(strlen(ip)+1); //memcpy(response,ip,strlen(ip)-1); //strncpy(response,ip,strlen(ip)-1);
	//recvfrom(3, "\326`\1 \0\1\0\0\0\0\0\1\6google\2it\0\0\1\0\1\0\0)\20\0"..., 256, 0, {sa_family=AF_INET, sin_port=htons(48379), sin_addr=inet_addr("192.168.2.84")}, [16]) = 38
	//(3, "\24\0\0\0\26\0\1\3\23\306;U\0\0\0\0\0\0\0\0", 20, 0, {sa_family=AF_NETLINK, pid=0, groups=00000000}, 12) = 20
	//(3, "z\271\205\200\0\1\0\1\0\0\0\0\6google\2hr\0\0\2\0\1\300\f\0\2\0"..., 41, 0, {sa_family=0x1a70 /* AF_??? */, sa_data="s\334\376\177\0\0\20D\1\270\223\177\0\0"}, 8)
	
	//(struct sockaddr *)xclient->sin_family = AF_INET;
	int yclient_len = sizeof(yclient);
        yclient->sin_family = AF_INET;
	//yclient->sin_addr.s_addr = inet_addr("192.168.2.84"); 
	//yclient->sin_port = htons(yclient->sin_port);
	yclient->sin_port = yclient->sin_port;
	memset(&(yclient->sin_zero), 0, sizeof(yclient->sin_zero)); // zero the rest of the struct 
	//memset(yclient, 0, 0);

    	if (DEBUG) {
	    	printf("INSIDE-raw-datagram			: %s\n", response);
		printf("INSIDE-yclient->sin_addr.s_addr        	: %u\n", (uint32_t)(yclient->sin_addr).s_addr);
		printf("INSIDE-yclient->sin_port               	: %u\n", (uint32_t)(yclient->sin_port));
		printf("INSIDE-yclient->sin_port               	: %u\n", htons(yclient->sin_port));
		printf("INSIDE-yclient->sin_family		: %d\n", (uint32_t)(yclient->sin_family));
		printf("INSIDE-dns-req->hostname		: %s\n", dns_req->hostname);
		printf("INSIDE-xrequestlen			: %u\n", (uint16_t)xrequestlen);
		/*
		printf("INSIDE-dns_req->query			: %s\n", dns_req->query);
		printf("INSIDE-xdns_req->query			: %s\n", xdns_req->query);
		printf("INSIDE-xdns_req->hostname-to-char		: %s\n", (char *)(xdns_req->hostname));
		printf("INSIDE-xdns_req->hostname			: %s\n", xdns_req->hostname);
		printf("INSIDE-xdns_req->query			: %s\n", xdns_req->query);
		*/
		printf("\n");	
	}
        
	/* example kept for educational purposes, to show how the response packet is built, tailored for the client */
	//bytes_sent = sendto(3, "\270\204\205\200\0\1\0\1\0\0\0\0\6google\2jp\0\0\1\0\1\300\f\0\1\0"..., 43, 0, {sa_family=0x0002 /* AF_??? */, sa_data="\365\366\374\177\0\0\1\0\0\0\3\0\0\0"}, 16)
	//bytes_sent = sendto(sd, response_ptr, response - response_ptr, 0, (struct sockaddr *)(&yclient), sizeof(yclient));
        bytes_sent = sendto(sd, response_ptr, response - response_ptr, 0, (struct sockaddr *)yclient, 16);

    } else if (mode == DNS_MODE_ERROR) {
        fprintf(stdout, "DNS_MODE_ERROR\n");
	//(struct sockaddr *)xclient->sin_family = AF_INET;
	int yclient_len = sizeof(yclient);
        yclient->sin_family = AF_INET;
	//yclient->sin_addr.s_addr = inet_addr("192.168.2.84"); 
	//yclient->sin_port = htons(yclient->sin_port);
	yclient->sin_port = yclient->sin_port;
	memset(&(yclient->sin_zero), 0, sizeof(yclient->sin_zero)); // zero the rest of the struct 
	//memset(yclient, 0, 0);
        bytes_sent = sendto(sd, response_ptr, response - response_ptr, 0, (struct sockaddr *)yclient, 16);
    } else {
        fprintf(stdout, "DNS_MODE_UNKNOWN\n");
        bytes_sent = sendto(sd, response_ptr, response - response_ptr, 0, (struct sockaddr *)yclient, 16);
    }

    /* DNS PACKET/VOLUME DISPLAY */
    if (DEBUG) {
	printf("SENT %d bytes\n", (uint32_t)bytes_sent);
    }
    
    fdatasync(sd);
    close(sd);
    free(response_ptr);
    free(dns_req);
    //free(ip);
}

/* new */
struct MemoryStruct {
  char *memory;
  size_t size;
};
 
/* new libCurl callback */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;
 
  mem->memory = realloc(mem->memory, mem->size + realsize + 1);
  if(mem->memory == NULL) {
    /* out of memory! */ 
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }
 
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
 
  return realsize;
}

/* libCurl write data callback */
static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream)
{
  size_t stream_size;
  stream_size = size * nmemb + 1;
  bzero(stream, HTTP_RESPONSE_SIZE);
  memcpy(stream, ptr, stream_size);
  //return 0;
  return stream_size-1;
}

char *substring(char *string, int position, int length) 
{
   char *pointer;
   int c;
 
   pointer = malloc(length+1);
 
   if (pointer == NULL)
   {
      printf("Unable to allocate memory.\n");
      exit(1);
   }
 
   for (c = 0 ; c < length ; c++)
   {
      *(pointer+c) = *(string+position-1);      
      string++;   
   }
 
   *(pointer+c) = '\0';
 
   return pointer;
}

/* Hostname lookup -> OK: Resolved IP, KO: Null */
char *lookup_host(const char *host, const char *proxy_host, unsigned int proxy_port, const char *proxy_user, const char *proxy_pass, const char *lookup_script, const char *typeq, unsigned int wport)
{
    CURL *ch;
    CURLSH *curlsh;
    //CURLcode res;

    char *http_response,
         *script_url,
	 *pointer;
         //*proxy_url,
    char base[2];

    int ret;
    //struct curl_slist *hosting = NULL;
    struct curl_slist *list = NULL;
    //struct MemoryStruct chunk;
    //chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
    //chunk.size = 0;    /* no data at this point */ 

    script_url = malloc(URL_SIZE);
    http_response = malloc(HTTP_RESPONSE_SIZE);
    bzero(script_url, URL_SIZE);
    snprintf(script_url, URL_SIZE-1, "%s?host=%s&type=%s", lookup_script, host, typeq);

    //proxy_url = malloc(URL_SIZE);
    //bzero(proxy_url, URL_SIZE);
    //snprintf(proxy_url, URL_SIZE-1, "http://%s/", proxy_host);

    /*
    if (proxy_host != NULL) {
        fprintf(stderr, "Required substring is \"%s\"\n", proxy_url);
    }
    */
    
    //printf(http_response);

    /* HTTPS DETECTION CODE ... might be better :) */
    pointer = substring(script_url, 5, 1);
    strcpy(base, "s");

    int result = strcmp(pointer, base);
    //printf("Required substring is \"%s\"\n", pointer);
    //printf("Compared substring is \"%s\"\n", base);
    //printf("Result is \"%d\"\n", result);

    if(result == 0) {
	    wport=443;
    } else {
	    printf(" *** HTTP does NOT guarantee against MITM attacks. Consider switching to HTTPS webservice\n");
	    wport=80;
    }

    free(pointer);
  
    /* curl setup */
    ch = curl_easy_init();

    /* to add soon */
    /*
	CURLOPT_MAXREDIRS, 2
	CURLOPT_COOKIEJAR, "cookies.txt"
	CURLOPT_COOKIEFILE, "cookies.txt"
	CURLOPT_ENCODING, "gzip"			check, is DEFLATE mostly
	CURLOPT_TIMEOUT, 5				choosen 5 as value
	CURLOPT_HEADER, 1
    */ 

    curl_easy_setopt(ch, CURLOPT_URL, script_url);
    curl_easy_setopt(ch, CURLOPT_PORT, wport); /* 80, 443 */

    /* Proxy configuration section */
    //curl_easy_setopt(ch, CURLOPT_PROXY, proxy_host);
    //curl_easy_setopt(ch, CURLOPT_PROXY, "http://127.0.0.1/");
    //curl_easy_setopt(ch, CURLOPT_PROXYPORT, 3128);	/* 1080, 8118, 8888, 9500, ... */
    curl_easy_setopt(ch, CURLOPT_PROXY, proxy_host);
    curl_easy_setopt(ch, CURLOPT_PROXYPORT, proxy_port);	/* 1080, 3128, 8118, 8888, 9500, ... */
    curl_easy_setopt(ch, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

    /* optional proxy username and password */
    if ((proxy_user != NULL) && (proxy_pass != NULL)) {
        curl_easy_setopt(ch, CURLOPT_PROXYUSERNAME, proxy_user);
        curl_easy_setopt(ch, CURLOPT_PROXYPASSWORD, proxy_pass);
    }

    curl_easy_setopt(ch, CURLOPT_MAXCONNECTS, MAXCONN);
    //curl_easy_setopt(ch, CURLOPT_FRESH_CONNECT, 0);
    curl_easy_setopt(ch, CURLOPT_FRESH_CONNECT, 0);
    curl_easy_setopt(ch, CURLOPT_FORBID_REUSE, 0);
    //curl_setopt($curl, CURLOPT_AUTOREFERER, 1);

    /* send all data to this function */
    //curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, write_data);

    /* we pass our 'chunk' struct to the callback function */ 
    //curl_easy_setopt(ch, CURLOPT_WRITEDATA, (void *)&chunk);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, http_response);
    
    /* cache with HTTP/1.1 304 Not Modified */

    /* OPTION --> FOLLOW-LOCATION, necessary if getting HTTP 301 */
    // HTTP/1.1 301 Moved Permanently
    // Location: http://www.example.org/index.asp
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);
    //LOC_PREEMPT_DLD

    // LOAD YOUR CA/CERT HERE
    //static const char *pCertFile = "testcert.pem";
    //static const char *pCACertFile="fantuznet.pem";
    //static const char *pHeaderFile = "dumpit";

    //curl_easy_setopt(ch, CURLOPT_CAINFO, pCACertFile);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 2L);;
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYSTATUS, 0L);
    //curl_easy_setopt(ch, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT); //CURL_SSLVERSION_TLSv1

    curl_easy_setopt(ch, CURLOPT_TIMEOUT, 3);
    curl_easy_setopt(ch, CURLOPT_DNS_CACHE_TIMEOUT, 3600);
    curl_easy_setopt(ch, CURLOPT_DNS_USE_GLOBAL_CACHE, 1);	/* DNS CACHE  */
    curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);		/* No progress meter */

    /* disable Nagle with 0, for bigger packets (full MSS) */
    curl_easy_setopt(ch, CURLOPT_TCP_NODELAY, 1L);

    if (DEBUGCURL) {
	    curl_easy_setopt(ch, CURLOPT_VERBOSE,  1);			/* Verbose ON  */
    } else {
	    curl_easy_setopt(ch, CURLOPT_VERBOSE,  0);			/* Verbose OFF */
    }

    /* test for HTTP */
    //curl -s -H "Host: www.fantuz.net" -H "Remote Address:104.27.133.199:80" -H "User-Agent:Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/42.0.2311.135 Safari/537.36" 'http://www.fantuz.net/nslookup.php?host=fantuz.net&type=NS' | xxd
    //curl -s -H "Host: php-dns.appspot.com" -H "User-Agent:Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/42.0.2311.135 Safari/537.36" 'http://php-dns.appspot.com/helloworld.php?host=fantuz.net&type=NS' | xxd

    /* polipo likes pipelining, reuse, pretty powerful */
    /* how about squid/nginx et al. ?? */
    /* anyway all will change with H2 */

    /* PROXY HOST INPUT VALIDATION ... to be improved for sanitisation */
    /*
    if (DEBUG) {
	printf(" *** Fetching cache from HTTP proxy at http://%s:%d\n", proxy_host, proxy_port);
	//printf(" *** Fetching cache from HTTP proxy at %s:%d\n", proxy_url);
    }
    */

    /* OVERRIDE RESOLVER --> add resolver CURL header, work in progress */
    // in the form of CLI --resolve my.site.com:80:1.2.3.4, -H "Host: my.site.com"

    /* OPTIONAL HEADERS, set with curl_slist_append */
     
    //list = curl_slist_append(list, "Request URL: http://www.fantuz.net/nslookup.php");
    //list = curl_slist_append(list, "Remote Address: 217.114.216.51:80");
    //list = curl_slist_append(list, "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/42.0.2311.135 Safari/537.36");
    //curl_easy_setopt(ch, CURLOPT_HTTPHEADER, list);

    //hosting = curl_slist_append(hosting, "www.fantuz.net:80:217.114.216.51");
    //curl_easy_setopt(ch, CURLOPT_RESOLVE, hosting);

    //list = curl_slist_append(list, "Request URL:http://www.fantuz.net/nslookup.php?host=news.google.fr.&type=A");
    //list = curl_slist_append(list, "Request URL:http://www.fantuz.net/nslookup.php");
    //list = curl_slist_append(list, "Request Method:GET");
    //list = curl_slist_append(list, "Remote Address: 217.114.216.51:443");

    list = curl_slist_append(list, "Accept:text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,image/apng,*/*;q=0.8");
    list = curl_slist_append(list, "Accept-Encoding:deflate");
    //list = curl_slist_append(list, "Accept-Encoding:gzip, deflate, sdch");
    list = curl_slist_append(list, "Accept-Language:en-US,en;q=0.8,fr;q=0.6,it;q=0.4");
    list = curl_slist_append(list, "Cache-Control:max-age=300");
    list = curl_slist_append(list, "Connection:keep-alive");

    //list = curl_slist_append(list, "Host:XYZ");
    //list = curl_slist_append(list, "If-None-Match: *");
    //list = curl_slist_append(list, "Accept:text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");

    //list = curl_slist_append(list, "Host: www.fantuz.net");
    //list = curl_slist_append(list, "User-Agent:Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/42.0.2311.135 Safari/537.36");
    //list = curl_slist_append(list, "User-Agent:Mozilla/5.0 (Macintosh; Intel Mac OS X 10_11_4) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/50.0.2661.94 Safari/537.36");
    list = curl_slist_append(list, "Upgrade-Insecure-Requests:1");
    list = curl_slist_append(list, "User-Agent:Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Ubuntu Chromium/64.0.3282.167 Chrome/64.0.3282.167 Safari/537.36");

    /*
Cookie:__cfduid=d5e9da110544f36f002a7d6ff3e7d96951518545176; gsScrollPos-1505=0; gsScrollPos-1568=0; __utmc=47044144; __utmz=47044144.1519169656.1.1.utmcsr=(direct)|utmccn=(direct)|utmcmd=(none); __utma=47044144.1144654312.1519169656.1519498523.1519564542.5; gsScrollPos-2056=; gsScrollPos-2122=

If-Modified-Since:Fri, 02 Mar 2018 13:25:57 GMT
If-None-Match:352d3e68703dce365ec4cda53f420f4a
Upgrade-Insecure-Requests:1
User-Agent:Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Ubuntu Chromium/64.0.3282.167 Chrome/64.0.3282.167 Safari/537.36
    */
    curl_easy_setopt(ch, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(ch, CURLOPT_HEADER, 1L);

    /* try to see if it works .. not sure anymore */
    //curl_easy_setopt(ch, CURLINFO_HEADER_OUT, "" );

    /*
    // CURL_LOCK_DATA_SHARE, quite advanced and criptic
    curlsh = curl_share_init();
    curl_easy_setopt(ch, CURLOPT_SHARE, curlsh);
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
    curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS); 
    */

    ret = curl_easy_perform(ch);
    /* get it! */
    //res = curl_easy_perform(ch);

    /* check for errors */ 
    /*
    if(res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n",
              curl_easy_strerror(res));
    } else {
      printf("%lu bytes retrieved\n", (long)chunk.size);
    }
    */
    /*
     * Now, our chunk.memory points to a memory block that is chunk.size
     * bytes big and contains the remote file.
     *
     * Do something nice with it!
     */ 
   
    /* Problem in performing the http request ?? */
    if (ret < 0) {
        debug_msg ("Error performing HTTP request (Error %d) - spot on !!!\n");
        printf("Error performing HTTP request (Error %d) - spot on !!!\n",ret);
        curl_easy_cleanup(ch);
	//curl_share_cleanup(curlsh);
        free(script_url);
	curl_slist_free_all(list);
	//curl_slist_free_all(hosting);
        return NULL;
    }
   
    /* Can't resolve host or packet too big (up it to 4096) */
    if ((strlen(http_response) > 512) || (strncmp(http_response, "0.0.0.0", 7) == 0)) {
	// insert error answers here, as NXDOMAIN, SERVFAIL etc
        printf("CORE: MALFORMED DNS, possible SERVFAIL from origin... \n");
	printf("from %s\n",script_url);
	//curl_slist_free_all(hosting);
    	printf("Here inside curl, %s\n",http_response);
        http_response = "0.0.0.0";
        //return NULL;
        return http_response;
    }
   
    if (DEBUG) {
        printf("[%s]",http_response);
    }

    curl_easy_cleanup(ch);
    free(script_url);
    //free(proxy_url);
    //free(chunk.memory);
    /* we're done with libcurl, so clean it up */ 
    curl_global_cleanup();
    curl_slist_free_all(list);
    //curl_slist_free_all(hosting);
    return http_response;
}

/* This is our thread function.  It is like main(), but for a thread */
void *threadFunc(void *arg)
{
	struct readThreadParams *params = (struct readThreadParams*)arg;

	struct dns_request *xdns_req = (struct dns_request *)params->xhostname;
	struct sockaddr_in *yclient = (struct sockaddr_in *)params->yclient;
	//struct sockaddr_in *xclient = (struct sockaddr_in *)params->xclient;
        struct dns_request *dns_req = malloc(sizeof(struct dns_request));
	struct dns_request *xhostname = (struct dns_request *)params->xhostname;
	size_t request_len = params->xrequestlen;
	//char *str;
	//int test = params->digit;
	//char* data = params->input;

	int wport = params->xwport;

	int proxy_port_t = params->xproxy_port;
	char* proxy_host_t = params->xproxy_host;
	char* proxy_user_t = params->xproxy_user;
	char* proxy_pass_t = params->xproxy_pass;

	char* lookup_script = params->xlookup_script;

	char* typeq = params->xtypeq;
	int xsockfd = params->xsockfd;
	int sockfd = params->sockfd;

	int ret;
	char *rip = malloc(256 * sizeof(char));
	char *ip = NULL;
	char *yhostname = (char *)params->xhostname->hostname;

	pthread_key_t key_i;
        pthread_key_create(&key_i, NULL);
	//str=(char*)arg;

	/* shall I use trylock or lock ? */
	//if (pthread_mutex_lock(&mutex)) {
	if (pthread_mutex_trylock(&mutex)) {
	    if (DEBUG) {
	    	printf("init lock OK ... \n");
	    }
	} else {
	    if (DEBUG) {
	        printf("init lock NOT OK ... \n");
	    }
	}

    	if (DEBUG) {
		//char *p = &xclient->sin_addr.s_addr;
		char *s = inet_ntoa(yclient->sin_addr);
		printf("params->xhostname->hostname		: %s\n",(char *)params->xhostname->hostname);
		printf("params->xdns_req->hostname		: %s\n",(char *)params->xdns_req->hostname);
		printf("xdns_req->hostname			: %s\n",(char *)xdns_req->hostname);
		printf("VARIABLE sin_addr			: %d\n", (uint32_t)(yclient->sin_addr).s_addr);
		printf("VARIABLE sin_addr human-readable	: %s\n", s);
		printf("VARIABLE script				: %s\n", lookup_script);
		//printf("VARIABLE proxy-host			: %s\n", proxy_host_t);
		//printf("VARIABLE proxy-port			: %s\n", proxy_port_t);
		//printf("VARIABLE proxy-host			: %d\n", (char *)params->xproxy_host);
		//printf("VARIABLE proxy-port			: %d\n", (char *)params->xproxy_port);
		printf("VARIABLE yhostname			: %s\n", yhostname);
		printf("\n");
	}
	
        rip = lookup_host(yhostname, proxy_host_t, proxy_port_t, proxy_user_t, proxy_pass_t, lookup_script, typeq, wport);

	/* PTHREAD SET SPECIFIC GLOBAL VARIABLE ... */
	pthread_setspecific(glob_var_key_ip, rip);

	if (DEBUG) {	
		printf("\nTHREAD CURL-CODE			: %d", ret);
		printf("\nTHREAD CURL-RESULT			: [%s]\n", rip);
		//pthread_setspecific(glob_var_key_ip, rip);
		//pthread_getspecific(glob_var_key_ip);
		//printf("VARIABLE-RET-HTTP-GLOBAL: %x\n", glob_var_key_ip);
		//printf("VARIABLE-HTTP: %x\n", pthread_getspecific(glob_var_key_ip));
		//printf("build: %s", inet_ntop(AF_INET, &ip_header->saddr, ipbuf, sizeof(ipbuf)));
	}

	if ((rip != NULL) && (strncmp(rip, "0.0.0.0", 7) != 0)) {
	    if (DEBUG) {
		printf("THREAD-V-ret				: [%d]\n",ret);
		printf("THREAD-V-type				: %d\n", dns_req->qtype);
		printf("THREAD-V-type				: %s\n", typeq);
		printf("THREAD-V-size				: %u\n", (uint32_t)request_len);
		printf("THREAD-V-socket-sockfd			: %u\n", sockfd);
		printf("THREAD-V-socket-xsockfd-u		: %u\n", xsockfd);
		printf("THREAD-V-socket-xsockfd-d		: %d\n", xsockfd);
		printf("THREAD-V-MODE-ANSWER			: %d\n", DNS_MODE_ANSWER);
		printf("THREAD-V-xclient->sin_addr.s_addr	: %u\n", (uint32_t)(yclient->sin_addr).s_addr);
		printf("THREAD-V-xclient->sin_port		: %u\n", (uint32_t)(yclient->sin_port));
		printf("THREAD-V-xclient->sin_family		: %u\n", (uint32_t)(yclient->sin_family));
		printf("THREAD-V-answer				: [%s]\n", rip);
		/*
		printf("THREAD-V-proxy-host			: %s\n", params->xproxy_host);
		printf("THREAD-V-proxy-port			: %d\n", params->xproxy_port);
		printf("THREAD-V-proxy-host			: %s\n", proxy_host_t);
		printf("THREAD-V-proxy-port			: %d\n", proxy_port_t);
		*/
		printf("\n");

		/*
		printf("THREAD-V-xhostname				: %s\n", yhostname);
		printf("THREAD-V-dns-req->hostname			: %s\n", dns_req->hostname);
		printf("THREAD-V-dns_req->query			: %s\n", dns_req->query);
		printf("THREAD-V-dns_req->query			: %s\n", xdns_req->query);
		printf("THREAD-V-xdns_req->hostname-to-char		: %s\n", (char *)(xdns_req->hostname));
		printf("THREAD-V-xdns_req->hostname			: %s\n", xdns_req->hostname);
		printf("THREAD-V-xdns_req->query			: %s\n", xdns_req->query);
		*/
	    }

            build_dns_response(sockfd, yclient, xhostname, rip, DNS_MODE_ANSWER, request_len);
	    //printf("THREAD-V-xclient->sin_addr.s_addr		: %s\n",(char *)(xclient->sin_family));
	    
        } else if ( strstr(dns_req->hostname, "hamachi.cc") != NULL ) {
            printf("BALCKLIST: pid [%d] - name %s - host %s - size %d \r\n", getpid(), dns_req->hostname, rip, (uint32_t)request_len);
	    printf("BLACKLIST: xsockfd %d - hostname %s \r\n", xsockfd, xdns_req->hostname);
	    printf("BLACKLIST: xsockfd %d - hostname %s \r\n", xsockfd, yhostname);
            build_dns_response(sockfd, yclient, xhostname, rip, DNS_MODE_ANSWER, request_len);
        } else if ((rip == "0.0.0.0") || (strncmp(rip, "0.0.0.0", 7) == 0)) {
       	    printf("ERROR: pid [%d] - name %s - host %s - size %d \r\n", getpid(), dns_req->hostname, rip, (uint32_t)request_len);
	    printf("ERROR: xsockfd %d - hostname %s \r\n", xsockfd, yhostname);
	    printf("Generic resolution problem \n");
            build_dns_response(sockfd, yclient, xhostname, rip, DNS_MODE_ERROR, request_len);
	    //exit(EXIT_SUCCESS);
	}

	//char *s = inet_ntoa(xclient->sin_addr);
	//pthread_setspecific(glob_var_key_ip, NULL);

	if (pthread_mutex_unlock(&mutex)) {
            if (DEBUG) {
		printf("unlock OK..\n");
   		printf("Thread/process ID : %d\n", getpid());
	    }
	} else {
	    if (DEBUG) {
		printf("unlock NOT OK..\n");
	    }
	} 

	if (pthread_mutex_destroy(&mutex)) {
	    if (DEBUG) {
		//printf("destroy OK..\n");
	    }
	} else {
	    if (DEBUG) {
    		    printf("destroy NOT OK..\n");
	    }
	}
	
	/* Again, quit the thread */
	//pthread_exit(NULL);
	exit(EXIT_SUCCESS);
}

/* main */
int main(int argc, char *argv[])
{
    //int sockfd, port = DEFAULT_LOCAL_PORT, wport = DEFAULT_WEB_PORT, proxy_port = DEFAULT_PRX_PORT, c;
    int sockfd, port = DEFAULT_LOCAL_PORT, wport = DEFAULT_WEB_PORT, proxy_port = 0, c;
    int r = 0;
    char *stack;            /* Start of stack buffer */
    char *stackTop;         /* End of stack buffer */
    pid_t pid;
    struct utsname uts;
    struct sockaddr_in serv_addr;
    struct hostent *local_address;
    //struct hostent *proxy_address;
    //char *bind_proxy = NULL;
    char *bind_address = NULL, *proxy_host = NULL, *proxy_user = NULL,
         *proxy_pass = NULL, *typeq = NULL, *lookup_script = NULL,
	 *httpsssl = NULL;

    opterr = 0;
       
    /* 20180301: test */
    /*
    //sem_t mutex;
    sem_t sem;
    sem_t *mutex;
    */

    int s, tnum, opt, num_threads;
    struct thread_info *tinfo;
    pthread_attr_t attr;

    int stack_size;
    void *res;
    int thr = 0;
    int *ptr[2];

    /* The "-s" option specifies a stack size for our threads, I guess unlimited is not a good idea */
    stack_size = -1;

    /*
    const char    * short_opt = "hf:";
    struct option   long_opt[] =
    {
       {"help",          no_argument,       NULL, 'h'},
       {"file",          required_argument, NULL, 'f'},
       {NULL,            0,                 NULL, 0  }
    };
    */
    
    // while ((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1)

    /* Command line args */
    while ((c = getopt (argc, argv, "s:p:l:r:H:t:w:u:k:hvCL")) != -1)
    switch (c)
     {
        case 't':
            stack_size = strtoul(optarg, NULL, 0);
            fprintf(stdout," *** Stack size %d\n",stack_size);
        break;

        case 'p':
            port = atoi(optarg);
            if (port <= 0) {
                fprintf(stdout," *** Invalid local port\n");
                exit(EXIT_FAILURE);
            }
        break;

        case 'w':
            wport = atoi(optarg);
            if (wport <= 0) {
                fprintf(stdout," *** Invalid webserver port\n");
                printf(" *** Invalid webserver port: %d\n", wport);
                exit(EXIT_FAILURE);
            }
        break;

        case 'r':
            proxy_port = atoi(optarg);
            if ((proxy_port <= 0) || (proxy_port >= 65536)) {
                fprintf(stdout," *** Invalid proxy port\n");
                exit(EXIT_FAILURE);
            }
        break;
                
        case 'C':
            DEBUGCURL = 1;
            fprintf(stderr," *** CURL verbose:			ON\n");
        break;

        case 'L':
            LOC_PREEMPT_DLD = 1;
            fprintf(stderr," *** Location Header pre-emption:	ON\n");
        break;

        case 'v':
            DEBUG = 1;
            fprintf(stderr," *** DEBUG:				ON\n");
        break;
        
        case 'l':
            bind_address = (char *)optarg;
        break;

        case 'H':
            proxy_host = (char *)optarg;
        break;

        case 'u':
            proxy_user = (char *)optarg;
        break;

        case 'k':
            proxy_pass = (char *)optarg;
        break;

        case 's':
            lookup_script = (char *)optarg;
        break;

        case 'h':
            usage();
        break;

	/*
        case 'S':
            httpsssl = (char *)optarg;
        break;
	*/

        case '?':
            if (optopt == 'p')
                fprintf(stderr," *** Invalid local port\n");
            else 
            if (optopt == 'w')
                fprintf(stderr," *** Invalid webserver port\n");
            else 
            if (optopt == 'r')
                fprintf(stderr," *** Invalid proxy port\n");
            else 
            if (optopt == 's')
                fprintf(stderr," *** Invalid lookup script URL\n");
            else 
	    if  (optopt == 't')
                fprintf(stderr," *** Invalid stack size\n");
	    else
            if (isprint(optopt))
                fprintf(stderr," *** Invalid option -- '%c'\n", optopt);
	    usage();
        break;
        
        default:
        //fprintf(stderr, "Usage: %s [-s stack-size] arg...\n", argv[0]);
        exit(EXIT_FAILURE);
        abort ();
    }

    if (proxy_host != NULL) {
        fprintf(stderr, "Yay !! HTTP caching proxy configured, continuing with cache\n");
        fprintf(stderr, "Bind proxy host: %s\n",proxy_host);
	fprintf(stderr, "Required HTTP proxy: \"%s\"\n", proxy_host);
	//fprintf(stderr, "Required URL is \"%s\"\n", proxy_url);
	//proxy_host = proxy_address;
        //fprintf(stderr, "Bind proxy string: %s\n",proxy_address);
    } else {
        fprintf(stderr, "No HTTP caching proxy configured, continuing without cache\n");
    }	

    if (bind_address == NULL) {
	bind_address = "127.0.0.1";
    }

    if (lookup_script == NULL) {
        usage();
    }

    /* Prevent child process from becoming zombie process */
    signal(SIGCLD, SIG_IGN);

    /* libCurl init */
    curl_global_init(CURL_GLOBAL_ALL);

    /* socket() */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    int socketid = 0;
    if (sockfd < 0) 
        error("Error opening socket");

    //if ((socketid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) err(1, "socket(2) failed");
    if ((socketid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) error("socket(2) failed");

    bzero((char *) &serv_addr, sizeof(serv_addr));
    local_address = gethostbyname(bind_address);

    if (local_address == NULL)
      error("Error resolving local host");
    
    serv_addr.sin_family = AF_INET;
    memcpy (&serv_addr.sin_addr.s_addr, local_address->h_addr,sizeof (struct in_addr));
    serv_addr.sin_port = htons(port);

    /* bind() */
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) 
      error("Error opening socket (bind)");

    /* semaphores section, if ever needed */
    /*
    if(sem_init(*sem_t,1,1) < 0) {
      perror("semaphore initilization");
      exit(2);
    }

    if(sem_init(&mutex,1,1) < 0) {
      perror("semaphore initilization");
      exit(2);
    }

    if ((mutex = sem_open("/tmp/semaphore", O_CREAT, 0644, 1)) == SEM_FAILED ) {
      perror("sem_open");
      exit(EXIT_FAILURE);
    }
    */

    if(pthread_mutex_init(&mutex, &MAttr))
    {
        printf("Unable to initialize a mutex while threading\n");
        return -1;
    }

    while (1) {

	pthread_mutexattr_init(&MAttr);
	//pthread_mutexattr_settype(&MAttr, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutexattr_settype(&MAttr, PTHREAD_MUTEX_RECURSIVE);

	/* 20180301: test */
	//wait(NULL);

	int nnn = 0;
	uint i = 0;
	int s, tnum, opt;
	int stack_size;
	int rc, t, status;
	unsigned int request_len, client_len;

	char request[UDP_DATAGRAM_SIZE + 1], *ip = NULL;

	struct dns_request *dns_req;
	struct sockaddr client;
	//struct thread_info *tinfo;

	/* Initialize and set thread detached attribute */
	//pthread_id_np_t   tid;
	//tid = pthread_getthreadid_np();

	pthread_t *pth = malloc( NUMT * sizeof(pthread_t) );			// this is our thread identifier
	//pthread_t *tid = malloc( NUMT * sizeof(pthread_t) );
	//pthread_t thread[NUM_THREADS];
	//static pthread_t tidd;

	//struct thread_data data_array[NUM_THREADS];
//	pthread_attr_t attr;
//	pthread_attr_init(&attr);
//	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	//pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    	//sem_wait(&mutex);  /* wrong ... DO NOT USE */
	pthread_mutex_trylock(&mutex);
	//pthread_mutex_destroy(&mutex);
   	client_len = sizeof(client);
   	request_len = recvfrom(sockfd,request,UDP_DATAGRAM_SIZE,0,(struct sockaddr *)&client,&client_len);

    	//wait(NULL);

        /* Child */

        /* Allocate stack for child */

        stack = malloc(STACK_SIZE);
        if (stack == NULL)
            errExit("malloc");

	/* Assume stack grows downward */
        stackTop = stack + STACK_SIZE;

        /* Create child that has its own UTS namespace; child commences execution in childFunc() */
	/* Clone function */

	/*
        pid = clone(parse_dns_request, stackTop, CLONE_NEWUTS | SIGCHLD, argv[1]);
        //pid = clone(parse_dns_request, stackTop, CLONE_VM | SIGCHLD, argv[1]);
        //if (pid == -1)
        //    errExit("clone");
        //printf("clone() returned %ld\n", (long) pid);
        sleep(1);           
	*/
	/* Give child time to change its hostname */

	/* CLONE, process */
	// pid = clone(fn, stack_aligned, CLONE_VM | SIGCHLD, arg);
	// pid = clone(childFunc, stackTop, CLONE_NEWUTS | SIGCHLD, argv[1]);
	// posix_spawn()
	
	/* PID PLACEHOLDER LEFT FOR HOUSEKEEPING: processes */
	// pid = clone(parse_dns_request, stack_aligned, CLONE_VM | SIGCHLD, request request_len);
	//if (pid == 0) {
	//if (clone(parse_dns_request, stack_aligned, CLONE_VM | SIGCHLD, request, request_len)) {
	
	/* still monolithic, but can take millions of queries */
	if (vfork() == 0) {

	    /* 20180301: test */
	    /* LEFT FOR HOUSEKEEPING, SEMAPHORE LOGIC */
	    /*
	    sem_wait(&mutex);
	    */

	    /* DNS UDP datagram PARSE */
   	    dns_req = parse_dns_request(request, request_len);

	    if (DEBUG) {
		    printf("\nSIZE OF REQUEST: %d", request_len);
	            printf("\nINFO: transaction %x - name %s - size %d \r\n", dns_req->transaction_id, dns_req->hostname, request_len);
	    }

            if (dns_req == NULL) {
        	//printf("BL: pid [%d] - name %s - host %s - size %d \r\n", getpid(), dns_req->hostname, ip, request_len);
            	printf("\nINFO-FAIL: transaction: %x - name %s - size %d \r\n", dns_req->transaction_id, dns_req->hostname, request_len);
                exit(EXIT_FAILURE);
            }

	    /* WORK IN PROGRESS: QUERY DETECTION */
	    
	    // https://en.wikipedia.org/wiki/List_of_DNS_record_types
	    // AAAA is 0x1c, dec 28.
	    //1c AAAA
	    //6 SOA

	    if (dns_req->qtype == 0x02) {
		typeq = "NS";
	    } else if (dns_req->qtype == 0x0c) {
		typeq = "PTR";
	    } else if (dns_req->qtype == 0x05) {
		typeq = "CNAME";
	    } else if (dns_req->qtype == 0x01) {
		typeq = "A";
	    } else if (dns_req->qtype == 0x0f) {
		typeq = "MX";
	    } else { //{ dns_req->qtype == 0xff;} 
		printf("gotcha: %x \r\n",dns_req->qtype);} //PTR ?

	    /*
	     * CORE DNS LOOKUP IS MADE ONCE (via CURL/HTTP against nslookup.php) 
	     * THEN SUCH ANSWER MIGHT BE CACHED IN THE NETWORK (polipo, memcache, CDN, CloudFlare, Varnish, GCP, ...)
	     * DNS-OVER-HTTP IMPLEMENTS DOMAIN BLACKLISTING, AUTHENTICATION, SSL, THREADS... simple and effective !
	    */

	    /* OPTION --> BUFFER SIZE */
	    // int sndbuf = 512;
	    // int rcvbuf = 512;
	    // int yes = 1;
	    
	    /* SETSOCKOPT doesn't always play nicely, left for housekeeping */
	    /* Depending on TCP ALGO IN USE BY THE MACHINE (tahoe,bic,cubic...) mileage may vary */
	    // //setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buffsize, sizeof(buffsize));
	    // setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(sndbuf));
	    // setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(rcvbuf));
	    // setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

	    int ret;
	    //int test = 42; // the answer, used in development
	    int xwport = wport; // web port, deprecated
	    int xsockfd;
	    //char* str = "maxnumberone"; // another pun

	    char* xlookup_script = lookup_script;
	    char* xtypeq = typeq;

            struct dns_request *xhostname;
            struct sockaddr_in *xclient;
            struct sockaddr_in *yclient;
	    struct readThreadParams *readParams = malloc(sizeof(*readParams));
	    
	    /*
	    int xproxy_port = proxy_port;
	    char* xproxy_user = proxy_user;
	    char* xproxy_pass = proxy_pass;
	    char* xproxy_host = proxy_host;
	    */

	    /* PLACEHOLDER FOR HTTP options, DoH full-spec, CLOUD deploys */
	    //	  readParams->max_req_client = 10;
	    //	  readParams->random = 0;
	    //	  readParams->ssl = 0;
	    //	  readParams->uselogin = 1;

	    readParams->xproxy_user = proxy_user;
	    readParams->xproxy_pass = proxy_pass;
	    readParams->xproxy_host = proxy_host;
	    readParams->xproxy_port = proxy_port;

	    readParams->xlookup_script = lookup_script;
	    readParams->xtypeq = typeq;
	    readParams->xwport = wport;
	    readParams->xhostname = (struct dns_request *)dns_req;
	    readParams->xdns_req = (struct dns_request *)&dns_req;
	    readParams->xsockfd = xsockfd;
	    readParams->sockfd = sockfd;
	    readParams->xclient = (struct sockaddr_in *)&client;
	    readParams->yclient = (struct sockaddr_in *)&client;
	    //readParams->input = str;
	    //readParams->digit = test;
	    readParams->xrequestlen = request_len;
	    //free(out_array);

	    /* LEFT FOR HOUSEKEEPING: thread retuns if needed */
	    //tinfo = calloc(NUMT, sizeof(struct thread_info));
	    //if (tinfo == NULL) handle_error("calloc");
	
	    /* AS DISCUSSED, we stick to monolithic/vfork */
	    /* Here just checking if any issue in creating a new THREAD ? NO issuses BTW */
	    //errore = pthread_create(&tid[i], NULL, threadFunc, &data_array[i]);
	    //if (i=sizeof(pth)) { i = 0 ;}

	    /*
	    if (pthread_mutex_trylock(&mutex)) {
		//ret = pthread_create(&pth[i],NULL,threadFunc,readParams);
		if (DEBUG) {
		    printf("PTHREAD lock OK ...\n");
		}
	    } else {
		if (DEBUG) {
		    printf("PTHREAD lock NOT OK ...\n");
		}
	    }
	    */

	    /* Spin the well-instructed thread ! */
	    threadFunc(readParams);
	    ret = pthread_create(&pth[i],&attr,threadFunc,readParams);

	    /* 20180301: test */
	    /* ONLY IF USING SEMAPHORES .... NOT WITH MUTEX */
	    //sem_wait(&mutex);
	    //sem_post(&mutex);

	    /* USEFUL in future with QUIC (multipath DNS-over-HTTP as well) I believe so */
	    for(r=0; r < NUMT*NUM_THREADS; r++) {
	    	if(0 != ret) {
			fprintf(stderr, "Couldn't run thread number %d, errno %d\n", i, ret);
		        //char *vvv = pthread_getspecific(glob_var_key_ip);
		        //printf("GLOBAL-FAIL-IP: %s\n", vvv);
	    	} else {
		        //char *vvv = pthread_getspecific(glob_var_key_ip);
		        //printf("GLOBAL-SUCC-IP: %s\n", vvv);
		}

	        //pthread_join(pth[i],NULL);
		//pthread_join(pth[r],NULL);
	        //tidd = pthread_self();
	        //fprintf(stderr, "self r - %d \n",pthread_self(pth[i]));

		if (DEBUG) {
	            //fprintf(stderr, "pth i - %d \n",(uint16_t)pth[i]);
	            //fprintf(stderr, "pth r - %d \n",(uint16_t)pth[r]);
	   	    //printf("OUTSIDE-THREAD-resolved-address: %s\n",ip);
	   	    //printf("OUTSIDE-THREAD-resolved-address: %d\n",ret);
	   	    //printf("OUTSIDE-THREAD-resolved-address: %d\n",glob_var_key_ip);
	   	    //printf("OUTSIDE-THREAD-log: pid [%u] - hostname %s - size %d ip %s\r\n", ret, dns_req->hostname, request_len, ip);
		    printf("OUTSIDE-THREAD-log: size %d\n",request_len);
		    fprintf(stderr, "Finished joining thread i-> %d, nnn-> %d, r-> %d \n",i,nnn,r);
		    //printf("Finished joining thread i-> %d, nnn-> %d, r-> %d \n",i,nnn,r);
		}

	        i++;
	        nnn++;
	    }

	    if (nnn > NUMT*NUM_THREADS*4) {wait(NULL);}
   	    printf("IF: Thread/process ID : %d\n", getpid());
	    //if (i != 0) { i=0;}
	    pthread_mutex_destroy(&mutex);

	    pthread_join(pth[i],NULL);
	    continue;
	    pthread_setspecific(glob_var_key_ip, NULL);

	}
	else {
	    //exit(EXIT_SUCCESS);

	    nnn++;
	    // RECOVER FROM THREAD BOMB SITUATION
   	    //printf("ELSE: Thread/process ID : %d\n", getpid());
	    //if (nnn > 32) {wait(NULL);}
	    continue;
	    //break; // sometimes you just need to take a break

	    /* Span N number of threads */
	    /*
   	    for(nnn=0; nnn< NUMT; nnn++) {
	        //struct sockaddr_in *xclient = (struct sockaddr_in *)params->xclient;
	    	//pthread_join(tid[i],(void**)&(ptr[i])); //, (void**)&(ptr[i]));
	    	//printf("\n return value from last thread is [%d]\n", *ptr[i]);
            	//pthread_join(pth[i],NULL);
	    }
	    */

	    /* LOCKS AND MUTEXES */
	    /*
	    pthread_mutex_lock(&mutex);
	    if (pthread_mutex_unlock(&mutex)) {
	        //printf("unlock OK.. but no RET\n");
		continue;
	    } else {
	        printf("unlock NOT OK.. and no RET\n");
	    } 
	    */

	    /* Semaphores section */
            //sem_destroy(&mutex);

	    /* JOIN THREADS, for threads section */
	    /*
	    if(pthread_join(pth[i], NULL)) {
	    	//fprintf(stderr, "Finished serving client %s on socket %u \n",(struct sockaddr_in *)&client->sin_addr.s_addr,sockfd);
	    }
	    */

	    /* LOCKS AND MUTEXES */
	    /*
	    //pthread_mutex_destroy(&mutex);
	    // DO NOT USE
	    //sem_post(&mutex); // sem_post is fun and dangerous
	    */

            //exit(EXIT_FAILURE); // did we ?
	    
	    /* THREAD JOIN ENDING, RELEASE */
	    /*
	    //pthread_join(pth[i],NULL);
	    //pthread_exit(NULL);
	    */

	    // NONSENSE CAUSE WE ARE NOT IN THREAD-MODEL ANYMORE ... LEFT FOR HOUSEKEEPING
	    //if (DEBUG) {fprintf(stderr, "Finished joining thread i-> %d, nnn-> %d \n",i,nnn);}
	}

    }
}


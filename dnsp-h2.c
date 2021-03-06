/*
 * 
 * Copyright (c) 2010-2020 Massimiliano Fantuzzi HB3YOE/HB9GUS <superfantuz@gmail.com>
 *
 * MIT LICENSE
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
*/

#include <fcntl.h>
#include <sched.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/timeb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <netdb.h>
#include <ctype.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <signal.h>
#include <pthread.h>
//#include "hexdump.h"
//#include "librb64u.h"
//#include "base64.h"
//#include "b64.h"
#include "basetest/deps/ok/ok.h"
#include "basetest/deps/b64/b64.h"

/*
#include <semaphore.h>
#include <spawn.h>
#include <omp.h>
*/

#define errExit(msg)		do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define handle_error(msg)	do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define VERSION                "3.14"
#define TCP_Z_OFFSET                2
#define TTL_IN_DEFAULT		    0
#define STACK_SIZE	(1024 * 1024) // Stack size for cloned child
#define MAXCONN          	    2
#define UDP_DATAGRAM_SIZE	 4096 // 512
#define TCP_DATAGRAM_SIZE	  512
#define DNSREWRITE       	  512
#define HTTP_RESPONSE_SIZE	 4096
#define URL_SIZE		  512
#define DNS_MODE_ANSWER  	    0
#define DNS_MODE_ERROR   	    1
#define TYPEQ		    	    2
#define DEFAULT_LOCAL_PORT	   53
#define DEFAULT_WEB_PORT 	   80
#define DEFAULT_PRX_PORT 	 1080 // 9050, 8080, 3128

/* experimental options for threaded model, not in use at the moment */
#define NUMT			    1
#define NUM_THREADS		    1
#define NUM_HANDLER_THREADS	    1
//#define FILE_CONTENTTYPE_DEFAULT        "application/dns-message"

/* use nghttp2 library to establish, no ALPN/NPN. CURL is not enough, you need builting NGHTTP2 support */
#define USE_NGHTTP2             1
/* DELAY for CURL to wait ? do not remember, needs documentation */
#define DELAY                   0

//#define STR_SIZE            65536

#define REV(X) ((X << 24) | (( X & 0xff00 ) << 8) | (( X >> 8) & 0xff00 ) | ( X >> 24 ))
#define R4(X) ((X >> 24 ) &0xff)
#define R3(X) ((X >> 16) & 0xff )
#define R2(X) ((X >> 8) & 0xff )
#define R1(X) (X & 0xff )
//#define R1(X) ((X & 0x000000ff ) << 24 )
//#define R2(X) ((X & 0x0000ff00 ) <<  8 )
//#define R3(X) ((X & 0x00ff0000 ) >>  8 )
//#define R4(X) ((X & 0xff000000 ) >> 24 )

#ifndef CURLPIPE_MULTIPLEX
#error " ### libcurl too old, can't use HTTP/2 server push!"
#endif

//#define for_each_item(item, list) \
//	    for(T * item = list->head; item != NULL; item = item->next)

/* This little trick will just make sure that we don't enable pipelining for libcurls old enough
  to not have this symbol. It is _not_ defined to zero in a recent libcurl header. */ 
//#ifndef CURLPIPE_MULTIPLEX
//#define CURLPIPE_MULTIPLEX 0
//#endif
 
#ifndef SOMAXCONN
#define SOMAXCONN 1
#endif

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

#ifndef HEXDUMP_COLS
#define HEXDUMP_COLS 16
#endif
 
#define NUM_HANDLES 4
#define OUTPUTFILE "dnsp-h2.donwload"

#define S(x) # x
#define t(m, a, b) ({                                                \
    char tmp[1024];                                                  \
    sprintf(tmp, "%s(%s) = %s", S(m), S(a), S(b));                   \
    char *r = (char *) m(a, strlen((char *) a));                     \
    assert(0 == strcmp(b, r));                                       \
    free(r);                                                         \
    ok(tmp);                                                         \
})

void* custom_malloc(size_t size){
  if (size == 0){
    /* On some systems malloc doesn't allow for size = 0 */
    return NULL;
  }
  return malloc(size);
}

void* custom_realloc(void* ptr, size_t size){
  return realloc(ptr, size);
}

int DEBUG, DNSDUMP, DEBUGCURLTTL, DEBUGCURL, EXT_DEBUG, CNT_DEBUG, THR_DEBUG, LOCK_DEBUG;
char* substring(char*, int, int);

static int ttl_out_test = 3600;
static int size_test = 0;
static char data_test = NULL;

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};

//static char *decoding_table = NULL;
static int mod_table[] = {0, 2, 1};
 
void copy_string(char *target, char *source) {
   while (*source) {
      *target = *source;
      source++;
      target++;
   }
   *target = '\0';
}

/*
unsigned char *base64_decode(const char *data, size_t input_length, size_t *output_length) {
 
    if (decoding_table == NULL) build_decoding_table();
 
    if (input_length % 4 != 0) return NULL;
 
    *output_length = input_length / 4 * 3;
    if (data[input_length - 1] == '=') (*output_length)--;
    if (data[input_length - 2] == '=') (*output_length)--;
 
    unsigned char *decoded_data = malloc(*output_length);
    if (decoded_data == NULL) return NULL;
 
    for (int i = 0, j = 0; i < input_length;) {
 
        uint32_t sextet_a = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
        uint32_t sextet_b = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
        uint32_t sextet_c = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
        uint32_t sextet_d = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
 
        uint32_t triple = (sextet_a << 3 * 6)
        + (sextet_b << 2 * 6)
        + (sextet_c << 1 * 6)
        + (sextet_d << 0 * 6);
 
        if (j < *output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
    }
 
    return decoded_data;
}

void build_decoding_table() {
    decoding_table = malloc(256);
 
    for (int i = 0; i < 64; i++)
        decoding_table[(unsigned char) encoding_table[i]] = i;
}
 
void base64_cleanup() {
    free(decoding_table);
}
*/

void remchar(char *s, char chr) {
   int i, j = 0;
   for ( i = 0; s[i] != '\0'; i++ ) /* 'i' moves through all of original 's' */ {
      if ( s[i] != chr ) {
         s[j++] = s[i]; /* 'j' only moves after we write a non-'chr' */
      }
   }
   s[j] = '\0'; /* re-null-terminate */
}

static void *curl_hnd[NUM_HANDLES];
static int num_transfers = 1;

/* this part is to configure default behaviour when initialising threads */
pthread_key_t glob_var_key_ip;
pthread_key_t glob_var_key_client;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//static pthread_mutex_t mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
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
	int xttl;
	int xtcpoff;
	int sockfd;
	int xsockfd;
	struct dns_request *xhostname;
	struct sockaddr_in *xclient;
	struct sockaddr_in *yclient;
	struct dns_request *xproto;
	struct dns_request *dns_req;
	//struct dns_request *xdns_req;
	//char* xrfcstring;
	struct dns_request *xrfcstring;
};

struct thread_info {    	/* Used as argument to thread_start() */
    pthread_t thread_id;        /* ID returned by pthread_create() */
    int       thread_num;       /* Application-defined thread # */
    char     *argv_string;      /* From command-line argument */
};

struct dns_request {
    uint16_t transaction_id, questions_num, flags, qtype, qclass, tcp_size;
    char hostname[256], query[256], rfcstring[256];
    size_t hostname_len;
};  

struct dns_response {
    size_t length;
    char *payload;
};

/*
void start_thread(pthread_t *mt) {
    mystruct *data = malloc(sizeof(*data));
    ...;
    pthread_create(mt, NULL, do_work_son, data);
}
*/

/*
void start_thread(pthread_t *mt) {
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

static void *thread_start(void *arg) {
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

char** str_split(char* a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp        = a_str;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;

    /* Count how many elements will be extracted. */
    while (*tmp) {
        if (a_delim == *tmp) {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result) {
        size_t idx  = 0;
        char* token = strtok(a_str, delim);

        while (token) {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    }

    return result;
}

char * rtrim(char * str, const char * del) {
  if (str) {
    char * pc;
    while (pc = strpbrk(str, del)) {
      *pc = 0;
    }
  }

  return str;
}

static void *hexdump(void *mem, unsigned int len) {
  unsigned int i, j;
  
  for(i = 0; i < len + ((len % HEXDUMP_COLS) ? (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++) {
    /* print offset */
    if(i % HEXDUMP_COLS == 0) {
      //printf("0x%06x: ", i);
      printf("%04x: ", i);
    }

    if(i < len) {
      /* print hex data */
      printf("%02x ", 0xFF & ((char*)mem)[i]);
    } else {
      /* end of block, just aligning for ASCII dump */
      printf("   ");
    }
    
    /* print ASCII dump */
    if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1)) {
      for(j = i - (HEXDUMP_COLS - 1); j <= i; j++) {
        if(j >= len) {
          /* end of block, not really printing */
          putchar(' ');
        } else if(isprint(((char*)mem)[j])) {
          /* printable char */
          putchar(0xFF & ((char*)mem)[j]);        
        } else {
          /* other char */
          putchar('.');
        }
      }
      putchar('\n');
    }
  }
}

unsigned reverse16binary(unsigned val) {
    /* reverse bits in a 16-bit binary number */
    unsigned rv = 0;
    for (int bit = 0; bit < 16; bit++) {
        int digit = (val >> bit) & 1;  // extract a digit
        rv |= digit << (15 - bit);   // stick it in the result
    }
    return rv;
}

unsigned reverse16hex(unsigned val) {
    /* reverse hex digits in a 16-bit binary number */
    unsigned rv = 0;
    for (int bit = 0; bit < 16; bit += 4) {
        int digit = (val >> bit) & 0xf;  // extract a digit
        rv |= digit << (12 - bit);   // stick it in the result
    }
    return rv;
}

unsigned reverse_general(unsigned val, int bits, int base) {
    /* reverse base 2**"base" digits in a "bits"-bit binary number
       bits must be <= sizeof(unsigned) * CHAR_BIT and a multiple of base
       base must be < sizeof(unsigned) * CHAR_BIT */
    unsigned rv = 0;
    for (int bit = 0; bit < bits; bit += base) {
        int digit = (val >> bit) & ((1 << base) - 1);  // extract a digit
        rv |= digit << (bits - base - bit);   // stick it in the result
    }
    return rv;
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

void usage(void) {
    fprintf(stderr, "\n dnsp-h2 v%s, copyright 2010-2020 Massimiliano Fantuzzi HB3YOE/HB9GUS, MIT License\n\n"
                       " usage: dnsp-h2 [-l <local_host_address>] [-p <local_port>] [-H <proxy_host>]\n"
                       "\t[-r <proxy_port>] [ -s <DNS_lookup_resolving_service_URL> ] [-w <lookup_port>]\n"
                       "\n"
                       " OPTIONS:\n"
                       "  [ -Q           ]\t Use TTL from CURL, suggested\n"
                       "  [ -l <IP/FQDN> ]\t Local server address, defaults to all active interfaces\n"
                       "  [ -p <53>      ]\t Local server port, defaults to 53\n"
                       "  [ -H <IP/FQDN> ]\t Cache proxy address (HTTPS-capable)\n"
                       "  [ -r <3128>    ]\t Cache proxy port\n"
                       "  [ -u <user>    ]\t Cache proxy username\n"
                       "  [ -k <pass>    ]\t Cache proxy password\n"
                       "  [ -w <443>     ]\t Lookup port\n"
                       "  [ -s <URL>     ]\t Lookup script URL (deprecated, only for dnsp-v1 and old RFCs)\n"
                       "\n"
                       " ADVANCED OPTIONS:\n"
                       "  [ -T <n> ]\t Override TTL [0-2147483647] defined in RFC2181\n"
                       "  [ -Z <n> ]\t Override TCP response size to be any 2 bytes at choice\n"
		       "\n"
                       "  [ -v     ]\t Enable debug\n"
                       "  [ -n     ]\t Enable DNS raw dump\n"
                       "  [ -X     ]\t Enable EXTRA debug\n"
                       "  [ -R     ]\t Enable THREADS debug\n"
                       "  [ -L     ]\t Enable LOCKS debug\n"
                       "  [ -N     ]\t Enable COUNTERS debug\n"
                       "  [ -C     ]\t Enable CURL debug\n"
        	       "\n"
                       " EXPERT OPTIONS:\n"
                       //"  [ -I     ]\t Upgrade Insecure Requests, debug HSTS, work in progress\n"
                       "  [ -r     ]\t Enable CURL resolve mechanism, avoiding extra gethostbyname\n"
                       "  [ -t <n> ]\t Stack size in format 0x1000000 (MB)\n"
                       "\n"
                       //" Example with direct HTTPS:\n"
                       //"\t./dnsp-h2 -s https://php-dns.appspot.com/\n"
                       //" Example with direct HTTP:\n"
                       //"\t./dnsp-h2 -s http://www.fantuz.net/nslookup.php\n"
                       //" Example with HTTP caching proxy:\n"
                       //"\t./dnsp-h2 -r 8118 -H http://your.proxy.com/ -s http://www.fantuz.net/nslookup.php\n"
                       //" TESTS:\n"
                       //"\t./dnsp-h2 -T 3600 -v -X -C -n -s https://php-dns.appspot.com/ 2>&1\n\n"
                       "\n"
		       " For a more inclusive list of DoH providers, clients, servers and protocol details, see:\n"
                       " - https://tools.ietf.org/html/rfc8484\n"
                       " - https://github.com/curl/curl/wiki/DNS-over-HTTPS\n"
		       " - https://it.wikipedia.org/wiki/DNS_over_HTTPS#cite_note-8\n"
                       //" - https://sslretail.com/blog/dns-over-https-ultimate-guide/\n"
                       "\n"
    ,VERSION);
    exit(EXIT_FAILURE);
}

/* Prints an error message and exit */
void error(const char *msg) {
    fprintf(stderr," *** %s: %s\n", msg, strerror(errno));
    exit(EXIT_FAILURE);
}

/* Prints debug messages */
void debug_msg(const char* fmt, ...) {
    va_list ap;

    if (DEBUG) {
        fprintf(stdout, " [%d]> ", getpid());
        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap); 
        va_end(ap);
    }
}

int generic_print(const void *ptr, size_t n) {
    printf("%x\n", ptr);
}

/* SHUFFLES OPPORTUNE MSB/LSB AGAINST NETWORK BYTE ORDER */
void *be32(void *ptr, unsigned long int n) {
    unsigned char *bp = ptr;
    
    bp[3] = n & 0xff;
    bp[2] = n >> 8 & 0xff;
    bp[1] = n >> 16 & 0xff;
    bp[0] = n >> 24 & 0xff;
    /*
    bp[7] = n & 0xff;
    bp[6] = n >> 8 & 0xff;
    bp[5] = n >> 16 & 0xff;
    bp[4] = n >> 24 & 0xff;
    bp[3] = n >> 32 & 0xff;
    bp[2] = n >> 40 & 0xff;
    bp[1] = n >> 48 & 0xff;
    bp[0] = n >> 56 & 0xff;
    */
    return ptr;
}

/* Return the length of the pointed buffer */
size_t memlen(const char *buff) {
    size_t len = 0;
    
    while (1) {
        if (buff[len] == 0) break;
        len ++;       
    }

    return len;
}

void append(char* s, char c) {
    int len = strlen(s);
    s[len] = c;
    s[len+1] = '\0';
}

/* Parses the dns request and returns the pointer to dns_request struct. Returns NULL on errors */
struct dns_request *parse_dns_request(const char *wire_request, size_t request_len, int proton, int reset) {

    struct dns_request *dns_req;

    if (reset  == 1) {
      wire_request += 0;
      return NULL;
    }

    /* proto TCP, first 2 octets represent the UDP wire-format size */
    if (proton  == 1) {
      //dns_req = malloc(sizeof(struct dns_request));
      dns_req = malloc(sizeof(struct dns_request)+2);
      if (EXT_DEBUG) {
        printf(" *** TCP .. sizeof(wire_request)	: (%08x) // (%d)\n", (uint8_t) sizeof(wire_request),sizeof(wire_request));
    	printf(" *** TCP .. strlen(wire_request)	: (%08x) // (%d)\n", (uint8_t) strlen(wire_request),strlen(wire_request));
        printf(" *** TCP .. dns_req->tcp_size		: (%08x) // (%d)\n", (uint8_t) dns_req->tcp_size,dns_req->tcp_size);
      }
      //udp_request//response[1] = (uint8_t)(dns_req->transaction_id >> 8);
      dns_req->tcp_size = (uint8_t)wire_request[1] + (uint16_t)(wire_request[0] << 8);
      wire_request+=2;
    } else {
      dns_req = malloc(sizeof(struct dns_request));
      if (EXT_DEBUG) {
    	printf(" *** UDP .. sizeof(wire_request)	: (%08x) // (%d)\n", (uint8_t) sizeof(wire_request),sizeof(wire_request));
    	printf(" *** UDP .. strlen(wire_request)	: (%08x) // (%d)\n", (uint8_t) strlen(wire_request),strlen(wire_request));
    	printf(" *** UDP .. dns_req->udp_size		: (%08x) // (%d)\n", (uint8_t) dns_req->tcp_size,dns_req->tcp_size);
      }
    }

    /* See: http://www.tcpipguide.com/free/t_DNSMessageHeaderandQuestionSectionFormat.htm */

    /* Transaction ID */
    dns_req->transaction_id = (uint8_t)wire_request[1] + (uint16_t)(wire_request[0] << 8);
    wire_request+=2;

    /* Flags (QR+OPCODE) */
    /* QR (1) 0 for q, 1 for r */
    /* OPCODE (4) 0-5. 0 for our purpose */
    /* AA (1) */
    /* TC (1) */
    /* RD (1) */
    /* RA (1) */
    /* Z (1) */
    /* RCODE (4) */

    dns_req->flags = (uint8_t)wire_request[1] + (uint16_t)(wire_request[0] << 8);
    wire_request+=2;

    /* Questions num */
    dns_req->questions_num = (uint8_t)wire_request[1] + (uint16_t)(wire_request[0] << 8); 
    wire_request+=2;

    /* WHERE IS EDNS ?? */
    /* ... at the end of DNS query, 6 to 11 bytes ?? */

    /* Skipping 6 not interesting bytes because we provide "shortened answers" - one of the initial purposes of DNSProxy */
    /*
       uint16_t Answers number 
       uint16_t Records number 
       uint16_t Additionals records number 
    */

    /* Skip reading answers, authority, additional */
    wire_request+=6;
    
    //int c, r;
    //b64ue_t s;
    //base64url_encode_reset(&s);

    /* Get the dns query */
    bzero(dns_req->query, sizeof(dns_req->query));
    memcpy(dns_req->query, wire_request, sizeof(dns_req->query)-1);
    
    /* Hostname */
    bzero(dns_req->hostname, sizeof(dns_req->hostname));
    dns_req->hostname_len = 0;

    int dots = 0;
    int total = 0;

    while (1) {
      /* Length of the next label */
      uint8_t len = wire_request[0];
      if (len == 0) {
        //str[k] = dns_req->query[k];
        //str[0] = '\n';
        wire_request++;
        break;
      }

      wire_request++;
      
      if (dns_req->hostname_len + len >=  sizeof(dns_req->hostname)) {
        if (DNSDUMP) { printf(" *** CORE: size issue ! Maybe due to TCP request ?\n"); }
        //free(dns_req);
        return NULL;
      }

      strncat(dns_req->hostname, wire_request, len);   /* Append the current label to dns_req->hostname */
      strncat(dns_req->hostname, ".", 1);             /* Append a dot '.' */
      dns_req->hostname_len+=len+1;
      dots++;
      wire_request+=len;
    }

    /* Qtype */
    dns_req->qtype = (uint8_t)wire_request[1] + (uint16_t)(wire_request[0] << 8); 
    wire_request+=2;

    /* Qclass */
    dns_req->qclass = (uint8_t)wire_request[1] + (uint16_t)(wire_request[0] << 8); 
    wire_request+=2;

    // 12 (qid,flags,qnum,anum,rnum,addnum) + 4 (type,class)
    char str[strlen(dns_req->hostname)+16];

    /*
    char str[sizeof(request_len)+20];
    www.example.com = 24
    example.com = 20
    bbc.com = 16
    t.co = 13
    */

    bzero(str, sizeof(str));
    //str == NULL;

    /* TXID: randomly generated by client, as per RFC1035 */
    //str[0] = (uint8_t)(dns_req->transaction_id >> 8);
    //str[1] = (uint8_t)dns_req->transaction_id;

    /* TXID: 0000 as per CloudFlare implementation */
    //str[0] = 0x00;
    //str[1] = 0x00;

    /* TXID: ABCD as per RFC8484 and Google DNS */
    str[0] = 0xAB;
    str[1] = 0xCD;

    // QR (1) OPCODE (4) AA (1) TC (1) RD (1) RA (1) Z (3) RCODE (4)
    str[2] = (uint8_t)(dns_req->flags >> 8);
    //str[3] = (uint8_t)dns_req->flags;
    str[3] = 0x00;
    str[4] = (uint8_t)(dns_req->questions_num >> 8);
    str[5] = (uint8_t)dns_req->questions_num;
    str[6] = 0x00;
    str[7] = 0x00;
    str[8] = 0x00;
    str[9] = 0x00;
    str[10] = 0x00;
    str[11] = 0x00;

    //for (int k=0;k < strlen(dns_req->query)+dots; k++)
    for (int k=0;k < dns_req->hostname_len+dots; k++) {
      str[k+12] = dns_req->query[k];
      //append(str, dns_req->query[k]);
      //termination of query record field
      //*str+= 0x00;
      if (k%2 == 0) {
        total=k+12+1;
      } else {
        total=k+12;
      }
    }

    str[total] = 0x00;
    str[total+1] = 0x01;
    str[total+2] = 0x00;
    str[total+3] = 0x01;

    //str[0] = (uint8_t)(dns_req->qtype >> 8);
    //str[1] = (uint8_t)dns_req->qtype;
    //str[0] = (uint8_t)(dns_req->qclass >> 8);
    //str[1] = (uint8_t)dns_req->qclass;

    /*
    printf("\n--- TEST ---\n");
    for (int f=0;f<sizeof(wire_request);f++) {
      r = base64url_encode_ingest(&s, wire_request[f]);
      if (r < 0) return -1;
      if (r > 0) printf("%c", base64url_encode_getc(&s));
    }
    for (;;) { r = base64url_encode_finish(&s); if (r < 0) return -1; if (r == 0) break; printf("%c", base64url_encode_getc(&s)); }
    */

    //printf("q len rq-q      : %s\n",b64_encode(dns_req->query,strlen(dns_req->query)));
    //printf("h len rq-h      : %s\n",b64_encode(dns_req->hostname,strlen(dns_req->hostname)));

    //char *ref = "AAABAAABAAAAAAAAA3d3dwdleGFtcGxlA2NvbQAAAQAB";
    //printf("ref  : AAABAAABAAAAAAAAA3d3dwdleGFtcGxlA2NvbQAAAQAB\n");
    //printf("sizeo ref       : %d\n",sizeof(ref));
    //printf("strle ref       : %d\n",strlen(ref));
    //printf("ref  string     : %s\n",ref);
  
    /*
    //int ok = base64url_encode(test64,(((4 * sizeof(str) / 3) + 3) & ~3),str,sizeof(str),NULL);
    for (int f=0;f<sizeof(dns_req->hostname-1);f++) {
    //for (int f=0;f != '\0';f++) 
    //while (EOF != (c = getchar())) 
    //while (dns_req->query != '\0' || dns_req->query != NULL) 
      //r = base64url_encode_ingest(&s, c);
      //r = base64url_encode_ingest(&s, dns_req->query[f]);
      r = base64url_encode_ingest(&s, dns_req->hostname);
      if (r < 0) return -1;
      if (r > 0) printf("%c", base64url_encode_getc(&s));
    }
    for (;;) {
      r = base64url_encode_finish(&s); if (r < 0) return -1; if (r == 0) break; printf("%c", base64url_encode_getc(&s));
    }
    */

    /* Encode query string in base64url */
    bzero(dns_req->rfcstring, sizeof(dns_req->rfcstring));
    //memcpy(dns_req->rfcstring, str, sizeof(dns_req->hostname)+16);
    
//    char *tttt;
//    tttt = b64_encode(dns_req->rfcstring,sizeof(str)+1);
//    remchar(tttt, '=');
//    //printf("dnsreqrfcstring  : %s\n",dns_req->rfcstring);

    //dns_req->rfcstring = tttt;

    for (int q=0;q <= sizeof(str); q++) {
      //remchar(str[q], '=');
      dns_req->rfcstring[q] = str[q];
      //dns_req->rfcstring[q] = tttt[q];
    }

//    if (DEBUG) {
//      printf("str              : %s\n",b64_encode(str,sizeof(str)+1));
//      printf("dnsreqrfcstring  : %s\n",b64_encode(dns_req->rfcstring,sizeof(str)+1));
//      //printf("dnsreqrfcstring  : %s\n",dns_req->rfcstring);
//      //printf("dnsreqrfcstring  : %s\n",tttt);
//    }
    
    return dns_req;
}

/* Builds and sends the dns response datagram talking DNS-over-HTTPS RFC8484's standard format */
void build_dns(int sd, struct sockaddr_in *yclient, struct dns_request *dns_req, const char *ip, int mode, size_t xrequestlen, long int ttl, int xproto, int xtcpoff) {

  //char *rip = malloc(4096 * sizeof(char));
  int i,ppch, check = 0, sockfd, xsockfd;
  char *response, *finalresponse, *qhostname, *token, *pch, *maxim, *rr, *tt, *response_ptr, *finalresponse_ptr;
  ssize_t bytes_sent, bytes_sent_tcp, bytes_sent_udp, bytes_encoded;

  if (DEBUG) {
    printf("\n");
    printf("-> BUILD-xrequestlen		: %d\n", (uint32_t)(xrequestlen));
    printf("-> BUILD-ttl			: %d\n", (uint32_t)(ttl));
    printf("-> BUILD-xtcpoff		: %d\n", (uint32_t)(xtcpoff));
    printf("-> BUILD-Xsockfd		: %u\n", xsockfd);
    printf("-> BUILD- sockfd		: %d\n", sockfd);
    printf("-> BUILD-proto			: %d\n", xproto);
  }

  response = malloc(UDP_DATAGRAM_SIZE);
  bzero(response, UDP_DATAGRAM_SIZE);

  finalresponse = malloc(TCP_DATAGRAM_SIZE);
  bzero(finalresponse, TCP_DATAGRAM_SIZE);

  maxim = malloc (DNSREWRITE);
  bzero(maxim, DNSREWRITE);

  response_ptr = response;
  finalresponse_ptr = finalresponse;

  /* DNS header added to TCP replies: 2-bytes lenght of the corresponding UDP/DNS usual wireformat. limit 64K */
  // 20200825 M FANTUZZI explain correct usage of this option
  if (xproto == 1) {
    int norm = (dns_req->tcp_size);
    response[0] = (uint8_t)(norm >> 8);
    response[1] = (uint8_t)norm;
    response+=2;
    //if (DNSDUMP) {
    //  printf(" *** TCP HEADER ON DNS WIRE PACKET: read tcp_size		: %d\n",(uint8_t)(dns_req->tcp_size));
    //  printf(" *** TCP HEADER ON DNS WIRE PACKET: read dns_req		: %d\n",(uint8_t)(sizeof(dns_req) - 2));
    //}
  }

  /* Transaction ID */
  response[0] = (uint8_t)(dns_req->transaction_id >> 8);
  response[1] = (uint8_t)dns_req->transaction_id;
  // CLOUDFLARE
  //response[0] = 0x00; //(uint8_t)(dns_req->transaction_id >> 8);
  //response[1] = 0x00; //(uint8_t)dns_req->transaction_id;
  // RFC/GOOGLE
  //response[0] = 0xAB; //(uint8_t)(dns_req->transaction_id >> 8);
  //response[1] = 0xCD; //(uint8_t)dns_req->transaction_id;
  //response+=2;

  int kkk = 0;
  //for (int x=2;x<get_size()+2;x++) {
  for (int x=2;x<get_size();x++) {
    //*response++ = ip[x];
    response[x] = ip[x];
    kkk++;
  }

  response+=kkk+2;

  if (mode == DNS_MODE_ANSWER) {
    int yclient_len = sizeof(yclient);
    yclient->sin_family = AF_INET;
    yclient->sin_port = yclient->sin_port;
    memset(&(yclient->sin_zero), 0, sizeof(yclient->sin_zero));

    /* TCP packet-length DNS header re-stamping */
    if (xproto == 1) {
      int resulttt = NULL;
      int norm2 = (response - response_ptr - xtcpoff); // account for 2 extra tcp bytes
      check = 1;
      if (DNSDUMP) {
	printf(" *** OFFSET from norm2 -> %d\n",norm2);
      }

      finalresponse[0] = (uint8_t)(norm2 >> 8);
      finalresponse[1] = (uint8_t)norm2;

      /* start off 3rd byte to leave the overwritten tcp_size value intact */
      for (int i=2; i < (response - response_ptr); i++) {
          resulttt <<= 8;
      	  resulttt |= response_ptr[i];
      	  finalresponse_ptr[i]+= resulttt;
      }

      finalresponse+=(response-response_ptr);

      /* dump to TCP wireformat */
      if (DNSDUMP) {
        printf(" *** TCP SENT %d bytes of finalresponse\n", finalresponse - finalresponse_ptr);
        printf(" *** DUMP of (finalresponse_ptr, LENGTH response - response_ptr)\n");
      	hexdump(finalresponse_ptr, response - response_ptr);
        printf(" *** TCP SENT %d bytes of finalresponse (including +2 for TCP)\n", finalresponse - finalresponse_ptr);
        printf(" *** DUMP of (finalresponse_ptr, LENGTH finalresponse - finalresponse_ptr)\n");
        hexdump(finalresponse_ptr, finalresponse - finalresponse_ptr);
      }
    } else {
      /* dump to classi UDP wireformat */
      if (DNSDUMP) {
        //printf(" *** UDP SENT %d bytes of finalresponse\n", finalresponse - finalresponse_ptr);
        //printf(" *** DUMP of (finalresponse_ptr, LENGTH response - response_ptr)\n"); 
        //hexdump(finalresponse_ptr, response - response_ptr);
        printf(" *** UDP SENT %d bytes of response\n", response - response_ptr);
        printf(" *** DUMP of (response_ptr, LENGTH response - response_ptr)\n"); 
        hexdump(response_ptr, response - response_ptr);
      }
    }
    
    /* send contents back onto the same socket */
    if (check != 1) {
      bytes_sent_udp = sendto(sd, (const char*)response_ptr, response - response_ptr, 0, (struct sockaddr *)yclient, 16);
      wait(bytes_sent_udp);
      close(sd);
      free(response_ptr);
      check = 0;
    } else if (xproto == 1) {
      check = 0;
      bytes_sent_tcp = sendto(sd, finalresponse_ptr, finalresponse - finalresponse_ptr, MSG_DONTWAIT, (struct sockaddr *)yclient, 16);
      wait(bytes_sent_tcp); 
      shutdown(sd,SHUT_RD);
      close(sd);
      // 20200824 - M FANTUZZI - comment-out
      //free(response_ptr);
      free(finalresponse_ptr);
      return;
    }

    close(sd);
    //free(response_ptr);
    //free(finalresponse_ptr);
    //fdatasync(sd);
    //free(rip);
    free(dns_req);
    return;

  } else if (mode == DNS_MODE_ERROR) {
  
    int yclient_len = sizeof(yclient);
    if (EXT_DEBUG) { fprintf(stdout, " *** DNS_MODE_ERROR\n"); }
    //(struct sockaddr *)xclient->sin_family = AF_INET;
    /* few lines left for reference, useful to understand sin_addr and sin_port struct */
    //yclient->sin_addr.s_addr = inet_addr("192.168.2.84"); 
    //yclient->sin_port = htons(yclient->sin_port);
    yclient->sin_family = AF_INET;
    yclient->sin_port = yclient->sin_port;
    memset(&(yclient->sin_zero), 0, sizeof(yclient->sin_zero)); // zero the rest of the struct 
    //memset(yclient, 0, 0);
    if (DNSDUMP) { hexdump(response_ptr, response - response_ptr ); }
    bytes_sent = sendto(sd, response_ptr, response - response_ptr, MSG_EOR, (struct sockaddr *)yclient, 16);
    printf(" *** AN ERRONEOUS PARSE HAPPENED\n");
    close(sd);
    //free(rip);
    free(dns_req);
    free(response_ptr);

  } else {

    fprintf(stdout, " *** DNS_MODE_UNKNOWN\n");
    if (DNSDUMP) { hexdump(response_ptr, response - response_ptr); }
    bytes_sent = sendto(sd, response_ptr, response - response_ptr, 0, (struct sockaddr *)yclient, 16);
    printf(" *** AN UNKNOWN PARSE HAPPENED\n");
    close(sd);
    //free(rip);
    free(dns_req);
    free(response_ptr);
  }

  if (DNSDUMP) { printf(" *** YYY SENT %d bytes\n", (uint32_t)bytes_sent); }
}

/* Builds and sends the dns response datagram talking Massimiliano Fantuzzi's pre-DoH format */
void build_dns_response(int sd, struct sockaddr_in *yclient, struct dns_request *dns_req, const char *ip, int mode, size_t xrequestlen, long int ttl, int protoq, int xtcpoff) {

  char *rip = malloc(256 * sizeof(char));
  //struct dns_request *dns_req, sockaddr_in *client;
  
  int i,ppch, check = 0, sockfd, xsockfd; // params->xsockfd, params->xsockfd;
  //struct dns_request *xhostname = (struct dns_request *)xhostname->hostname;
  // dns_req->hostname, typeq,
  char *response, *finalresponse, *qhostname, *token, *pch, *maxim, *rr, *tt, *response_ptr, *finalresponse_ptr;
  ssize_t bytes_sent, bytes_sent_tcp, bytes_sent_udp, bytes_encoded;

  if (DEBUG) {
    printf("\n");
    /*
    printf("BUILD-yclient->sin_addr.s_addr		: %u\n", (uint32_t)(yclient->sin_addr).s_addr);
    printf("BUILD-yclient->sin_port			: %u\n", (uint32_t)(yclient->sin_port));
    printf("BUILD-yclient->sin_family		: %d\n", (uint32_t)(yclient->sin_family));
    */
    printf("-> BUILD-xrequestlen			: %d\n", (uint32_t)(xrequestlen));
    printf("-> BUILD-ttl				: %d\n", (uint32_t)(ttl));
    printf("-> BUILD-xtcpoff			: %d\n", (uint32_t)(xtcpoff));
    printf("-> BUILD-Xsockfd			: %u\n", xsockfd);
    printf("-> BUILD- sockfd			: %d\n", sockfd);
    printf("-> BUILD-proto				: %d\n", protoq);
  }

  response = malloc(UDP_DATAGRAM_SIZE);
  bzero(response, UDP_DATAGRAM_SIZE);

  finalresponse = malloc(TCP_DATAGRAM_SIZE);
  bzero(finalresponse, TCP_DATAGRAM_SIZE);

  //bzero(dns_req->query, sizeof(dns_req->query));
  //memcpy(dns_req->query, udp_request, sizeof(dns_req->query)-1);

  maxim = malloc (DNSREWRITE);
  bzero(maxim, DNSREWRITE);

  response_ptr = response;
  finalresponse_ptr = finalresponse;

  /* DNS header added when using TCP, represents the lenght in 2-bytes for the corresponding UDP/DNS usual wireformat. limit 64K */
  if (protoq == 1) {
    int norm = (dns_req->tcp_size);
    //response[0] = 0x00; //response[1] = 0x35; // testing with 55 bytes responses, as for A news.infomaniak.com
    //response[1] = sizeof(response_ptr); // 55 bytes
    response[0] = (uint8_t)(norm >> 8);
    response[1] = (uint8_t)norm;
    response+=2;
    if (DNSDUMP) {
      printf(" *** TCP HEADER ON DNS WIRE PACKET: read tcp_size		: %d\n",(uint8_t)(dns_req->tcp_size));
      printf(" *** TCP HEADER ON DNS WIRE PACKET: read dns_req		: %d\n",(uint8_t)(sizeof(dns_req) - 2));
    }
  }

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
    /* Questions: 1 */
    response[0] = 0x00;
    response[1] = 0x01;
    response+=2;
    /* Answers: 1 */
    response[0] = 0x00;
    response[1] = 0x01;
    response+=2;
    if (EXT_DEBUG) { printf(" *** EXITING MODE_ANSWER\n"); }
  
  } else if (mode == DNS_MODE_ERROR) {
      
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
    if (EXT_DEBUG) { printf(" *** EXITING MODE_ERROR\n"); }
  }
  
  /* Authority RRs 0 */
  /* Scope of DNSP was to minimise answers ... this part is yet to be perfect */
  response[0] = 0x00;
  response[1] = 0x00;
  response+=2;
  
  /* Additional RRs 0 */
  /* See Authority section, same comments */
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
        printf("\n *** NO PARSE HAPPENED\n\n");
        return NULL;
        //*response++ = 0x00;
        //*response++ = 0x01;
    }
    
    /* Class IN */
    /* other classes to be supported, not sure is a MUST but shall be implemented for general compatibility */
    *response++ = 0x00;
    *response++ = 0x01;

    /* TTL: interpretation of HTTP headers completed */
    /* 0000: Cache-control: public, max-age=276, s-maxage=276 */

    int i=1, j;
    //int i=1, j, temp;
    
    long int decimalNumber = ttl, remainder, quotient;
    //quotient = decimalNumber;
 
    //int swapped = ((ttl>>24)&0xff) | // move byte 3 to byte 0
    //                ((ttl<<8)&0xff0000) | // move byte 1 to byte 2
    //                ((ttl>>8)&0xff00) | // move byte 2 to byte 1
    //                ((ttl<<24)&0xff000000); // byte 0 to byte 3

    //printf("TTL REV REV     %08x\n", REV(REV(ttl)));
    //printf("quotient REV    %08x\n", REV(quotient));
    //printf("quotient        %02x\n", quotient);
    //printf("swapped         %02x\n", swapped);

    response[0] = R4(ttl);
    response[1] = R3(ttl);
    response[2] = R2(ttl);
    response[3] = R1(ttl);
    response+=4;

    /*
    while(quotient!=0) {
      //if (quotient <10)
      if (quotient <16) {
        printf("%1X\n", quotient);
        break;
      }

      temp = quotient % 16;
      
      // To convert integer into character
      if( temp < 10) {
        temp = temp + 48;
        //temp = temp - 30;
      } else {
        temp = temp + 55;
        //temp = temp - 37;
      }
      
      printf("%1X\n", temp);
      quotient = quotient / 16;
    }
    */

    //printf("%d\n", (unsigned char)strtol(swapit, NULL, 16));

    /*
    int c=0, x;
    long int dec = ttl;
    char a[4];

    //while(dec>0)
    while(dec!=0) {
      if (dec < 16) break;
      //if (dec < 10) break;
      a[c]=dec%16;
      if( a[c] < 10)
          a[c] = a[c] + 48;
      else
          temp = temp + 55;
      //response[0] = a[c];
      *response++ = a[c];
      //response++;

      dec=dec/16;
      c++;
    }

    for(x=c-1;x>=0;x--) {
    	if(a[x]>=10) {
            if (DNSDUMP) {
    		    printf("%c",a[x]+55);
                printf(" ... a[x] BIGGER than 10\n\n",a[x]);
            }
    	} else {
            if (DNSDUMP) {
    		    printf("%d",a[x]);
                printf(" ... a[x] smaller than 10\n\n",a[x]);
            }
    	}
    }
    
    if (DNSDUMP) {
        printf(" *** a[c] post-conversion %d\n",a[c]);
        printf(" *** c    post-conversion %d\n",c);
    }
    */

    /*
    if (DNSDUMP) {
      printf(" *** raw-answer: response_ptr, response - response_ptr\n");
      hexdump(response_ptr, response - response_ptr);
    }
    */

    /* DNS request TYPE parsing */
    if (dns_req->qtype == 0x0c) {
      // PTR
      /* Data length (4 bytes) */
      response[0] = 0x00;
      response[1] = 0x04;
      response+=2;
      response[0] = 0xc0;
      response[1] = 0x0c;
      response+=2;

    } else if (dns_req->qtype == 0x02) { 
      // NS
      char *newline = strchr(ip,"\r\n\t");
      if ( newline ) *newline = 0;
      //printf ("%s",newline);
      pch = strtok((char *)ip,". ");
      response[0] = 0x00;
      response[1] = (strlen(ip)-1);
      response+=2;

      while (pch != NULL) {
      	ppch = strlen(pch);
      	*response++ = strlen(pch);
      	for (i = 0; i < strlen(pch); ++i) {
      	  *response++ = pch[i];
      	  maxim[i] = pch[i];
      	}

      	pch = strtok(NULL, ". ");
      	//if (pch != ' ' && pch != '\t' && pch != NULL)
      	//if (pch == ' ' || pch == '\t' || pch == NULL || pch == '\n' || pch == '\r')
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
      /* *response++ = 0xc0; *response++ = 0x0c; *response++ = 0xc0; *response++ = 0x2b; */

    } else if (dns_req->qtype == 0x05) {
      // CNAME
      response[0] = 0x00;
      response[1] = (strlen(ip)-1);
      response+=2;

      pch = strtok((char *)ip,". \r\n\t");

      while (pch != NULL) {
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
      // MX
      /* Data length accounting for answer length plus final dot and termination field */
      response[0] = 0x00;
      response[1] = (strlen(ip)+3);
      /* int qrr = (strlen(ip)+3); response[0] = (uint8_t)(qrr >> 8); response[1] = (uint8_t)qrr; */

      /* PRIO (4 bytes)*/
      response[0] = 0x00; response[1] = 0x0a; response+=2;

      /* POINTER, IF YOU ARE SO BRAVE OR ABLE TO USE IT (4 bytes) -> do not use label-mode then ..
       * in that case, you should re-write the code to have super-duper minimal responses.
       * That code would also need to implement domain comparison to check if suffix can be appended */
      //response[0] = 0xc0; //response[1] = 0x0c; //response+=2;

      /*
      char *newline = strchr(ip,"\r\n");
      if ( newline ) *newline = 0;
      char * line;
      line = rtrim(ip,"\r\n\t");
      pch = strtok(line,".");
      */

      pch = strtok((char *)ip,".\t\r\n");
      while (pch != NULL) {
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
          *response++ = ppch-1;
          for (i = 0; i < ppch-1; ++i) {
            *response++ = maxim[i];
          }
        }
      }
      *response++ = 0x00;
    	
    } else if (dns_req->qtype == 0x01) {
      // A
      /* Data length (4 bytes)*/
      *response++ = 0x00; *response++ = 0x04;

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
      fprintf(stdout, " *** DNS_MODE_ISSUE, no headers to parse !\n");
      return;
    }

    /*
     * DUMMY ADDITIONAL SECTION (trailing ?)
        *response++ = 0x00; *response++ = 0x00; *response++ = 0x29; *response++ = 0x10;
        *response++ = 0x00; *response++ = 0x00; *response++ = 0x00; *response++ = 0x00;
        *response++ = 0x00; *response++ = 0x00; *response++ = 0x00;
    */

    //*response++=(unsigned char)(strlen(ip)+1);
    //memcpy(response,ip,strlen(ip)-1);
    //strncpy(response,ip,strlen(ip)-1);
    
    int yclient_len = sizeof(yclient);
    yclient->sin_family = AF_INET;
    yclient->sin_port = yclient->sin_port;
    memset(&(yclient->sin_zero), 0, sizeof(yclient->sin_zero));
    //memset(yclient, 0, 0);

    /* save HEX packet structure to file, from dns_response->payload for dns_response->length */
    /* possible use-case: feed local disk cache, or serve it as HTTP content from DNSP daemon */

    /*
    #define MAX_LENGTH 512
    FILE *fout = fopen("out.txt", "w");

    if(ferror(fout)) {
        fprintf(stderr, "Error opening output file");
        return 1;
    }
    char init_line[]  = {"char hex_array[] = { "};
    const int offset_length = strlen(init_line);

    char offset_spc[offset_length];

    unsigned char buff[1024];
    char curr_out[1024];
    //char curr_out[64];

    int count, i;
    int line_length = 0;

    memset((void*)offset_spc, (char)32, sizeof(char) * offset_length - 1);
    offset_spc[offset_length - 1] = '\0';

    fprintf(fout, "%s", init_line);

    // NOT USEFUL TO USE A WHILE-LOOP (got from CURLLIB examples)
    while(!feof(stdin)){
      //count = fread(buff, sizeof(char), sizeof(buff) / sizeof(char), stdin);
      count = sizeof(response);

      for(i = 0; i < count; i++) {
        line_length += sprintf(curr_out, "%#x, ", buff[i]);

        fprintf(fout, "%s", curr_out);
        if(line_length >= MAX_LENGTH - offset_length) {
          fprintf(fout, "\n%s", offset_spc);
          line_length = 0;
        }
      }
    }
    
    fseek(fout, -2, SEEK_CUR);
    fprintf(fout, " };\n");
    fclose(fout);
    */

    /* WHERE IS EDNS ?? */
    /* ... at the end of DNS query, 6 to 11 bytes ?? */

    /* TCP packet-length DNS header re-stamping */
    if (protoq == 1) {
      int resulttt = NULL;
      // 20200824 comment: is it a - or a + ... ??? xtcpoff
      int norm2 = (response - response_ptr - xtcpoff); // account for 2 extra tcp bytes
      check = 1;
      if (DNSDUMP) { printf(" *** BYTES in norm2 -> %d\n",norm2); }
      
      finalresponse[0] = (uint8_t)(norm2 >> 8);
      finalresponse[1] = (uint8_t)norm2;

      /* start off 3rd byte to leave the overwritten tcp_size value intact */
      for (int i=2; i < (response - response_ptr); i++) {
          resulttt <<= 8;
          resulttt |= response_ptr[i];
          finalresponse_ptr[i]+= resulttt;
      }

      finalresponse+=(response-response_ptr);
      
      if (DNSDUMP) { 
	printf(" *** TCP SENT %d bytes of finalresponse\n", finalresponse - finalresponse_ptr);
	printf(" *** DUMP of (finalresponse_ptr, response - response_ptr)\n");
	hexdump(finalresponse_ptr, response - response_ptr);

	printf(" *** TCP SENT %d bytes of finalresponse (including +2 for TCP)\n", finalresponse - finalresponse_ptr);
	printf(" *** DUMP of (finalresponse_ptr, finalresponse - finalresponse_ptr)\n");
	hexdump(finalresponse_ptr, finalresponse - finalresponse_ptr);
      }

      // MSG_OOB, MSG_NOSIGNAL, MSG_EOR, MSG_MORE, MSG_WAITALL, MSG_CONFIRM, MSG_DONTWAIT
      // msg_flags=MSG_TRUNC|MSG_DONTWAIT|MSG_EOR|MSG_WAITALL|MSG_CONFIRM|MSG_ERRQUEUE|MSG_MORE|MSG_WAITFORONE
      //sendto(sd, finalresponse_ptr, finalresponse - finalresponse_ptr, MSG_EOR, (struct sockaddr *)yclient, 16);
      //write(sd, (const char*)finalresponse_ptr, finalresponse - finalresponse_ptr); 
    }

    /* dump to udpwireformat */
    if (DNSDUMP) {
      printf(" *** XXX SENT %d bytes of response\n", response - response_ptr);
      printf(" *** DUMP of (response_ptr, OF LENGTH OF response - response_ptr)\n"); 
      hexdump(response_ptr, response - response_ptr);
    }

    /* send contents back onto the same socket */
    if (check != 1) {
      bytes_sent_udp = sendto(sd, (const char*)response_ptr, response - response_ptr, 0, (struct sockaddr *)yclient, 16);
      wait(bytes_sent_udp);
      close(sd);
      free(response_ptr);
      check = 0;
      // 20200824 comment: is it a 0 or NULL ? check
    } else if (protoq == 1) {
      bytes_sent_tcp = sendto(sd, finalresponse_ptr, finalresponse - finalresponse_ptr, MSG_DONTWAIT, (struct sockaddr *)yclient, 16);
      wait(bytes_sent_tcp); 
      // 20200220 - MAX FANTUZZI - WARNING !
      // RST
      shutdown(sd,SHUT_RD);
      close(sd);
      free(response_ptr);
      free(finalresponse_ptr);
      return;
    }

    close(sd);
    //free(response_ptr);
    //free(finalresponse_ptr);
    //fdatasync(sd);
    free(rip);
    free(dns_req);
    return;

  } else if (mode == DNS_MODE_ERROR) {

    if (EXT_DEBUG) { fprintf(stdout, " *** DNS_MODE_ERROR\n"); }
    //(struct sockaddr *)xclient->sin_family = AF_INET;
    int yclient_len = sizeof(yclient);

    /* few lines left for reference, useful to understand sin_addr and sin_port struct */
    //yclient->sin_addr.s_addr = inet_addr("192.168.2.84"); 
    //yclient->sin_port = htons(yclient->sin_port);
    yclient->sin_family = AF_INET;
    yclient->sin_port = yclient->sin_port;
    memset(&(yclient->sin_zero), 0, sizeof(yclient->sin_zero)); // zero the rest of the struct 
    //memset(yclient, 0, 0);

    if (DNSDUMP) { hexdump(response_ptr, response - response_ptr ); }

    bytes_sent = sendto(sd, response_ptr, response - response_ptr, MSG_EOR, (struct sockaddr *)yclient, 16);
    printf(" *** AN ERRONEOUS PARSE HAPPENED\n");
    close(sd);
    //free(rip);
    free(dns_req);
    free(response_ptr);

  } else {
    fprintf(stdout, " *** DNS_MODE_UNKNOWN\n");
    if (DNSDUMP) { hexdump(response_ptr, response - response_ptr); }
    bytes_sent = sendto(sd, response_ptr, response - response_ptr, 0, (struct sockaddr *)yclient, 16);
    printf(" *** AN UNKNOWN PARSE HAPPENED\n");
    close(sd);
    free(rip);
    free(dns_req);
    free(response_ptr);
  }

  if (DNSDUMP) { printf(" *** YYY SENT %d bytes\n", (uint32_t)bytes_sent); }
  //flag = NULL;
  //free(rip);
}

/* homemade substring function */
char *substring(char *string, int position, int length) {
   char *pointer;
   int c;
 
   pointer = malloc(length+1);
 
   if (pointer == NULL) {
      printf("Unable to allocate memory.\n");
      exit(1);
   }
 
   for (c = 0 ; c < length ; c++) {
      *(pointer+c) = *(string+position-1);      
      string++;   
   }
 
   *(pointer+c) = '\0';
   return pointer;
}
 
/* struct to support libCurl callback */
struct MemoryStruct {
  char *memory;
  size_t size;
};
 
/* new libCurl callback */
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
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
static size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream) {
  size_t stream_size;
  stream_size = size * nmemb + 1;
  bzero(stream, HTTP_RESPONSE_SIZE);
  memcpy(stream, ptr, stream_size);
  //printf("\n---\n%s\n---\n",stream);
  //printf("\n%s\n",stream);
  //printf(substring(stream,31,5));
  return stream_size-1;
}

/* handler checker/limiter */
static int hnd2num(CURL *ch) {
  int i;
  for(i = 0; i< num_transfers; i++) {
    if(curl_hnd[i] == ch)
      return i;
  }
  /* weird, but just a fail-safe */ 
  return 0;
}

static void dump(const char *text, int num, unsigned char *ptr, size_t size, char nohex) {
  size_t i;
  size_t c;
  unsigned int width = 0x10;

  /* without the hex output, we can fit more on screen */
  if(nohex) width = 0x40;

  fprintf(stderr, "%d %s, %ld bytes (0x%lx)\n", num, text, (long)size, (long)size);

  for(i = 0; i<size; i += width) {

    fprintf(stderr, "%4.4lx: ", (long)i);

    if(!nohex) {
      /* hex not disabled, show it */
      for(c = 0; c < width; c++)
        if(i + c < size)
          fprintf(stderr, "%02x ", ptr[i + c]);
        else
          fputs("   ", stderr);
    }

    for(c = 0; (c < width) && (i + c < size); c++) {
      /* check for 0D0A; if found, skip past and start a new line of output */
      if(nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D && ptr[i + c + 1] == 0x0A) {
        i += (c + 2 - width);
        break;
      }
      fprintf(stderr, "%c", (ptr[i + c] >= 0x20) && (ptr[i + c]<0x80)?ptr[i + c]:'.');
      /* check again for 0D0A, to avoid an extra \n if it's at width */
      if(nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D && ptr[i + c + 2] == 0x0A) {
        i += (c + 3 - width);
        break;
      }
    }
    /* newline */ 
    fputc('\n', stderr);
  }
  fflush(num);
}

static void dumptwo(const char *text, FILE *stream, unsigned char *ptr, size_t size, char nohex) {
  size_t i;
  size_t c;
  unsigned int width = 0x10;
 
  /* without the hex output, we can fit more on screen */ 
  if(nohex) width = 0x40;
 
  fprintf(stream, "%s, %10.10lu bytes (0x%8.8lx)\n", text, (unsigned long)size, (unsigned long)size);
 
  for(i = 0; i<size; i += width) {
 
    fprintf(stream, "%4.4lx: ", (unsigned long)i);
 
    if(!nohex) {
      /* hex not disabled, show it */ 
      for(c = 0; c < width; c++)
        if(i + c < size)
          fprintf(stream, "%02x ", ptr[i + c]);
        else
          fputs("   ", stream);
    }
 
    for(c = 0; (c < width) && (i + c < size); c++) {
      /* check for 0D0A; if found, skip past and start a new line of output */ 
      if(nohex && (i + c + 1 < size) && ptr[i + c] == 0x0D && ptr[i + c + 1] == 0x0A) {
        i += (c + 2 - width);
        break;
      }
      fprintf(stream, "%c", (ptr[i + c] >= 0x20) && (ptr[i + c]<0x80)?ptr[i + c]:'.');
      /* check again for 0D0A, to avoid an extra \n if it's at width */ 
      if(nohex && (i + c + 2 < size) && ptr[i + c + 1] == 0x0D && ptr[i + c + 2] == 0x0A) {
        i += (c + 3 - width);
        break;
      }
    }
    /* newline */ 
    fputc('\n', stream);
  }
  fflush(stream);
}

static size_t header_handler(void *ptr, size_t size, size_t nmemb, void *userdata) {
    char *x = calloc(size + 1, nmemb);
    assert(x);
    memcpy(x, ptr, size * nmemb);
    printf("New header:\n%s\n", x);
    return size * nmemb;
}

/*
pub struct curl_fileinfo {
    pub filename: *mut c_char,
    pub filetype: curlfiletype,
    pub time: time_t,
    pub perm: c_uint,
    pub uid: c_int,
    pub gid: c_int,
    pub size: curl_off_t,
    pub hardlinks: c_long,
    pub strings_time: *mut c_char,
    pub strings_perm: *mut c_char,
    pub strings_user: *mut c_char,
    pub strings_group: *mut c_char,
    pub strings_target: *mut c_char,
    pub flags: c_uint,
    pub b_data: *mut c_char,
    pub b_size: size_t,
    pub b_used: size_t,
}
*/

/* BEWARE: libcurl does not unfold HTTP "folded headers" (deprecated since RFC7230). */
/* A folded header is a header that continues on a subsequent line and starts with a whitespace. */
/* Such folds will be passed to the header callback as a separate one, although strictly it is just a continuation of the previous line. */
/* A complete HTTP header that is passed to this function can be up to CURL_MAX_HTTP_HEADER (100K) bytes. */

int get_size(void) {
        return size_test;
}

void set_size(int rqsize) {
        size_test = rqsize;
}

int get_data(void) {
        return data_test;
}

void set_data(char buffer) {
        data_test = buffer;
}

int get_ttl(void) {
        return ttl_out_test;
}

void set_ttl(int new_value) {
        ttl_out_test = new_value;
}

struct data {
  char trace_ascii; /* 1 or 0 */ 
  int ttl_out_test_data;
};

// 20200824 20191107 max fantuzzi - reviewed
//static int my_trace(CURL *handle, curl_infotype type, char *data, size_t size) {
//static int my_trace(CURL *handle, curl_infotype type, char *data, size_t size, struct data *userp) {
static int my_trace(CURL *handle, curl_infotype type, char *data, size_t size, void *userp) {
  const char *text;
  int num = hnd2num(handle);
  /* prevent compiler warning */ 
  (void)handle;
  
  (void)userp;
  //struct data *config = (struct data *)userp;

  switch(type) {
  case CURLINFO_TEXT:
    if (DEBUGCURL) {
	fprintf(stderr, "== %d Info        : %s", num, data);
    }
    /* fallthrough */ 
  default: /* in case a new one is introduced to shock us */ 
    return 0;
 
  case CURLINFO_HEADER_OUT:
    text = "=> Send header";
    if (DEBUGCURL) { fprintf(stderr, "== %d Header out  : %s\n", num, data); }
    break;
  case CURLINFO_DATA_OUT:
    text = "=> Send data";
    if (DEBUGCURL) { fprintf(stderr, "== %d Data out    : %s\n", num, data); }
    break;
  case CURLINFO_SSL_DATA_OUT:
    text = "=> Send SSL data";
    if (DEBUGCURL) { fprintf(stderr, "== %d SSL Data out: %s", num, data); }
    break;
  case CURLINFO_HEADER_IN:
    text = "<= Recv header";
    if (DEBUGCURL) {
	fprintf(stderr, "== %d Header in   : %s", num, data);
    }

    /* Parse headers as tokens, find "cache-control" header */
    /* Then extract TTL validity out of either maxage/s-maxage */

    char** tokens;
    char *compare;
    char ref[14] = "cache-control";
    compare = substring(data, 1, 13);
    strcpy(ref, "cache-control");
    int cacheheaderfound = strcmp(compare,ref), ttl_out;

    /* dumping all headers */
    //dump(text, num, (unsigned char *)data, size, 1);

    if(cacheheaderfound == 0) {
//  	if ( DEBUGCURL ) {
//	  dump(text, num, (unsigned char *)data, size, 0);
//	} else {
//	  printf(" *** NO CACHE DATA FOUND\n");
//	}
   
  	// More general pattern
  	const char *my_str_literal = data;
  	char *token, *str, *tofree;
  	
  	tofree = str = strdup(my_str_literal);  // We own str's memory now.

  	while ((token = strsep(&str, ","))) {
    	  if ( DEBUGCURL ) {
	    printf(" ----> %s\n",token);
	  }
    	  continue;
    	}
      
  	free(tofree);
	//printf("\n ----> %s\n", data);
	
        //from: see https://developer.mozilla.org/fr/docs/Web/HTTP/Headers/Cache-Control
        //max-age=<secondes>
        //Indique la durée pendant laquelle la ressource doit être considérée comme valide (non expirée).
        //Contrairement à expires, la durée indiquée dans cette directive commence à la date de la requête.
        //s-maxage=<secondes>
        //Indique une valeur pour écraser les valeurs définies par max-age ou Expires pour les caches
        //partagés (comme les proxies). Il est donc ignoré par les caches privés (dont les navigateurs).

        char *str2, *tofree2;
        tofree2 = str2 = strdup(my_str_literal);  // We own str's memory now.

        tokens = str_split(data, ',');

        if (tokens != NULL) {
          char *compare2;
          char ref2[8];
          strcpy(ref2, "max-age");

          //while ((tokens = strsep(&str2, "="))) printf(" ----> %s\n",tokens);

          for (int ff = 0; *(tokens + ff); ff++) {
            char *p = *(tokens + ff);
            //printf("OOO: %s\n---\n", *(tokens + ff));
            //compare2 = substring(tokens, 1, 6);
            compare2 = substring(*(tokens + ff), 2, 7);
            int maxagevaluefound = strcmp(compare2,ref2);
            while (*p) {
              if ( isdigit(*p) || ( (*p=='-'||*p=='+') && isdigit(*(p+1)) )) {
                long val = strtol(p, &p, 10); // Read number
                if (maxagevaluefound == 0) {
                  set_ttl(val);
                  //ttl_out_test = 666;
                  //userp = val;
                  if (DEBUGCURL) {
                    printf(" ----> max-age token: %ld\n", val);
                    printf(" ----> get_ttl token: %d\n", get_ttl());
                    //dumptwo(text, stderr, (unsigned char *)data, size, config->trace_ascii);
                  }
                }
              } else {
                p++;
              }
            }
            free(*(tokens + ff));
          }
          free(tokens);
          ref == NULL;
          //return 0;
        }
    }
    break;
  case CURLINFO_DATA_IN:
    text = "<= Recv data";
    if ( DNSDUMP ) {
      /* dumping response body */
      dump(text, num, (unsigned char *)data, size, 0);
      //hexdump(data, size);
    }
    /* export response body into set_data */
    set_data(data);
    set_size(size);
    break;
  case CURLINFO_SSL_DATA_IN:
    text = "<= Recv SSL data";
    break;
  }
 
  return 0;
}

static void setup(CURL *hndin, char *script_target) {
  FILE *out; // = fopen(OUTPUTFILE, "wb");
  //FILE *out = fopen(OUTPUTFILE, "wb");
  int num = 1;
  //char *q = ( char * ) malloc( 512 * sizeof( char ) );
  char q[512];
  char filename[128];
  struct data config;

  config.trace_ascii = 1; /* enable ascii tracing */ 
  
  /* avoid hardcoding services: provide parameter (or a static list, for blind root-check) with parallel threads */ 
  snprintf(q, sizeof(q)-1, "%s", script_target);
  //snprintf(q, URL_SIZE-1, "%s", script_target);
  //snprintf(q, sizeof(q)-1, "https://php-dns.appspot.com/%s", script_target);
  //snprintf(q, sizeof(q)-1, "https://cloudflare-dns.com/dns-query?dns=%s", script_target);
  //snprintf(script_url, URL_SIZE-1, "https://cloudflare-dns.com/dns-query?dns=%s", b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9));

  curl_easy_setopt(hndin, CURLOPT_URL, q);
  curl_easy_setopt(hndin, CURLOPT_NOSIGNAL, 1L);

  if (DEBUGCURLTTL) { curl_easy_setopt(hndin, CURLOPT_VERBOSE,  1); } else { curl_easy_setopt(hndin, CURLOPT_VERBOSE,  0); }
  
  snprintf(filename, 128, "dnsp-h2.response-%d", num);
  out = fopen(filename, "wb");

  // write to this file to be served back from "cache" via HTTP/2
  //curl_easy_setopt(hndin, CURLOPT_WRITEDATA, (void *)&chunk);
  //curl_easy_setopt(hndin, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt(hndin, CURLOPT_WRITEDATA, out);
  
  curl_easy_setopt(hndin, CURLOPT_DEBUGDATA, &config);
  curl_easy_setopt(hndin, CURLOPT_DEBUGFUNCTION, my_trace);

  curl_easy_setopt(hndin, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
  curl_easy_setopt(hndin, CURLOPT_CAPATH, "/etc/ssl/certs/");
  curl_easy_setopt(hndin, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(hndin, CURLOPT_SSL_VERIFYHOST, 0L);
  //curl_easy_setopt(hndin, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
  curl_easy_setopt(hndin, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT); //CURL_SSLVERSION_TLSv1
  curl_easy_setopt(hndin, CURLOPT_SSLENGINE, "dynamic");
  curl_easy_setopt(hndin, CURLOPT_SSLENGINE_DEFAULT, 1L);
  // OCSP not always available on CloudFlare or other cloud providers (OK for Google's GCP, still need to test with AWS)
  curl_easy_setopt(hndin, CURLOPT_SSL_VERIFYSTATUS, 0L);
  
  //curl_easy_setopt(hndin, CURLOPT_PROXY, proxy_host);
  //curl_easy_setopt(hndin, CURLOPT_PROXYPORT, proxy_port);	
 
  #if (CURLPIPE_MULTIPLEX > 0)
    // wait for pipe connection to confirm
    curl_easy_setopt(hndin, CURLOPT_PIPEWAIT, 1L);
    //curl_easy_setopt(hndin, CURLOPT_PIPEWAIT, 0L);
  #endif
 
  //fprintf(stderr, " *** CURL SETUP : %s\n", q);

  // placeholder, can parallelise N threads but is blocking/waiting. About the intent of spawning multiple/multipath things
  //curl_hndin[num] = 2;
  curl_easy_perform(hndin);
  //fclose(out);
  //curl_easy_cleanup(hndin);
  //curl_global_cleanup();
  //return 0;
}

/* called when there's an incoming push */ 
static int server_push_callback(CURL *parent, CURL *easy, size_t num_headers, struct curl_pushheaders *headers, void *userp) {
  char *headp;
  size_t i;
  int *transfers = (int *)userp;
  char filename[128];
  FILE *out;
  static unsigned int count = 0;
 
  (void)parent; /* we have no use for this */ 
 
  snprintf(filename, 128, "push%u", count++);
  /* here's a new stream, save it in a new file for each new push */ 
  out = fopen(filename, "wb");
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, out);
 
  fprintf(stderr, "**** push callback approves stream %u, got %d headers!\n", count, (int)num_headers);
 
  for(i = 0; i<num_headers; i++) {
    headp = curl_pushheader_bynum(headers, i);
    fprintf(stderr, "**** header %u: %s\n", (int)i, headp);
  }
 
  headp = curl_pushheader_byname(headers, ":path");
  if(headp) {
    fprintf(stderr, "**** The PATH is %s\n", headp /* skip :path + colon */ );
  }
 
  /* one more */ 
  (*transfers)++;
  return CURL_PUSH_OK;
}

/* Hostname lookup -> OK: Resolved IP, KO: Null */
char *lookup_host(const char *host, const char *proxy_host, unsigned int proxy_port, const char *proxy_user, const char *proxy_pass, const char *lookup_script, const char *typeq, unsigned int wport, char *rfcstring) {

  //CURL *ch;
  CURL *hnd;
  //CURL *hndtest[2];		// for CURLM and parallel
  //CURLSH *shobject = curl_share_init();
  //CURLSH *curlsh;
  //CURLM *multi_handle;

  /* keep number of running handles */ 
  int still_running;

  /* we start with one */ 
  int transfers = 1;

  CURLcode res;		// for CURLE error report
  //CURLcode ret;		// for CURLE error report
  int i, ret;
  char *http_response, *script_url, *script_url_alt, *script_get, *pointer;
  char base[2];

  /* CURL structs, different interfaces for different performances and needs */
  //struct curl_slist *hosting = NULL;
  struct curl_slist *list;
  //struct curl_slist *slist1;
  struct data config;
  struct CURLMsg *m;

  config.trace_ascii = 1; /* enable ascii tracing */ 
  config.ttl_out_test_data = ttl_out_test;

  /* hold result in memory */
  struct MemoryStruct chunk;
  chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
  chunk.size = 0;    /* no data at this point */ 

  script_url = malloc(URL_SIZE);
  script_url_alt = malloc(URL_SIZE);
  http_response = malloc(HTTP_RESPONSE_SIZE);
  bzero(script_url, URL_SIZE);
  bzero(script_url_alt, URL_SIZE);
  //bzero(stream, HTTP_RESPONSE_SIZE);
  //memcpy(stream, ptr, stream_size);
  
  //char *n = ( char * ) malloc( 80 * sizeof( char ) );
  char n[512];
  char ttt;

  /* Many other DoH services can be integrated (i.e. dns.google) */
  //snprintf(script_url, URL_SIZE-1, "%s?name=%s", lookup_script, host);

  /* here my pre-DoH request format, needs HOST and QTYPE */
  //snprintf(script_url, URL_SIZE-1, "%s?host=%s&type=%s", lookup_script, host, typeq);

  /* here the DOH RFC8484-ENCODED URL */
  //snprintf(script_url, URL_SIZE-1, "https://cloudflare-dns.com/dns-query?dns=%s", b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9));
  //snprintf(n, sizeof(n)-1, "https://cloudflare-dns.com/dns-query?dns=%s", b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9));
  
  char *temp = b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9);
  remchar(temp, '=');
  snprintf(script_url, URL_SIZE-1, "https://dns.google/dns-query?dns=%s", temp);
  //snprintf(script_url, URL_SIZE-1, "https://dns.google/dns-query?dns=%s", b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9));
  //snprintf(n, sizeof(n)-1, "https://dns.google/dns-query?dns=%s", b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9));
  //snprintf(n, sizeof(n)-1, "?host=%s&type=%s", host, typeq);

  /* See: https://developers.google.com/speed/public-dns/docs/doh */
  if (EXT_DEBUG) {
    //printf("URL: https://cloudflare-dns.com/dns-query?dns=%s\n", b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9));
    printf("URL: %s\n", script_url);
    //printf("URL: %s\n", n);
    printf("sizeof(rfcstring)+strlen(host)+9): %d\n", sizeof(rfcstring)+strlen(host)+9);
    //printf("URL: https://dns.google.com/query?name=%s&type=%s&dnssec=true\n",host,typeq);
  }
  
  //printf("DEBUG -> %s",base64_encode(host,strlen(host),((4 * strlen(host) / 3) + 3) & ~3));

  /* Beware of proxy-string: not every format is \"welcome\" to be cached .. and CURL fails silently here */
  //snprintf(proxy_url, URL_SIZE-1, "http://%s/", proxy_host);
  //if (proxy_host != NULL) { fprintf(stderr, "Required substring is \"%s\"\n", proxy_url); }

  /* HTTPS detection pseudocode .. not great, shall be way better. And deny non-HTTPS URLs */
  pointer = substring(script_url, 5, 1);
  strcpy(base, "s");

  int result = strcmp(pointer, base);
  //printf("Required substring is \"%s\"\n", pointer);
  //printf("Compared substring is \"%s\"\n", base);
  //printf("Result is \"%d\"\n", result);

  if(result == 0) {
    wport=443;
  } else {
    printf(" *** HTTP does NOT guarantee against MITM attacks.\n *** Consider switching to an HTTPS webservice for increased security !\n");
    wport=80;
  }

  free(pointer);

  /* a suitable low default, do that many transfers */ 
  num_transfers = 1;
  //if(!num_transfers || (num_transfers > NUM_HANDLES))

  /* curl setup, read: https://curl.haxx.se/libcurl/c/threadsafe.html to implement sharing and locks between threads */
  /* set specific nifty options for multi handlers or none at all */
  //ch = curl_easy_init();
  
  /* New section: spawn two threads, so that one queries Google and the othe goes to CLoudflare */
  /* This way, one answer can be cross-cheked and/or saver on filesystem */
  hnd = curl_easy_init();
  snprintf(script_url_alt, URL_SIZE-1, "https://cloudflare-dns.com/dns-query?dns=%s", temp);
  setup(hnd,script_url);
  //setup(hnd,lookup_script);

  // 20200220 20200824 - M FANTUZZI
  /* placeholder for DNS-over-HTTPS (DoH) GET or POST method of choice, to become CLI option ASAP */
  //curl_setopt($ch,CURLOPT_POST,1);
  //curl_setopt($ch,CURLOPT_POSTFIELDS,'customer_id='.$cid.'&password='.$pass);

  /*
  //curl_setopt($ch, CURLOPT_HEADER, 1L);
  curl_easy_setopt(ch, CURLOPT_URL, script_url);
  curl_easy_setopt(ch, CURLOPT_PORT, wport);

  // HTTP/2 prohibits connection-specific header fields. The following header fields must not appear
  // “Connection”, “Keep-Alive”, “Proxy-Connection”, “Transfer-Encoding” and “Upgrade”.
  // Additionally, “TE” header field must not include any value other than “trailers”.

  //curl_easy_setopt(ch, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
  //curl_easy_setopt(ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
  
  //curl_easy_setopt(ch, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
  //curl_easy_setopt(ch, CURLOPT_SSL_ENABLE_ALPN, 1L);
  //curl_easy_setopt(ch, CURLOPT_SSL_ENABLE_NPN, 1L);

  //curl_easy_setopt(ch, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
  //curl_easy_setopt(ch, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT); //CURL_SSLVERSION_TLSv1
  //curl_easy_setopt(ch, CURLOPT_SSLENGINE, "dynamic");
  //curl_easy_setopt(ch, CURLOPT_SSLENGINE_DEFAULT, 1L);

  curl_easy_setopt(ch, CURLOPT_FILETIME, 1L);
  curl_easy_setopt(ch, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(ch, CURLOPT_PROXY, proxy_host);
  curl_easy_setopt(ch, CURLOPT_PROXYPORT, proxy_port);	
  curl_easy_setopt(ch, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);

  if ((proxy_user != NULL) && (proxy_pass != NULL)) {
      curl_easy_setopt(ch, CURLOPT_PROXYUSERNAME, proxy_user);
      curl_easy_setopt(ch, CURLOPT_PROXYPASSWORD, proxy_pass);
  }

  //curl_easy_setopt(ch, CURLOPT_MAXCONNECTS, MAXCONN); //curl_easy_setopt(ch, CURLOPT_FRESH_CONNECT, 0);
  //curl_easy_setopt(ch, CURLOPT_FORBID_REUSE, 0); //curl_setopt($curl, CURLOPT_AUTOREFERER, 1);

  // send all data to this function
  curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, write_data);
  //curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

  // 20191107 m fantuzzi
  // we pass our 'chunk' struct to the callback function
  //curl_easy_setopt(ch, CURLOPT_WRITEDATA, http_response);
  //curl_easy_setopt(ch, CURLOPT_WRITEDATA, (void *)&chunk);
  //curl_easy_setopt(ch, CURLOPT_DEBUGFUNCTION, my_trace);

  // set curlopt --> FOLLOW-LOCATION, necessary if getting 301 "Moved Permanently"
  // reacting to "Location: http://www.example.org/index.asp"
  //curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 1);

  // it doesnt cost much to verify, disable only while testing
  curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 0L);;

  // OCSP validation, avoided in the test phase, as some cloud including Google GCP do not provide it
  // Cloudflare CDN does well with terminated frontend-SSL certificate
  curl_easy_setopt(ch, CURLOPT_SSL_VERIFYSTATUS, 0L);

  // SSL engine config
  //static const char *pCertFile = "testcert.pem"; //static const char *pCACertFile="fantuznet.pem"; //static const char *pHeaderFile = "dumpit";

  // Cloudflare is using COMODO CA thus we shall avoid gzip as it clashes with OCSP validation
  //curl_easy_setopt(ch, CURLOPT_ENCODING, "gzip, deflate, br, sdch");
  //curl_easy_setopt(ch, CURLOPT_ENCODING, "br");

  // This timeout is deemed to become a parameter
  curl_easy_setopt(ch, CURLOPT_TIMEOUT, 5);
  curl_easy_setopt(ch, CURLOPT_TCP_FASTOPEN, 1L);
  curl_easy_setopt(ch, CURLOPT_TCP_NODELAY, 0L);            // disable Nagle with 0, for bigger packets (full MSS)
  curl_easy_setopt(ch, CURLOPT_DNS_CACHE_TIMEOUT, 15);
  curl_easy_setopt(ch, CURLOPT_DNS_USE_GLOBAL_CACHE, 1);    // DNS CACHE WITHIN CURL, yes or not ?
  curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(ch, CURLOPT_BUFFERSIZE, 8192L);          // lowering from 100K to 8K

  if (DEBUGCURLTTL) { curl_easy_setopt(ch, CURLOPT_VERBOSE,  1); } else { curl_easy_setopt(ch, CURLOPT_VERBOSE,  0); }

  curl_easy_setopt(ch, CURLOPT_NOSIGNAL, 1L);

  */

  /* wait for pipe to confirm */
  /*
  #if (CURLPIPE_MULTIPLEX > 0)
  	curl_easy_setopt(ch, CURLOPT_PIPEWAIT, 1L);
  #endif
  */

  /* do proxies like pipelining ? polipo does, and how about squid, nginx, apache ... ?? */
  /* anyway all the story changes completely with H2 and DoH specs from RFC8484 */

  /* OPTIONAL HEADERS, set with curl_slist_append */
  //list = NULL;
  //list = curl_slist_append(list, "content-type: application/dns-message");
  //list = curl_slist_append(list, "accept: application/dns-message");
  
  /* Defining which one to use, between: gzip, deflate, br, sdch */
  //list = curl_slist_append(list, "accept-encoding: sdch, br, deflate");
  // WARNING !!
  //list = curl_slist_append(list, "accept-encoding: deflate");

  /* OVERRIDE RESOLVER --> add resolver CURL header, work in progress */
  // in the form of CLI --resolve my.site.com:80:1.2.3.4, -H "Host: my.site.com"
  //hosting = curl_slist_append(hosting, "www.fantuz.net:80:217.114.216.51");
  //curl_easy_setopt(ch, CURLOPT_RESOLVE, hosting);
  //list = curl_slist_append(list, ":host:www.fantuz.net");
  
  //curl_setopt($ch, CURLOPT_HTTPHEADER, ["Content-Type: application/x-www-form-urlencoded; charset=UTF-8"]);

  //list = curl_slist_append(list, "Request URL: http://www.fantuz.net/nslookup-doh.php?host=news.google.fr.&type=A");
  //list = curl_slist_append(list, "Request Method:GET");
  //list = curl_slist_append(list, "Remote Address: 217.114.216.51:443");
  //list = curl_slist_append(list, "Cache-Control:max-age=300");
  //list = curl_slist_append(list, "Connection:keep-alive");
  //list = curl_slist_append(list, "If-None-Match: *");

  // Upgrades, not always a good idea
  //list = curl_slist_append(list, "Connection: Upgrade, HTTP2-Settings");
  //list = curl_slist_append(list, "Upgrade: h2c");
  //list = curl_slist_append(list, "HTTP2-Settings: AAMAAABkAAQAAP__");
  
  /*
  // see if H2 goes no-ALPN with no headers set ...
  curl_easy_setopt(ch, CURLOPT_HEADER, 1L);
  curl_easy_setopt(ch, CURLOPT_HTTPHEADER, list);
  // try to see if it works .. not sure anymore
  //curl_easy_setopt(ch, CURLINFO_HEADER_OUT, 1L );
  // get us the resource without a body!
  curl_easy_setopt(ch, CURLOPT_NOBODY, 0L); 
  curl_easy_setopt(ch, CURLOPT_HEADERDATA, NULL);
  curl_easy_setopt(ch, CURLOPT_HEADERFUNCTION, header_handler);

  // CURL_LOCK_DATA_SHARE, quite advanced and criptic, useful in H2
  //  curlsh = curl_share_init();
  //  curl_easy_setopt(ch, CURLOPT_SHARE, curlsh);
  //  curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
  //  curl_share_setopt(curlsh, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
  //  curl_easy_setopt(curl2, CURLOPT_URL, "https://example.com/second");
  //  curl_easy_setopt(curl2, CURLOPT_COOKIEFILE, "");
  //  curl_easy_setopt(curl2, CURLOPT_SHARE, shobject);

  */

  // WARNING
  // CURL multi-handler spawner, deactivated for the time being. we start some action by calling perform right away
  //curl_multi_perform(multi_handle, &still_running);

  /* Section left for mutlipath or PUSH implementations */
  // as long as we have transfers going, do work ...
  
  /*
  do {
      struct timeval timeout;
  
      // select() return code
      int rc;
      // curl_multi_fdset() return code
      CURLMcode mc;
  
      fd_set fdread;
      fd_set fdwrite;
      fd_set fdexcep;
      int maxfd = -1;
   
      long curl_timeo = -1;
   
      FD_ZERO(&fdread);
      FD_ZERO(&fdwrite);
      FD_ZERO(&fdexcep);
   
      // set a suitable timeout to play around with
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
   
      curl_multi_timeout(multi_handle, &curl_timeo);
      if(curl_timeo >= 0) {
        timeout.tv_sec = curl_timeo / 1000;
        if(timeout.tv_sec > 1)
          timeout.tv_sec = 1;
        else
          timeout.tv_usec = (curl_timeo % 1000) * 1000;
      }
   
      // get file descriptors from the transfers
      mc = curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);
   
      if(mc != CURLM_OK) {
        fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
        break;
      }
   
      // On success the value of maxfd is guaranteed to be >= -1. We call
      // select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
      // no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
      // to sleep 100ms, which is the minimum suggested value in the
      // curl_multi_fdset() doc.
   
      if(maxfd == -1) {
          #ifdef _WIN32
              Sleep(100);
              rc = 0;
          #else
      	// Portable sleep for platforms other than Windows
      	struct timeval wait = { 0, 100 * 1000 };
      	// 100ms
      	rc = select(0, NULL, NULL, NULL, &wait);
          #endif
      } else {
        // Note that on some platforms 'timeout' may be modified by select().
        // If you need access to the original value save a copy beforehand.
        rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
      }
   
      switch(rc) {
      case -1:
        // select error
        break;
      case 0:
      default:
        // timeout or readable/writable sockets
        curl_multi_perform(multi_handle, &still_running);
        break;
      }
   
      // A little caution when doing server push is that libcurl itself has
      // created and added one or more easy handles but we need to clean them up
      // when we are done.
   
      do {
        int msgq = 0;;
        m = curl_multi_info_read(multi_handle, &msgq);
        if(m && (m->msg == CURLMSG_DONE)) {
          CURL *e = m->easy_handle;
          transfers--;
          curl_multi_remove_handle(multi_handle, e);
          curl_easy_cleanup(e);
        }
      } while(m);
   
  } while(transfers);
  */
 

  //curl_multi_cleanup(multi_handle);

  /* multiple transfers */
  //    for(i = 0; i<num_transfers; i++) {
  //      easy[i] = curl_easy_init();
  //      /* set options */ 
  //      setup(easy[i], i);
  //   
  //      /* add the individual transfer */ 
  //      curl_multi_add_handle(multi_handle, easy[i]);
  //    }

  /* get it! */
  //res = curl_easy_perform(ch);

  /* check for errors */ 
  //if(res != CURLE_OK) { fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res)); } else { printf("%lu bytes retrieved\n", (long)chunk.size); }

  /* Now chunk.memory points to memory block chunk.size bytes big and contains the remote file. Do something nice with it! */ 

  /* slist holds specific headers here, beware of H2 reccomendations mentioned above */
  list = NULL;
  //list = curl_slist_append(list, "authority = cloudflare-dns.com");

  // 20200307 - POST -> ‘content-type: application/dns-message’
  //  slist1 = curl_slist_append(slist1, "content-type = application/dns-message");
  //  slist1 = curl_slist_append(slist1, "content-length = XXXX");
  
  // 20200307 - GET  -> ‘accept: application/dns-message’
  //  slist1 = curl_slist_append(slist1, "accept = application/dns-message; charset = utf-8");

  list = curl_slist_append(list, "accept: application/dns-message");
  list = curl_slist_append(list, "accept-encoding: gzip, deflate, br");
  //slist1 = curl_slist_append(slist1, "accept-charset = utf-8");
  //slist1 = curl_slist_append(slist1, "accept-encoding = deflate");
  //slist1 = curl_slist_append(slist1, "accept: application/octet-stream");
  //slist1 = curl_slist_append(slist1, "upgrade-insecure-requests = 1");

  //curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 8192L); /* 8K test, normally 100K buffer, set accordingly to truncation bit and other considerations */ 
  curl_easy_setopt(hnd, CURLOPT_BUFFERSIZE, 102400L);
  //curl_easy_setopt(hnd, CURLOPT_USERAGENT, "dnsp-h2/2.0-DEV");
  curl_easy_setopt(hnd, CURLOPT_USERAGENT, "curl 7.64.1-DEV (x86_64-pc-linux-gnu) libcurl/7.64.1-DEV OpenSSL/1.0.2g zlib/1.2.11 nghttp2/1.37.0-DEV");

  curl_easy_setopt(hnd, CURLOPT_URL, script_url);
  curl_easy_setopt(hnd, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(hnd, CURLOPT_TCP_FASTOPEN, 0L);
  curl_easy_setopt(hnd, CURLOPT_NOBODY, 0L); /* placeholder for HEAD method */
  curl_easy_setopt(hnd, CURLOPT_HEADER, 1L); /* set whether or not fetching headers and including in response */
  curl_easy_setopt(hnd, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(hnd, CURLOPT_MAXREDIRS, 2L); /* delegation, RD bit set ? default 50, nice 2 */ 

  curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
  //curl_easy_setopt(hnd, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE);
  //curl_easy_setopt(hnd, CURLOPT_SSL_ENABLE_ALPN, 1L);
  //curl_easy_setopt(hnd, CURLOPT_SSL_ENABLE_NPN, 1L);
  curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYHOST, 0L);
  /* OCSP not always available on CloudFlare or other cloud providers (OK for Google's GCP, still need to test with AWS) */
  curl_easy_setopt(hnd, CURLOPT_SSL_VERIFYSTATUS, 0L);
  curl_easy_setopt(hnd, CURLOPT_FILETIME, 1L);
  curl_easy_setopt(hnd, CURLOPT_TCP_KEEPALIVE, 1L);
  //curl_easy_setopt(hnd, CURLOPT_ENCODING, "gzip, deflate, br, sdch");
  curl_easy_setopt(hnd, CURLOPT_ENCODING, "deflate, br, sdch");
  //curl_easy_setopt(hnd, CURLOPT_FOLLOWLOCATION, 1);

  if (DEBUGCURLTTL) { curl_easy_setopt(hnd, CURLOPT_VERBOSE,  1); } else { curl_easy_setopt(hnd, CURLOPT_VERBOSE,  0); }

  /* send all data to this function */
  curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, write_data); 
  //curl_easy_setopt(hnd, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  
  /* we pass our 'chunk' struct to the callback function */ 
  curl_easy_setopt(hnd, CURLOPT_WRITEDATA, http_response);
  //curl_easy_setopt(hnd, CURLOPT_WRITEDATA, (void *)&chunk);

  curl_easy_setopt(hnd, CURLOPT_DEBUGFUNCTION, my_trace);
  curl_easy_setopt(hnd, CURLOPT_DEBUGDATA, &config);

  //if ((proxy_user != NULL) && (proxy_pass != NULL))
  if ((proxy_host != NULL) && (proxy_port != NULL)) {
	curl_easy_setopt(hnd, CURLOPT_PROXY, proxy_host);
	curl_easy_setopt(hnd, CURLOPT_PROXYPORT, proxy_port);	
  	curl_easy_setopt(hnd, CURLOPT_PROXYTYPE, CURLPROXY_HTTP);
	printf(" *** proxy set !\n");
  }

  if (DEBUGCURL) {
      fprintf(stderr, " *** POST data  : N/A\n",n);
      fprintf(stderr, " *** GET string : %s\n",script_url);
      //fprintf(stderr, " *** DOH string : ?dns=%s\n",b64_encode(rfcstring,sizeof(rfcstring)+strlen(host)+9));
  }

  // init a multi stack
  //multi_handle = curl_multi_init();
  
  // add the easy transfer
  //curl_multi_add_handle(multi_handle, hndtest);
  //curl_multi_add_handle(multi_handle, hnd);

  //curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
  //curl_multi_setopt(multi_handle, CURLMOPT_PUSHFUNCTION, server_push_callback);
  //curl_multi_setopt(multi_handle, CURLMOPT_PUSHDATA, &transfers);
  //curl_multi_setopt(multi_handle, CURLOPT_CAPATH, "/etc/ssl/certs/");
  
  /* ret on (hnd) is the H2 cousin of original ret used above for (ch). This section has been commented out */
  //if (!(host == NULL) && !(host == "") && !(host == ".") && !(host == "(null)"))
  if (sizeof(host) > 3 ) {
    //printf(" *** VALID HOST	: '%s'\n", host);
    res = curl_easy_perform(hnd);
    //ret = curl_multi_perform(multi_handle,&still_running);
    if(res != CURLE_OK) { fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res)); }
    //if(ret != CURLE_OK) { fprintf(stderr, " *** CURL: curl_multi_perform() failed: %s\n", curl_multi_strerror(ret)); } //else { printf("\n *** MULTI -> OK !\n"); }
    // 20200824 - M FANTUZZI - commented out printing
//    printf(" *** http_response		: '%x'\n", http_response);
//    printf(" *** http_response		: '%s'\n", http_response);
//    printf(" *** ret			: '%d'\n", res);
//    printf(" *** OBTAINED VALUE		: '%s'\n", ret);
    
    //free(host);
    //return http_response;
    //return;
  } else {
    printf(" *** WRONG HOST		: '%s'\n", host);
    ret = -1;
  }
  
  /* Problem performing request */
  if (ret < 0) {
    fprintf(stderr, " *** Error performing HTTP request (Error %d) - spot on !!!\n", ret);
    free(script_url);
    curl_slist_free_all(list);

    //curl_slist_free_all(hosting);
    //curl_share_cleanup(curlsh);
    //printf("%s\n",http_response);

    free(n);
    http_response = "0.0.0.0";
    return http_response;
  }
 
  /* Can't resolve DoH query, or packet too big (up to 4096 in UDP and 65535 in TCP) */
  if ((strlen(http_response) > 4096) || (strncmp(http_response, "0.0.0.0", 7) == 0)) {
    // insert error answers here, as NXDOMAIN, SERVFAIL etc
    // In BIND 8 the SOA record (minimum parameter) was used to define the zone default TTL value.
    // In BIND 9 the SOA 'minimum' parameter is used as the negative (NXDOMAIN) caching time (defined in RFC2308).
      
    if (DEBUG) {
      printf(" *** DNS-over-HTTP server  -> %s\n", script_url);
      //printf(" *** Response from libCURL -> %s\n", http_response);
    }

    //curl_slist_free_all(hosting);
    curl_slist_free_all(list);
    //curl_share_cleanup(curlsh);
    //curl_easy_cleanup(ch);
    http_response = "0.0.0.0";
    return http_response;
  }
 
//  //20191212
//  if (DEBUGCURL) {
//    // max fantuzzi verfied 20200924 20191105
//    hexdump(http_response,sizeof(http_response));
//    //printf("\n[%03ld]\n",http_response);
//    //printf(" *** DOH server : %s\n", script_url);
//    //printf(" *** Response from libCURL -> %s\n", http_response);
//  }

  free(script_url);
  //free(proxy_url);
  free(chunk.memory);
  //curl_multi_cleanup(multi_handle);
  //curl_share_cleanup(curlsh);
  curl_slist_free_all(list);
  curl_easy_cleanup(hnd);
  curl_global_cleanup();
  //hnd = NULL;
  //free(hnd);
  //free(n);

  return http_response;
}

/* This is our thread function.  It is like main() but for a thread */
void *threadFunc(void *arg) {
  struct readThreadParams *params = (struct readThreadParams*)arg;
  struct sockaddr_in *yclient = (struct sockaddr_in *)params->yclient;
  struct dns_request *dns_req = malloc(sizeof(struct dns_request));
  struct dns_request *xhostname = (struct dns_request *)params->xhostname;
  size_t request_len = params->xrequestlen;
  
  int wport = params->xwport, ret;
  
  int proxy_port_t = params->xproxy_port;
  char* proxy_host_t = params->xproxy_host;
  char* proxy_user_t = params->xproxy_user;
  char* proxy_pass_t = params->xproxy_pass;
  
  int xsockfd = params->xsockfd;
  int sockfd = params->sockfd;
  long int ttl = params->xttl;
  int tcp_z_offset = params->xtcpoff;

  int proto = params->xproto;
  char* typeq = params->xtypeq;
  char* lookup_script = params->xlookup_script;
  char *rfcstring = (char *)params->xhostname->rfcstring;
  //char* rfcstring = params->xrfcstring;
  //char *rfcstring = (char *)params->xrfcstring;
  //struct dns_request *xrfcstring = (struct dns_request *)params->xhostname->rfcstring;
  //struct dns_request *xrfcstring = (struct dns_request *)params->xrfcstring;
  
  char *www = malloc(256 * sizeof(char)),
       *ip = NULL,
       *yhostname = (char *)params->xhostname->hostname;

  //char www[4096];
  //www == NULL;
  
  pthread_key_t key_i;
  pthread_key_create(&key_i, NULL);
  
  /* shall we use trylock or lock ? */
  //if (pthread_mutex_lock(&mutex))
  if (pthread_mutex_trylock(&mutex)) {
    if (LOCK_DEBUG) { printf(" *** Locking .............. OK\n"); }
  } else {
    if (LOCK_DEBUG) { printf(" *** Locking .......... NOT OK\n"); }
  }
  
  if (EXT_DEBUG) {
    char *s = inet_ntoa(yclient->sin_addr);
    //char *p = &xclient->sin_addr.s_addr;
    //printf("params->xhostname		: %s\n",(char *)params->xhostname);
    //printf("VARIABLE sin_addr		: %d\n", (uint32_t)(yclient->sin_addr).s_addr);
    printf(" *** params-xhostname->host	: %s\n",(char *)params->xhostname->hostname);
    printf(" *** proto			: %d\n",params->xproto);
    printf(" *** sin_addr readable		: %s\n", s);
  }

  if (!(params->xhostname->hostname == NULL) && !(yhostname == NULL)) {
    www = lookup_host(yhostname, proxy_host_t, proxy_port_t, proxy_user_t, proxy_pass_t, lookup_script, typeq, wport, rfcstring);
    yhostname == NULL;
    params->xhostname->hostname == NULL;
    // m fantuzzi review 20200924 20190221
    //pthread_exit(NULL);
    //exit(EXIT_SUCCESS);
  } else {
    //printf("\n *** HALT");
    // SECTION TO BE SUPPRESSED, WITH DOH THIS CASE IS NO MORE RELEVANT.
    www == NULL;
    //www == "0.0.0.0";
    yhostname == NULL;
    params->xhostname->hostname == NULL;
    //return;
    // m fantuzzi review 20200924 20190221
    //pthread_exit(NULL);
    exit(EXIT_SUCCESS);
  }
  
  /* PTHREAD SET SPECIFIC GLOBAL VARIABLE */
  pthread_setspecific(glob_var_key_ip, www);
  pthread_getspecific(glob_var_key_ip);

  //printf("building for: %s", inet_ntop(AF_INET, &ip_header->saddr, ipbuf, sizeof(ipbuf)));
  
  if (EXT_DEBUG) {
    printf("\n *** RET-HTTP-GLOBAL		: %x", glob_var_key_ip);
    printf("\n *** ANSWER MODE		: %d", DNS_MODE_ANSWER);
    printf("\n *** DOH retcode		: %d", ret);
    //printf("\n *** DOH r size		: %d", get_size());
    printf("\n *** base64url			: %s\n",b64_encode(params->xhostname->rfcstring,sizeof(params->xhostname)+request_len-31));
    //printf("\n *** base64url		: %s", b64_encode(params->xhostname->rfcstring,sizeof(params->xhostname)+request_len-19));

    //printf(" *** THREAD-xproxy-host			: %s\n", params->xproxy_host);
    //printf(" *** THREAD-xproxy-port			: %d\n", params->xproxy_port);
    //printf(" *** THREAD-proxy-host			: %s\n", proxy_host_t);
    //printf(" *** THREAD-proxy-port			: %d\n", proxy_port_t);
  }
  
  /* RIP vs WWW, different data presentation */
//  for (int d=0;d<=get_size()-1;d++) {
//    fprintf(stdout,"%02hhx\n",www[d]);
//    snprintf(ip, 1, "%02hhx\n", www[d]);
//    //www[d] = ip[d];
//  }

  //printf("WWW: %02hhx\n",www);
  //hexdump(www,sizeof(www));
  //hexdump(www,get_size());
  
  //printf("BUILD dns-req->hostname	            : %s\n", dns_req->hostname);
  //printf("BUILD yhostname			        : %s\n", yhostname);

  // pre-DoH
  //if ((rip != NULL) && (strncmp(rip, "0.0.0.0", 7) != 0))
  //if ((rip != NULL) && (www != NULL))

  // DoH
  if (www != NULL) {
    if (DEBUG) {
	printf("\n");
	printf(" *** THREAD qsize		: %u\n", (uint32_t)request_len);
	printf(" *** THREAD typeq		: %s\n", typeq);
	printf(" *** THREAD dns_req->qtype	: %d\n", dns_req->qtype);
	/*
	printf("THREAD V-socket-Xsockfd			: %u\n", xsockfd);
	printf("THREAD V-socket- sockfd			: %u\n", sockfd);
	printf("THREAD V-xclient->sin_addr.s_addr	: %u\n", (uint32_t)(yclient->sin_addr).s_addr);
	//printf("THREAD V-xclient->sin_port		: %u\n", (uint32_t)(yclient->sin_port));
	//printf("THREAD V-xclient->sin_family		: %u\n", (uint32_t)(yclient->sin_family));
	//printf("THREAD V-xclient->sin_addr.s_addr		: %s\n",(char *)(xclient->sin_family));
	*/
    }

    if (get_ttl()>0) {
        build_dns(sockfd, yclient, xhostname, www, DNS_MODE_ANSWER, request_len, get_ttl(), proto, tcp_z_offset);
    } else {
        build_dns(sockfd, yclient, xhostname, www, DNS_MODE_ANSWER, request_len, ttl, proto, tcp_z_offset);
    }
    // new 20190221
    //pthread_exit(NULL);

  } else if ( strstr(dns_req->hostname, "hamachi.cc" ) != NULL ) {
    // BLACKLIST TO BE RADICALLY CHANGED IN DOH. MAYBE RETIRED.
    printf("BALCKLIST: pid [%d] - name %s - host %s - size %d\r\n", getpid(), dns_req->hostname, www, (uint32_t)request_len);
    //printf("BLACKLIST: xsockfd %d - hostname %s \r\n", xsockfd, xdns_req->hostname);
    printf("BLACKLIST: xsockfd %d - hostname %s \r\n", xsockfd, yhostname);

    build_dns(sockfd, yclient, xhostname, www, DNS_MODE_ANSWER, request_len, ttl, proto, tcp_z_offset);

    close(sockfd);
    params->xhostname->hostname == NULL;
    // new 20190221
    pthread_exit(NULL);
    exit(EXIT_SUCCESS);
  
  // else if ((rip == "0.0.0.0") || (strncmp(rip, "0.0.0.0", 7) == 0)) 
  } else if (strncmp(www, "0.0.0.0", 7) == 0) {
    fprintf(stderr," *** ERROR: pid [%d] - dns_req->hostname %s - host (www) %s - size %d \r\n", getpid(), dns_req->hostname, www, (uint32_t)request_len);
    fprintf(stderr," *** ERROR: xsockfd %d - yhostname %s \r\n", xsockfd, yhostname);

    //build_dns_response(sockfd, yclient, xhostname, rip, DNS_MODE_ERROR, request_len, ttl, proto, tcp_z_offset);
    build_dns(sockfd, yclient, xhostname, www, DNS_MODE_ANSWER, request_len, ttl, proto, tcp_z_offset);
    
    close(sockfd);
    params->xhostname->hostname == NULL;
    // new 20190221
    //pthread_exit(NULL);
    exit(EXIT_SUCCESS);
  }
  
  //char *s = inet_ntoa(xclient->sin_addr);
  pthread_setspecific(glob_var_key_ip, NULL);
  
  if (pthread_mutex_unlock(&mutex)) {
    if (LOCK_DEBUG) { printf(" *** Mutex unlock ........  OK (thread ID: %d)\n", getpid()); }
  } else {
    if (LOCK_DEBUG) { printf(" *** Mutex unlock ..... NOT OK (thread ID: %d)\n", getpid()); }
  } 
  
  if (pthread_mutex_destroy(&mutex)) {
    if (LOCK_DEBUG) { printf(" *** Mutex destroy .........OK\n\n"); }
  } else {
    if (LOCK_DEBUG) { printf(" *** Mutex destroy .... NOT OK\n\n"); }
  }
  
  //pthread_exit(NULL);
  exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
  /* clear the master and temp sets */
  /* master file descriptor list */
  fd_set master;

  /* temp file descriptor list for select() */
  fd_set read_fds;

  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  //int sockfd, fd, port = DEFAULT_LOCAL_PORT, wport = DEFAULT_WEB_PORT, proxy_port = 0, c, r = 0, ttl_in = TTL_IN_DEFAULT, tcp_z_offset = TCP_Z_OFFSET;
  int sockfd, fd, port = DEFAULT_LOCAL_PORT, wport = DEFAULT_WEB_PORT, proxy_port = 0, c, r = 0, ttl_in, tcp_z_offset;
  char *stack;            /* Start of stack buffer */
  char *stackTop;         /* End of stack buffer */
  pid_t pid;
  struct utsname uts;
  struct sockaddr_in serv_addr;
  struct sockaddr_in serv_addr_tcp;
  struct hostent *local_address;
  //struct hostent *proxy_address;
  //char *bind_proxy = NULL;
  char *bind_address_tcp = NULL, *bind_address = NULL, *proxy_host = NULL, *proxy_user = NULL,
       *proxy_pass = NULL, *typeq = NULL, *lookup_script = NULL, *rfcstring = NULL, *httpsssl = NULL;

  opterr = 0;
     
  /* deactivating mutexes, placeholders */
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

  /* The "-s" option specifies a stack size for our threads. Unlimited is not a good idea */
  stack_size = -1;

  /*
  const char    * short_opt = "hf:";
  struct option   long_opt[] = {
     {"help",          no_argument,       NULL, 'h'},
     {"file",          required_argument, NULL, 'f'},
     {NULL,            0,                 NULL, 0  }
  };
  */
  
  /* Command line args */
  // while ((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1)
  while ((c = getopt (argc, argv, "T:s:p:l:r:H:t:w:u:k:Z:hQvNRCLXn")) != -1)
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
        fprintf(stderr," *** CURL-VERBOSE ....... ON\n");
      break;

      case 'L':
        LOCK_DEBUG = 1;
        fprintf(stderr," *** WITH LOCK DEBUG .... ON\n");
      break;
      
      case 'Q':
        DEBUGCURLTTL = 1;
        fprintf(stderr," *** CURL-THE-TTL ....... ON\n");
      break;

      case 'R':
        THR_DEBUG = 1;
        fprintf(stderr," *** WITH THREAD DEBUG .. ON\n");
      break;
      
      case 'N':
        CNT_DEBUG = 1;
        fprintf(stderr," *** WITH COUNTERS ...... ON\n");
      break;

      case 'v':
        DEBUG = 1;
        fprintf(stderr," *** WITH DEBUG ......... ON\n");
      break;

      case 'X':
        EXT_DEBUG = 1;
        fprintf(stderr," *** EXTENDED DEBUG ..... ON\n");
      break;

      case 'Z':
        tcp_z_offset = atoi(optarg);
        if (tcp_z_offset <= 0) {
          fprintf(stdout," *** Invalid TCP offset !\n");
          exit(EXIT_FAILURE);
        } else {
          fprintf(stdout," *** TCP offset set to	%d\n",tcp_z_offset);
        }
      break;

      case 'T':
        ttl_in = atoi(optarg);
        if ((ttl_in >= 2147483648) || (ttl_in <= 0) )  {
          fprintf(stdout," *** Invalid TTL set. Please choose a value between 1 and 2147483647 seconds ...\n");
          exit(EXIT_FAILURE);
        }
        fprintf(stdout," *** Response TTL set to dec %d / hex %x. 4 bytes field, 0-2147483647 sec (RFC2181) ***\n",ttl_in,ttl_in);
      break;

      case 'n':
        DNSDUMP = 1;
        fprintf(stderr," *** HEX DNSDUMP ........ ON\n");
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
            fprintf(stderr," *** Invalid URL\n");
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
      abort();
  }

  if (proxy_host != NULL) {
      fprintf(stderr, " *** Yay !! Cache enabled, proxy set.\n");
      fprintf(stderr, " *** Using proxy-host: %s\n",proxy_host);
      //proxy_host = proxy_address;
      //fprintf(stderr, "Bind proxy string: %s\n",proxy_address);
  } else {
      fprintf(stderr, " *** Running without acceleration as no proxy was entered.\n");
  }	

  if (bind_address == NULL) { bind_address = "127.0.0.1"; bind_address_tcp = "127.0.0.1"; }
  //if (lookup_script == NULL) { usage(); }

  /* Prevent child process from becoming zombie process */
  signal(SIGCLD, SIG_IGN);

  /* libCurl init */
  curl_global_init(CURL_GLOBAL_ALL);

  /* TEST: KERNEL SUPPORT FOR TCP REUSE OPTIONS */
  #ifdef SO_REUSEPORT
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    int reuseport = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, (const char*)&reuseport, sizeof(reuseport)) < 0) {
      if (errno == EINVAL || errno == ENOPROTOOPT) {
        printf("SO_REUSEPORT is not supported by your kernel\n");
      } else {
        printf("unknown error\n");
      }
    } else {
      printf("SO_REUSEPORT is supported\n");
    }
    close(sock);
  #else
    printf("SO_REUSEPORT is not supported by your include files\n");
  #endif
  
  #ifdef SO_REUSEADDR
    int sock2 = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    if (setsockopt(sock2, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
      if (errno == EINVAL || errno == ENOPROTOOPT) {
        printf("SO_REUSEADDR is not supported by your kernel\n");
      } else {
        printf("unknown error\n");
      }
    } else {
      printf("SO_REUSEADDR is supported\n");
    }
    close(sock2);
  #else
    printf("SO_REUSEADDR is not supported by your include files\n");
  #endif

  /*
  memset(&serv_addr, 0, sizeof(serv_addr)); 
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(10000); //arbitrary port number...
  serv_addr.sin_addr.s_addr = inet_addr(host_addr);
  // connect socket to the above address
  if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    printf("Error : Connect Failed. %s \n", strerror(errno));
    return 1;
  }  
  */

  struct timeval read_timeout_micro;
  struct timespec read_timeout_nano;

  /* timeval */
  read_timeout_micro.tv_sec = 0;
  read_timeout_micro.tv_usec = 300;
  
  /* timespec */
  read_timeout_nano.tv_sec = 0;
  read_timeout_nano.tv_nsec = 100000;

  /* socket() UDP */
  sockfd = socket(AF_INET, SOCK_DGRAM, 17);
  int reuse_a_udp = 0, reuse_p_udp = 0;
  if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR, (const char*)&reuse_a_udp,sizeof(reuse_a_udp))==-1) { printf("%s",strerror(errno)); }
  if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEPORT, (const char*)&reuse_p_udp,sizeof(reuse_p_udp))==-1) { printf("%s",strerror(errno)); }
  int socketid = 0;
  if (sockfd < 0) error("Error opening socket");
  if ((socketid = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) error("socket(2) failed");

  /* local address listener guessing UDP */
  bzero((char *) &serv_addr, sizeof(serv_addr));
  local_address = gethostbyname(bind_address);
  if (local_address == NULL) error("Error resolving local host");
  
  serv_addr.sin_family = AF_INET;
  memcpy (&serv_addr.sin_addr.s_addr, local_address->h_addr,sizeof (struct in_addr));
  serv_addr.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) error("Error opening socket (bind)");
  
  /* socket() TCP */
  fd = socket(AF_INET, SOCK_STREAM, 6);
  
  if (fd<0) { printf(" *** %s",strerror(errno)); }

  int reuse_a_tcp = 1, reuse_p_tcp = 1;
  if (setsockopt(fd,SOL_SOCKET,SO_REUSEADDR, (const char*)&reuse_a_tcp,sizeof(reuse_a_tcp))==-1) { printf("%s",strerror(errno)); }
  if (setsockopt(fd,SOL_SOCKET,SO_REUSEPORT, (const char*)&reuse_p_tcp,sizeof(reuse_p_tcp))==-1) { printf("%s",strerror(errno)); }

  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout_micro, sizeof read_timeout_micro);
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout_micro, sizeof read_timeout_micro);

  int socketidtcp = 0;
  if (fd < 0) error("Error opening socket");
  if ((socketidtcp = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) error("socket(2) failed");

  /* local address listener guessing TCP */
  bzero((char *) &serv_addr_tcp, sizeof(serv_addr_tcp));
  memset(&serv_addr_tcp, 0, sizeof(serv_addr_tcp)); 
  local_address = gethostbyname(bind_address_tcp);
  if (local_address == NULL) error("Error resolving local host");

  serv_addr_tcp.sin_family = AF_INET;
  serv_addr_tcp.sin_port = htons(port);
  serv_addr_tcp.sin_addr.s_addr = inet_addr(bind_address_tcp);
  //memcpy (&serv_addr.sin_addr.s_addr, local_address->h_addr,sizeof (struct in_addr));

  if (bind(fd, (struct sockaddr *) &serv_addr_tcp, sizeof(serv_addr_tcp)) ==-1) { printf("%s",strerror(errno)); }
  //bind(fd, (struct sockaddr *) &serv_addr_tcp, sizeof(serv_addr_tcp));
  if ((listen(fd, SOMAXCONN)==-1)) { printf("%s",strerror(errno)); }

  int cnt = 0, cntudp = 0, flag;
  //int cnt = 0, cntudp = 0, flag = -1;

  // 20200824
  int reuse_addr = 1, reuse_port = 1;
  //int reusead = 0, reusepo = 0;
  if (setsockopt(fd,SOL_SOCKET,SO_REUSEADDR, (const char*)&reuse_addr,sizeof(reuse_addr))==-1) { printf("%s",strerror(errno)); }
  if (setsockopt(fd,SOL_SOCKET,SO_REUSEPORT, (const char*)&reuse_port,sizeof(reuse_port))==-1) { printf("%s",strerror(errno)); }

  /* selfconnect test */
  //if (connect(fd, (struct sockaddr *) &serv_addr_tcp, sizeof(serv_addr_tcp)) < 0) error("caca");;
  
  /* semaphores section, if ever needed */
  /*
  if(sem_init(*sem_t,1,1) < 0) { perror("semaphore initilization"); exit(2); }
  if(sem_init(&mutex,1,1) < 0) { perror("semaphore initilization"); exit(2); }
  if ((mutex = sem_open("/tmp/semaphore", O_CREAT, 0644, 1)) == SEM_FAILED ) { perror("sem_open"); exit(2); }
  */

  if(pthread_mutex_init(&mutex, &MAttr)) { printf("Unable to initialize a mutex while using threads\n"); return -1; }

  int rc, t, status, nnn = 0;
  uint i = 0;
  unsigned int request_len, client_len, request_len_tcp, client_len_tcp, new_socket_len;
  
  struct dns_request *dns_req, *dns_req_tcp;
  struct sockaddr client, client_tcp;

  /* client */
  client_len = sizeof(client);
  client_len_tcp = sizeof(client_tcp);

  /* UDP listener */
  fcntl(sockfd, F_SETFL, O_NONBLOCK);
  //fcntl(sockfd, F_SETFL, FNDELAY);

  /* add the listener to the master set */
  FD_SET(sockfd, &master);

  /* TCP listener */
  fcntl(fd, F_SETFL, O_NONBLOCK);
  //fcntl(fd, F_SETFL, O_ASYNC);
  //fcntl(fd, F_SETFL, FNDELAY);

  /* add the listener to the master set */
  FD_SET(fd, &master);

  /* Run forever */
  //while (1)
  for (;;) {
    
    /* copy it */
    read_fds = master;
     
    char *ip = NULL;
    char request[UDP_DATAGRAM_SIZE + 1];
    char request_tcp[TCP_DATAGRAM_SIZE + 1];

    pthread_mutexattr_init(&MAttr);
    //pthread_mutexattr_settype(&MAttr, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutexattr_settype(&MAttr, PTHREAD_MUTEX_RECURSIVE);
    
    struct thread_info *tinfo;
    
    /* Initialize and set thread detached attribute */
    //pthread_id_np_t tid;
    //tid = pthread_getthreadid_np();
    //wait(NULL);
    
    pthread_t *pth = malloc( NUMT * sizeof(pthread_t) ); // this is our thread identifier
    //pthread_t *tid = malloc( NUMT * sizeof(pthread_t) );
    pthread_t thread[NUM_THREADS];
    //static pthread_t tidd;
    
    //struct thread_data data_array[NUM_THREADS];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    //pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    
    /* wrong ... DO NOT USE */
    //sem_wait(&mutex);

    pthread_mutex_trylock(&mutex);

    request_len = recvfrom(sockfd,request,UDP_DATAGRAM_SIZE,0,(struct sockaddr *)&client,&client_len);

    int recv_udp = select(sockfd, sockfd, NULL, NULL, &read_timeout_micro);
    if (!(recv_udp == -1)) printf(" *** UDP    sockfd -> select() contains %d bytes\n",recv_udp);

    //if (cnt == 0) {
      //flag = 1;
      //request_len_tcp = recvfrom(fd,request_tcp,TCP_DATAGRAM_SIZE,MSG_DONTWAIT,(struct sockaddr *)&client_tcp,&client_len_tcp);
      /*
      int selt = select(fd+1, fd, NULL, NULL, &read_timeout_micro);
      if (!(selt == -1)) { printf(" *** TCP        fd -> select() contains %d bytes\n",selt); }
      */
      //cnt++;
    //} else {
      //flag = 1;
      //setsockopt(newsockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);
      //newsockfd = accept(fd, (struct sockaddr *) &client_tcp, &client_len_tcp);
      //fcntl(newsockfd, F_SETFL, O_NONBLOCK);
      //fcntl(newsockfd, F_SETFL, O_ASYNC);
      //setsockopt(newsockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout_micro, sizeof read_timeout_micro);
      //close(fd);
      //newsockfd = accept(fd, (struct sockaddr *) &client, &client_len);
      //request_len_tcp = recvfrom(newsockfd,request_tcp,TCP_DATAGRAM_SIZE,MSG_WAITALL,(struct sockaddr *)&client,&client_len);
      
    int recv_tcp = select(fd, fd, NULL, NULL, &read_timeout_micro);
    int newsockfd = accept(fd, (struct sockaddr *)&client_tcp,&client_len_tcp);
    // 20200824
    fcntl(newsockfd, F_SETFL, O_NONBLOCK);
    setsockopt(newsockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout_micro, sizeof read_timeout_micro);
    //fcntl(newsockfd, F_SETFL, O_ASYNC);
    //fcntl(newsockfd, F_SETFL, FNDELAY);

    if (!(recv_tcp == -1)) fprintf(stderr, " *** TCP newsockfd -> select() contains %d bytes\n",recv_tcp);
    //FD_SET(newsockfd, &master); /* add to master set */

    //request_len_tcp = recvfrom(newsockfd,request_tcp,TCP_DATAGRAM_SIZE,MSG_WAITALL,(struct sockaddr *)&client_tcp,&client_len_tcp);
    request_len_tcp = recvfrom(newsockfd,request_tcp,TCP_DATAGRAM_SIZE,MSG_DONTWAIT,(struct sockaddr *)&client_tcp,&client_len_tcp);

    cnt++;

    //}

    //if ((accept(fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) < 0) { printf("\nE INIT ACCEPT\n"); close(fd); } else { printf("\nNOERR INIT ACCEPT\n"); }
    //if ((accept(fd, (struct sockaddr *) &client, sizeof(&client))) < 0) { printf("\nE ACCEPT\n"); //close(fd); } else { printf("\nNOERR ACCEPT\n"); }
    
    /* Allocate stack for child */
    stack = malloc(STACK_SIZE);
    if (stack == NULL) errExit("malloc");
    
    /* Assume stack grows downward */
    stackTop = stack + STACK_SIZE;
    
    /* Clone function */
    /* Create child that has its own UTS namespace; child commences execution in childFunc() */
    /*
    pid = clone(parse_dns_request, stackTop, CLONE_NEWUTS | SIGCHLD, argv[1]);
    //pid = clone(parse_dns_request, stackTop, CLONE_VM | SIGCHLD, argv[1]);
    if (pid == -1) errExit("clone");
    printf("clone() returned %ld\n", (long) pid);
    sleep(1);           
    */

    /* Give child time to change its hostname while CLONE process/thread */
    // pid = clone(fn, stack_aligned, CLONE_VM | SIGCHLD, arg);
    // pid = clone(childFunc, stackTop, CLONE_NEWUTS | SIGCHLD, argv[1]);
    // posix_spawn()
    
    /* PID/clone() placeholder for: cloning processes */
    // pid = clone(parse_dns_request, stack_aligned, CLONE_VM | SIGCHLD, request request_len);
    //if (pid == 0) 
    //if (clone(parse_dns_request, stack_aligned, CLONE_VM | SIGCHLD, request, request_len)) 
    
    /* Monolithic, should be parallelised in C or in libcurl */
    if (vfork() == 0) {
    
      /* housekeeping, semaphores' logic */
      //sem_wait(&mutex);
    
      /*
       * A corresponding DNS lookup is made ONCE (via HTTP 1 or 2 against nslookup.php or DoH resolver) 
       * Retry methods have not been specified in RFC8484 by DoH IETF working-group.
       * SUCH ANSWER MIGHT BE CACHED IN THE NETWORK (polipo, memcache, CDN, CloudFlare, Varnish, GCP, ...)
      */
    
      // xproto
      int ret, xsockfd, xwport = wport;
      int ttl;
      char* xlookup_script = lookup_script, xtypeq = typeq;

      char* xrfcstring = xrfcstring;
      //struct dns_request *xrfcstring;

      struct dns_request *xhostname;
      struct sockaddr_in *xclient, *yclient;
      struct readThreadParams *readParams = malloc(sizeof(*readParams));

      int xproxy_port = proxy_port;
      char* xproxy_user = proxy_user;
      char* xproxy_pass = proxy_pass;
      char* xproxy_host = proxy_host;

      //if (dns_req == NULL) { flag = 0; }
      //if (dns_req_tcp == NULL) { flag = 1; } 

      if (request_len == -1) {
        flag = 1;

        // 20200824 M FANTUZZI - doubts on using close here
        if ((request_len_tcp == -1)) {
          flag = 3;
          
	  //printf(" *** flag equals 3. closing fd\n");
	  close(fd);
          exit(EXIT_SUCCESS);
        }

	if (cnt == 0) {
          
          int conn1 = select(fd, fd, NULL, NULL, &read_timeout_micro); 
          readParams->sockfd = fd;
          if (!(conn1 == -1)) {
	    printf(" ***        fd -> select(), %d\n",(conn1));
	  }
    	  cnt++;

          /* selfconnect test */
	  /*
          if (connect(fd, (struct sockaddr *) &serv_addr_tcp, sizeof(serv_addr_tcp)) < 0) {
            error("caca1\n");
          } else {
            printf("connect1 : %d",cnt);
          }
	  */
	  
          if (CNT_DEBUG) { fprintf(stderr, " *** QUANTITY TCP/1		: %x - %d\n", request_tcp, request_len_tcp); }

        } else {

          //int conn2 = select(newsockfd, newsockfd, NULL, NULL, &read_timeout_micro); 
          //readParams->sockfd = newsockfd;
          int conn2 = select(fd, fd, NULL, NULL, &read_timeout_micro); 
          readParams->sockfd = newsockfd;
	  if (!(conn2 == -1)) {
	    printf(" *** newsockfd -> select(), %d\n",(conn2));
	  }
    	  cnt++;

          /* selfconnect test */
	  /*
          if (connect(newsockfd, (struct sockaddr *) &serv_addr_tcp, sizeof(serv_addr_tcp)) < 0) {
            error("caca2\n");
          } else {
            printf("connect2 : %d",cnt);
          }
	  */
    	}
	
    	if ((flag != 3)) {
          if (CNT_DEBUG) { fprintf(stderr, " *** QUANTITY TCP/2		: %x - %d\n", request_tcp, request_len_tcp); }
	  dns_req_tcp = parse_dns_request(request_tcp, request_len_tcp, 1, 0);
    	} else {
          fprintf(stderr," *** flag is NULL (3). closing fd\n");
          //printf(" *** flag is NULL (3). closing fd\n");
          //close(newsockfd);
          close(fd);
    	  exit(EXIT_SUCCESS);
    	}

        readParams->xproto = 1;
        readParams->xclient = (struct sockaddr_in *)&client;
        readParams->yclient = (struct sockaddr_in *)&client;
        readParams->xrequestlen = request_len_tcp;
        readParams->xhostname = (struct dns_request *)dns_req_tcp;
        //readParams->xdns_req = (struct dns_request *)&dns_req_tcp;

    	// new 20200824
	//cnt++;
        
        // new 20190221
        close(fd);

      } else if (request_len_tcp == -1) {
    	
        /*
    	if ((flag != 3)) {
	      continue;
    	} else {
          printf(" *** flag equals 3 in UDP. closing fd\n");
          close(sockfd);
    	  exit(EXIT_SUCCESS);
    	}
        */

        flag = 0;
        if (CNT_DEBUG) { fprintf(stderr, " *** QUANTITY UDP		: %x - %d\n", request, request_len); }
        dns_req = parse_dns_request(request, request_len, 0, 0);

        readParams->sockfd = sockfd;
        readParams->xproto = 0;
        readParams->xclient = (struct sockaddr_in *)&client;
        readParams->yclient = (struct sockaddr_in *)&client;
        readParams->xrequestlen = request_len;
        readParams->xhostname = (struct dns_request *)dns_req;
    	cntudp++;

      } else {
    	
        flag = 3;
        fprintf(stderr," *** flag is NULL (3). closing fd and newsockfd\n");
        readParams->xhostname = NULL;
        dns_req = parse_dns_request(request, request_len, 0, 1);
        close(newsockfd);
        close(fd);
        //pthread_mutex_destroy(&mutex);
        
        // M FANTUZZI 20200824 introduced 20190221
        //pthread_join(pth[i],NULL);
        exit(EXIT_SUCCESS);
      }

      //flag = 1;
      //fprintf(stderr," *** OUT ***\n");

      /*
      if ((dns_req == NULL) || (dns_req_tcp == NULL)) {
        //printf("BL: pid [%d] - name %s - host %s - size %d \r\n", getpid(), dns_req->hostname, ip, request_len);
        printf("\nINFO-FAIL: transaction: %x - name %s - size %d \r\n", dns_req->transaction_id, dns_req->hostname, request_len);
        printf("\nINFO-FAIL: transaction: %x - name %s - size %d \r\n", dns_req_tcp->transaction_id, dns_req_tcp->hostname, request_len_tcp);
        exit(EXIT_FAILURE);
      }
      */

      /* QUERY DETECTION, for example see https://en.wikipedia.org/wiki/List_of_DNS_record_types */
      // AAAA is 0x1c, dec 28.  //6 SOA
    
      //if (dns_req_tcp->proto == 1) { dns_req->qtype == 0x01; }
      //if ((request_len == -1) && (request_len_tcp == -1)) { continue; }
      
      if (flag == 1) {
        if (dns_req_tcp->qtype == 0x02) {
          typeq = "NS";
        } else if (dns_req_tcp->qtype == 0x0c) {
          typeq = "PTR";
        } else if (dns_req_tcp->qtype == 0x05) {
          typeq = "CNAME";
        } else if (dns_req_tcp->qtype == 0x01) {
          typeq = "A";
        } else if (dns_req_tcp->qtype == 0x0f) {
          typeq = "MX";
        }
        // else { { dns_req->qtype == 0xff;} }
    	
	if (EXT_DEBUG) {
          printf(" *** TCP query type		: %x // %d\r\n",dns_req_tcp->qtype,dns_req_tcp->qtype);
          printf(" *** TCP trans ID		: %x // %d\r\n",dns_req_tcp->transaction_id,dns_req_tcp->transaction_id);
    	}

        if (tcp_z_offset) {
          //fprintf(stderr," *** TCP_Z_OFFSET set to %d\n",tcp_z_offset);
	  continue;
        } else {
          tcp_z_offset = TCP_Z_OFFSET;
          //fprintf(stderr," *** TCP_Z_OFFSET not set, using 2 as default for A type.");
        }

      } else if (flag == 0) {

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
        }
        // else { { dns_req->qtype == 0xff;} }
    	if (EXT_DEBUG) {
          printf(" *** UDP query type		: %x // %d\r\n",dns_req->qtype,dns_req->qtype);
          printf(" *** UDP trans ID		: %x // %d\r\n",dns_req->transaction_id,dns_req->transaction_id);
	}

      } else if ( flag > 1) {
      // else if ( flag == 3)

        pthread_join(pth[i],NULL);
    	printf("flag is NULL or equal 3\n");
    	flag = NULL;
    	//break;
    	return;
        //continue;
      }

      /* placeholder for HTTP options */
      //	  readParams->max_req_client = 10;
      //	  readParams->random = 0;
      //	  readParams->ssl = 0;
      //	  readParams->uselogin = 1;
    
      if (ttl_in == NULL) {
        //fprintf(stdout," *** TTL not set, forcing default value\n");
    	ttl = TTL_IN_DEFAULT;
      } else {
        //fprintf(stdout," *** TTL override, set to %d\n",ttl_in);
        ttl = ttl_in;
      }
    
      readParams->xlookup_script = lookup_script;
      readParams->xtypeq = typeq;
      readParams->xwport = wport;
      //readParams->xttl = get_ttl();
      readParams->xttl = ttl;
      readParams->xtcpoff = tcp_z_offset;

      readParams->xrfcstring = (struct dns_request *)dns_req;
      //readParams->xrfcstring = (struct dns_request *)rfcstring;
      //readParams->xrfcstring = dns_req->rfcstring;

      readParams->xproxy_user = proxy_user;
      readParams->xproxy_pass = proxy_pass;
      readParams->xproxy_host = proxy_host;
      readParams->xproxy_port = proxy_port;

      /*
      readParams->xhostname = (struct dns_request *)dns_req;
      readParams->xdns_req = (struct dns_request *)&dns_req;
      readParams->xsockfd = xsockfd;
      readParams->sockfd = sockfd;
      readParams->xproto = proto;
      readParams->xrequestlen = request_len;
      readParams->xclient = (struct sockaddr_in *)&client;
      readParams->yclient = (struct sockaddr_in *)&client;
      */
      
      //if ((!(readParams->xhostname->hostname == NULL))) && (flag == 1))) 
      //if ((!(readParams->xhostname == NULL)) || (!(readParams->xhostname->hostname == NULL))) 

      if ((dns_req->hostname == NULL) && (dns_req_tcp->hostname == NULL)) {
    	printf(" *** BINGO ! We just met the infamous no-host condition. \n ### flag: %d, TCP: %d, UDP: %d\n",flag,cnt,cntudp);
    	flag = NULL;
    	//return;
    	break;
      }

//      if (DNSDUMP) {
//        printf(" *** hostname			: %s\n", readParams->xhostname->hostname);
//        //printf("readParams->xrfcstring c	      : %c\n", readParams->xrfcstring);
//        //printf("readParams->xhostname->rfcstring c  : %c\n", readParams->xhostname->rfcstring);
//      }

      //free(out_array);
    
      /* LEFT FOR HOUSEKEEPING: thread retuns if needed */
      tinfo = calloc(NUMT, sizeof(struct thread_info));
      if (tinfo == NULL) handle_error("calloc");

      /* As stated, I have bene sticking to monolithic/vfork */
      //int errore = pthread_create(&tid[i], NULL, threadFunc, &data_array[i]);
      //if (i=sizeof(pth)) { i = 0 ;}
    
      if (CNT_DEBUG) {
	printf(" ### flag: %d, TCP: %d, UDP: %d ###\n",flag,cnt,cntudp);
      }

      if (pthread_mutex_trylock(&mutex)) {
        /* Spin the well-instructed thread ! */
        threadFunc(readParams);
        ret = pthread_create(&pth[i],&attr,threadFunc,readParams);
        //ret = pthread_create(&pth[i],NULL,threadFunc,readParams);
        if (THR_DEBUG) { printf("*** thread spin ... OK\n"); }
      } else {
        if (THR_DEBUG) { printf("*** thread spin ... KO\n"); }
        //return;
        break;
      }
          
      /* ONLY IF USING SEMAPHORES .... NOT WITH MUTEX */
      //sem_wait(&mutex);
      //sem_post(&mutex);
    
      for(r=0; r < NUMT*NUM_THREADS; r++) {
      	if(0 != ret) {
      	  fprintf(stderr, " ### Couldn't run thread number %d, errno %d\n", i, ret);
          //char *vvv = pthread_getspecific(glob_var_key_ip);
          //printf("GLOBAL-FAIL-IP: %s\n", vvv);
        } else {
          char *vvv = pthread_getspecific(glob_var_key_ip);
          printf("GLOBAL-SUCC-IP: %s\n", vvv);
      	  fprintf(stderr, " ### OK %d\n", i);
        }
    
	// 20200220 - MAX FANTUZZI - WARNING !
        //pthread_join(pth[i],NULL); /* joining is the trick */
        pthread_join(pth[r],NULL); /* joining is the trick */

        //tidd = pthread_self();
        //fprintf(stderr, "self r - %d \n",pthread_self(pth[i]));
    
        if (THR_DEBUG) {
          //fprintf(stderr, "pth i - %d \n",(uint16_t)pth[i]);
          //fprintf(stderr, "pth r - %d \n",(uint16_t)pth[r]);
          //printf("OUTSIDE-THREAD-resolved-address: %s\n",ip);
          //printf("OUTSIDE-THREAD-resolved-address: %d\n",ret);
          //printf("OUTSIDE-THREAD-resolved-address: %d\n",glob_var_key_ip);
          //printf("OUTSIDE-THREAD-log: pid [%u] - hostname %s - size %d ip %s\r\n", ret, dns_req->hostname, request_len, ip);
          fprintf(stderr, "---> OUTSIDE-THREAD-log: size %d\n",request_len);
          fprintf(stderr, "---> Finished joining thread i-> %d, nnn-> %d, r-> %d \n",i,nnn,r);
        }

        i++;
        nnn++;
      }
    
      //if (nnn >= NUMT*NUM_THREADS) { wait(NULL); }
      printf(" *** Thread/process ID : %d\n", getpid());
      pthread_mutex_destroy(&mutex);
      //if (i != 0) { i=0;}
      //if (!(flag == 3))
      
      // 20200220 MAX FANTUZZI - WARNING !
      pthread_join(pth[i],NULL);
     
      /* trying to re-enable this logic, continue() shouldn't be prepended to pthread_setspecific() */
      /* testing destroy after join, and before setspecific, seems right */
      pthread_attr_destroy(&attr);
      pthread_setspecific(glob_var_key_ip, NULL);

      if (nnn > 20) {
          wait(NULL);
      } else {
          continue;
      }

      continue;
    
    } else {
    
      /* sometimes you just need to take a break, or continue .. */
      nnn++;

      // RECOVER FROM THREAD BOMB SITUATION
      //if (DEBUG) { printf(" *** BIG FAULT with thread/process ID : %d\n", getpid()); }
      if (nnn > NUM_THREADS) {
        wait(NULL);
      }
      wait(NULL);
    
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
      //pthread_mutex_lock(&mutex);
      /*
      if (pthread_mutex_unlock(&mutex)) {
        //printf("mutex unlock OK\n");
	    continue;
      } else {
        printf("mutex unlock NOT OK\n");
      }
      */
    
      /* Semaphores section */
      //sem_destroy(&mutex);
    
      /* JOIN THREADS, rejoin and terminate threaded section */
      //if(pthread_join(pth[i], NULL)) {
        //fprintf(stderr, "Finished serving client %s on socket %u \n",(struct sockaddr_in *)&client->sin_addr.s_addr,sockfd);
      //}
    
      /* LOCKS AND MUTEXES */
      pthread_mutex_destroy(&mutex);
      // DO NOT USE
      //sem_post(&mutex); // sem_post is fun and dangerous
      
      /* THREAD JOIN ENDING, RELEASE */
      //pthread_join(pth[i],NULL);

      /* not in main() */
      //pthread_exit(NULL);
      if (THR_DEBUG) { fprintf(stderr, "---> Finished joining thread i-> %d, nnn-> %d \n",i,nnn); }
      continue;
      //exit(EXIT_FAILURE); // did we ?
    }
  }
}


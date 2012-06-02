// http://www.cs.utah.edu/~swalton/listings/sockets/programs/part2/chap7/echo-thread.c
// with modifications by Dan Crosta, 2012.

/* echo-thread.c
 *
 * Copyright (c) 2000 Sean Walton and Macmillan Publishers.  Use may be in
 * whole or in part in accordance to the General Public License (GPL).
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
*/

/*****************************************************************************/
/*** echo-thread.c                                                         ***/
/***                                                                       ***/
/*** An echo server using threads.                                         ***/
/*****************************************************************************/
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <pthread.h>

void PANIC(char* msg);
#define PANIC(msg)  { perror(msg); exit(-1); }

#define MAXLEN 1024

struct options {
    long delay;

    // "private" attributes
    unsigned long id;
    int socket;
};

pthread_mutex_t lock;
int nconns = 0;
unsigned long total = 0;
int debug = 0;

unsigned long nconns_inc() {
    if (pthread_mutex_lock(&lock) != 0)
        PANIC("pthread mutex lock");

    nconns += 1;
    total += 1;

    pthread_mutex_unlock(&lock);

    return total;
}

void nconns_dec() {
    if (pthread_mutex_lock(&lock) != 0)
        PANIC("pthread mutex lock");

    nconns -= 1;

    pthread_mutex_unlock(&lock);
}

/*--------------------------------------------------------------------*/
/*--- Child - echo servlet                                         ---*/
/*--------------------------------------------------------------------*/
void* Child(void* arg)
{
    char line[MAXLEN];
    int bytes;
    int socket;
    uint32_t len = 0;
    void * lenptr;
    long delay;
    unsigned long self;
    int millis = 0;

    struct options * opts = (struct options*)arg;
    delay = opts->delay;
    socket = opts->socket;
    self = opts->id;
    free(arg);
    arg = opts = NULL;

    while (1) {
        // read the length prefix
        bytes = recv(socket, &len, 4, MSG_WAITALL);
        len = ntohl(len);
        if (len >= MAXLEN) {
            // need a better way to signal this condition,
            // or ideally not to have it possible at all.
            if (debug) {
                printf("conn[%lu]: message length %d too long, killing connection\n", self, len);
            }
            break;
        }

        // read the actual message
        bytes = recv(socket, &line[bytes], len, MSG_WAITALL);
        if (bytes <= 0) {
            break;
        }
        line[bytes] = 0;

        if (delay >= 0) {
            if (debug) {
                printf("conn[%lu]: sleeping %ld usec\n", self, delay);
            }
            usleep(delay * 1000);
            millis = delay;
        }

        bytes = snprintf(line, MAXLEN, "{\"millis\":%d}", millis);
        if (debug) {
            printf("conn[%lu]: put '%s'\n", self, line);
        }

        len = htonl(bytes);
        lenptr = &len;
        send(socket, lenptr, 4, 0);
        send(socket, line, bytes, 0);
    }

    nconns_dec();
    printf("conn[%lu]: disconnecting (%d active)\n", self, nconns);
    close(socket);

    return NULL;
}

/*--------------------------------------------------------------------*/
/*--- listenloop - setup server and await connections (no need to  ---*/
/*--- clean up after terminated children.                          ---*/
/*--------------------------------------------------------------------*/
int listenloop(int port, struct options opts)
{
    int sd;
    struct sockaddr_in addr;

    if (pthread_mutex_init(&lock, NULL) != 0)
        PANIC("pthread mutex init");

    if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        PANIC("Socket");

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
        PANIC("Bind");

    if (listen(sd, 20) != 0)
        PANIC("Listen");

    printf("listening on %s:%d ...\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    while (1) {
        int socket;
        socklen_t addr_size;
        pthread_t child;
        struct options * threadopts;

        socket = accept(sd, (struct sockaddr*)&addr, &addr_size);

        /* the child thread is responsible for freeing this memory */
        threadopts = malloc(sizeof(threadopts));
        threadopts->delay = opts.delay;
        threadopts->socket = socket;
        threadopts->id = nconns_inc();

        printf("conn[%lu]: from %s:%d (%d active)\n", threadopts->id, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), nconns);

        if (pthread_create(&child, NULL, Child, threadopts) != 0)
            perror("Thread creation");
        else
            pthread_detach(child);  /* disassociate from parent */
    }

    return 0;
}

int usage(int returncode, char* errmsg)
{
    printf("usage: echoserver [OPTIONS]\n");
    printf("\n");
    printf("OPTIONS:\n");
    printf("  -s DELAY        milliseconds to sleep before responding (default: none)\n");
    printf("  -p PORT         TCP port to listen for connections on (default: 9999)\n");

    if (errmsg == NULL && returncode == 0) {
        /* we are showing help purely, not responding to a
         * failed invocation, so issue the warning about -d */
        printf("\n");
        printf("Note: there is a difference between a DELAY of 0 milliseconds and not\n");
        printf("having a delay at all. With a delay of 0, a call to usleep() is made,\n");
        printf("wheras with no delay set, no call is made. Thus a DELAY of 0 milliseconds\n");
        printf("may take longer than no DELAY at all.\n");
    }


    if (errmsg != NULL) {
        printf("\n");
        printf("error: %s\n", errmsg);
    }
    return returncode;
}

int main(int argc, char* argv[])
{
    struct options opts;
    long longtmp;
    char* endptr;
    char buf[100];
    int c = 0;

    // don't sleep at all unles asked to
    opts.delay = -1;
    int port = 9999;

    while ((c = getopt(argc, argv, "hs:p:d")) != -1) {
        switch (c) {
        case 's':
            longtmp = strtol(optarg, &endptr, 10);
            if (endptr != optarg + strlen(optarg))
                return usage(1, "argument to -s must be an integer");
            opts.delay = longtmp;
            break;

        case 'p':
            longtmp = strtol(optarg, &endptr, 10);
            if (endptr != optarg + strlen(optarg))
                return usage(1, "argument to -p must be an integer");
            if (longtmp < 1 || longtmp > 65535)
                return usage(1, "invalid port (must be in range [1, 65535])");
            port = longtmp;
            break;

        case 'd':
            debug = 1;
            break;

        case 'h':
            return usage(0, NULL);

        default:
            sprintf(buf, "unknown option '-%c'", optopt);
            return usage(1, buf);
        }
    }

    return listenloop(port, opts);
}


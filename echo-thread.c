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

#define MAXLEN 100

struct options {
    long delay;
    int socket;
};

char* repr(char* str)
{
    int control_count = 0;
    int i, j;
    int len = strlen(str);
    char* out;
    char hex[3];

    for (i=0; i<len; ++i)
    {
        if (str[i] < 32)
            control_count += 1;
    }

    out = malloc(sizeof(char) * len + (3 * control_count) + 1);
    out[len + (3 * control_count)] = 0;

    for (i=0, j=0; i<len; ++i)
    {
        if (str[i] < 32 || str[i] == 127) {
            /* put in \xNN in place of the unprintable char */
            out[j++] = '\\';
            if (str[i] == 10) {
                out[j++] = 'n';
            } else if (str[i] == 13) {
                out[j++] = 'r';
            } else {
                snprintf(hex, 3, "%02x", (char)str[i]);
                out[j++] = 'x';
                out[j++] = hex[0];
                out[j++] = hex[1];
            }
        } else {
            out[j++] = str[i];
        }
    }

    return out;
}

/*--------------------------------------------------------------------*/
/*--- Child - echo servlet                                         ---*/
/*--------------------------------------------------------------------*/
void* Child(void* arg)
{
    char line[MAXLEN];
    char * linerepr;
    int bytes_read;
    int socket;
    long delay;
    pthread_t _self = pthread_self();
    unsigned long self = (unsigned long)&_self;

    struct options * opts = (struct options*)arg;
    delay = opts->delay * 1000;
    socket = opts->socket;
    free(arg);
    arg = opts = NULL;

    printf("%lx: hello\n", self);

    do {
        bzero(line, MAXLEN);
        bytes_read = recv(socket, line, sizeof(line), 0);
        if (delay >= 0) {
            printf("%lx: sleeping %ld usec\n", self, delay);
            usleep(delay);
        }
        send(socket, line, bytes_read, 0);

        linerepr = repr(line);
        printf("%lx: '%s'\n", self, linerepr);
        free(linerepr);
    }
    while (strncmp(line, "bye\r", 4) != 0);

    printf("%lx: bye\n", self);
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

    if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        PANIC("Socket");

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
        PANIC("Bind");

    if (listen(sd, 20) != 0)
        PANIC("Listen");

    while (1) {
        int socket;
        socklen_t addr_size;
        pthread_t child;
        struct options * threadopts;

        socket = accept(sd, (struct sockaddr*)&addr, &addr_size);
        printf("Connected: %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

        /* the child thread is responsible for freeing this memory */
        threadopts = malloc(sizeof(threadopts));
        threadopts->delay = opts.delay;
        threadopts->socket = socket;

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

    while ((c = getopt(argc, argv, "hs:p:")) != -1) {
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

        case 'h':
            return usage(0, NULL);

        default:
            sprintf(buf, "unknown option '-%c'", optopt);
            return usage(1, buf);
        }
    }

    return listenloop(port, opts);
}


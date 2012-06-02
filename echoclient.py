from contextlib import closing
import signal
import socket
import struct
import sys
import time
import threading
import random

def say(sock, message):
    s = time.time()
    try:
        sock.sendall(struct.pack('!I', len(message)))
        sock.sendall(message)
        resplen = struct.unpack('!I', sock.recv(4))[0]
        response = sock.recv(resplen)
        e = time.time()
        return (e - s) * 1000, response
    except:
        return None, None

class Sayer(threading.Thread):
    def __init__(self, myid, sock):
        super(Sayer, self).__init__()
        self.myid = myid
        self.sock = sock
        self.stop = False

    def end(self):
        self.stop = True

    def run(self):
        i = 0
        with closing(sock):
            while not self.stop:
                delay, response = say(self.sock, 'hello world %d' % i)
                if delay is None:
                    break
                sys.stdout.write('thread[%03d]: took %d ms, got %s\n' % (self.myid, delay, response))
                i += 1

if __name__ == '__main__':
    host = '127.0.0.1'
    port = 9999
    try:
        nthreads = int(sys.argv[1])
    except:
        nthreads = 10
    threads = []

    def handle_sigint(signum, frame):
        for t in threads:
            t.end()
        while threading.active_count() > 1:
            print 'waiting for threads to die...'
            time.sleep(1)
    signal.signal(signal.SIGINT, handle_sigint)

    for i in range(nthreads):
        sock = socket.socket(socket.AF_INET)
        sock.connect((host, port))
        t = Sayer(i, sock)
        t.daemon = True
        t.start()
        threads.append(t)

    time.sleep(60)

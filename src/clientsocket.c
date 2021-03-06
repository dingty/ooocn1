/*
 *
@brief
  ClientSocket object attribute and interface implementation module.

@author Hongchao Deng (Timber) <hongchad@andrew.cmu.edu>

@bugs No known bugs
 *
 */
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <openssl/ssl.h>

#include <clientsocket.h>
#include <logger.h>


ClientSocket *new_ClientSocket(int fd) {
    ClientSocket *clisock = malloc(sizeof(ClientSocket) );

    if ( ! clisock)
        return NULL;

    clisock->fd = fd;
    clisock->readIndex = 0;
    clisock->closed = 0;

    init_request(& clisock->request);
    init_response( & clisock->response);

    clisock->cgi_preprocessed = 0;
    clisock->isHTTPS = 0;

    return clisock;
}
void DeleteClientSocket(ClientSocket * clisock) {
    if(clisock->isHTTPS){
        SSL_shutdown(clisock->ssl);
        SSL_free(clisock->ssl);
    }
    delete_request(& clisock->request);
    delete_response(& clisock->response);
    free(clisock);
}

int ableToRead(ClientSocket *clisock) {
    return ( clisock->request.state != REQ_DONE
             && clisock->readIndex < CLISOCK_BUFSIZE );
}
int ableToWrite(ClientSocket *clisock) {
    return clisock->request.state == REQ_DONE || clisock->response.isPipelining;
}

/*
@brief
  This functions provides the interface for how to handle read bytes on
  specific client socket.
 */
void handleread(ClientSocket *clisock) {
    int n;
    int ctLength;
    int ctIndex;
    int readsize;
    int ctSize;
    int bufFreeSize;

    if(clisock->readIndex == CLISOCK_BUFSIZE)
        return;

    switch (clisock->request.state) {
    case REQ_CONTENT:
        if(!clisock->request.content) // not initialized
            return;
        ctLength = clisock->request.ctLength;
        ctIndex = clisock->request.ctIndex;
        if( ctLength < ctIndex){
            clisock->closed = 2;
            logger(LOG_ERROR, "Content-Length is wrong!");
            return;
        }

        ctSize = ctLength - ctIndex;
        bufFreeSize = CLISOCK_BUFSIZE - clisock->readIndex;
        readsize = (ctSize <= bufFreeSize)? ctSize: bufFreeSize ;

        if(readsize == 0)
            return;

        if(! clisock->isHTTPS){
            n = recv(clisock->fd,
                     & clisock->readbuf[clisock->readIndex],
                     readsize,
                     0 );
        }
        else{
            n = SSL_read(clisock->ssl,
                     & clisock->readbuf[clisock->readIndex],
                     readsize);
        }
        break;
    case REQ_LINE:
    case REQ_HEADER:
        // get one byte at a time for easier parsing
        if(! clisock->isHTTPS){
            n = recv(clisock->fd,
                     & clisock->readbuf[clisock->readIndex],
                     1,
                     0 );
        }
        else{
            n = SSL_read(clisock->ssl,
                     & clisock->readbuf[clisock->readIndex],
                     1);
        }
        break;
    default:
        return;
    }

    if(n < 0) {
        if (errno == EINTR) {
            logger(LOG_WARN, "recv() EINTR. Try again later.");
            return;
        }
        logger(LOG_ERROR, "recv() Error: %s", strerror(errno));
        clisock->closed = 2; //close socket
    }
    else if(n == 0) {
        logger(LOG_DEBUG, "Client connection closed (fd: %d)", clisock->fd);
        clisock->closed = 2; //close socket
    }
    else {
        logger(LOG_DEBUG, "Read %d bytes from client(fd: %d)", n, clisock->fd);
        clisock->readIndex += n;
    }

}

/*
@brief
  This functions provides the interface for how to handle send bytes on
  specific client socket.
 */
void handlewrite(ClientSocket *clisock) {
    int n;

    if(clisock->writeIndex == 0) return;

    if( ! clisock->isHTTPS){
        n = send(clisock->fd
                 , clisock->writebuf
                 , clisock->writeIndex
                 , 0
                );
    }
    else{
        n = SSL_write(clisock->ssl
                 , clisock->writebuf
                 , clisock->writeIndex
                );
    }

    if(n < 0) {
        if (errno == EINTR) {
            logger(LOG_WARN, "send() EINTR. Try again later.");
            return;
        }
        logger(LOG_ERROR, "send() Error: %s", strerror(errno));
        clisock->closed = 2; //close socket
    }
    else if(n != clisock->writeIndex) {
        logger(LOG_WARN, "Can't send whole buffer to client (fd: %d)", clisock->fd);
        clisock->closed = 2; //close socket
    }
    else {
        logger(LOG_INFO, "Send %d bytes to client(fd: %d)", n, clisock->fd);
        clisock->writeIndex = 0;
    }

    if(clisock->response.state == -1){
        clisock->closed = 1;
    }

}


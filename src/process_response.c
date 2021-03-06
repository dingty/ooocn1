/*
 *
@brief
  interface process_response() implementation and its helper functions.

@author Hongchao Deng (Timber) <hongchad@andrew.cmu.edu>

@bugs No known bugs
 *
 */
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <malloc.h>

#include <httprequest.h>
#include <httpresponse.h>
#include <clientsocket.h>
#include <logger.h>
#include <staticfile.h>
#include <netservice.h>
#include "http_internal.h"

static int addBuffer(char *, int *, int *, char *);
static int readFileContent(char *, int *, FILE *, int);
static void preprocess(HttpRequest *, HttpResponse *);
static int checkHeader(Linlist *headers, char *key);

/*
@brief
  This function provides the state machine of creating response message.
 */
void process_response(HttpRequest *request, HttpResponse * response,
                      char *buf, int *lenptr) {
    if(request->state != REQ_DONE && ! response->isPipelining) return;

    if(! response->preprocessed) {
        preprocess(request, response);
    }

    // status line
    if(response->state == 0) {
        char *statusline;
        switch (response->httpcode) {
        case 200:
            statusline = "HTTP/1.1 200 OK\r\n";
            break;
        case 400:
            statusline = "HTTP/1.1 400 BAD REQUEST\r\n";
            break;
        case 404:
            statusline = "HTTP/1.1 404 NOT FOUND\r\n";
            break;
        case 411:
            statusline = "HTTP/1.1 411 LENGTH REQUIRED\r\n";
            break;
        case 500:
        default:
            statusline = "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n";
            break;
        }
        if( ! addBuffer(buf, lenptr, &response->bufIndex, statusline))
            return;
        response->state = 1;
        logger(LOG_INFO, "RESPONSE STATUS LINE: %s", statusline);
    }

    // date
    if(response->state == 1) {
        if( ! addBuffer(buf, lenptr, &response->bufIndex, "Date: "))
            return;
        response->state = 2;
    }
    if(response->state == 2) {
        if( ! addBuffer(buf, lenptr, &response->bufIndex, response->datestr))
            return;
        response->state = 3;
    }

    // server
    if(response->state == 3) {
        if( ! addBuffer(buf, lenptr, &response->bufIndex,
                        "Server: Liso/1.0\r\n"))
            return;
        response->state = (response->httpcode != 200)? 4: 5 ;
    }
    if(response->state == 4) {
        if( ! addBuffer(buf, lenptr, &response->bufIndex, "Content-Length: 6\r\n\r\nFailed" ))
            return;
        start_pipelining(request, response);
        response->state = -1; // response done
    }

    // connection
    if(response->state == 5) {
        if( ! addBuffer(buf, lenptr, &response->bufIndex,
                        "Connection: close\r\n" ))
            return;
        response->state = 6;
    }

    logger(LOG_DEBUG, "RESPONSE: processing method specific content");

    switch (request->httpmethod) {
    case GET:
    case HEAD:
        if(response->state == 6) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex,
                            "Content-Type: " ))
                return;
            response->state = 7;
        }
        if(response->state == 7) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex,
                            response->mimetype ))
                return;
            response->state = 8;
        }
        if(response->state == 8) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex,
                            "Content-Length: " ))
                return;
            response->state = 9;
        }
        if(response->state == 9) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex,
                            response->ctlen ))
                return;
            response->state = 10;
        }
        if(response->state == 10) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex,
                            "Last-Modified: " ))
                return;
            response->state = 11;
        }
        if(response->state == 11) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex,
                            response->lmdate ))
                return;
            response->state = 12;
        }
        if(response->state == 12) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex, "\r\n" ))
                return;

            if(request->httpmethod == HEAD) {
                start_pipelining(request, response);
                response->state = -1; // response done
                return;
            }
            else
                response->state = 13;
        }
        if(response->state == 13) {
            if(! response->isPipelining){
                start_pipelining(request, response);
            }

            if( ! readFileContent(buf, lenptr, response->fp, response->fsize) )
                return;
            response->state = -1; // response done
            return;
        }

        break;
    case POST:
        if(response->state == 6) {
            if( ! addBuffer(buf, lenptr, &response->bufIndex, "\r\n" ))
                return;
            start_pipelining(request, response);
            response->state = -1; // response done
        }

        break;
    }

}

void start_pipelining(HttpRequest *request, HttpResponse *response) {
    response->isPipelining = 1;
    request->state = REQ_LINE;
    delete_request(request);
    init_request(request);
}

static void preprocess(HttpRequest *request, HttpResponse *response) {
    int needOpen = 0;
    char *path;
    struct stat filestat;

    response->preprocessed = 1;

    // date string
    response->datestr = getHTTPDate(time(NULL));
    if(! response->datestr ) {
        response->httpcode = 500;
        return;
    }

    if(response->httpcode != 200) return;

    switch (request->httpmethod) {
    case GET:
        needOpen = 1;
    case HEAD:
        path = make_path(get_WWW_folder(), request->uri, NULL);
        if(stat(path, &filestat) < 0 ) {
            del_path(path);
            response->httpcode = 404;
            return;
        } else {
            if(S_ISDIR(filestat.st_mode)) {
                del_path(path);
                path = make_path(get_WWW_folder(), request->uri, "/index.html");
                if(stat(path, &filestat) < 0 ) {
                    del_path(path);
                    response->httpcode = 404;
                    return;
                }
            }
        }
        logger(LOG_INFO, "Serving file: %s", path);

        // fsize, lmdate
        response->fsize = filestat.st_size;
        response->ctlen = getContentLength(response->fsize);
        response->lmdate = getHTTPDate(filestat.st_mtime);
        if(! response->ctlen || ! response->lmdate) {
            del_path(path);
            response->httpcode = 500;
            return;
        }

        // mime type
        getMIMEType(path, &response->mimetype);
        logger(LOG_INFO, "File Stat: size (%s), last-modified (%s)",
               response->ctlen, response->lmdate);

        if(needOpen) {
            // fp
            response->fp = fopen(path,"r");
            if( ! response->fp) {
                del_path(path);
                response->httpcode = 500;
                return;
            }
        }

        del_path(path);

        break;
    case POST:
        if(! checkHeader(&request->headers, "Content-Length") ){
            response->httpcode = 411;
            return;
        }
        break;
    }

}

static int checkHeader(Linlist *headers, char *key) {
    ll_Node *iter;
    iter = ll_start(headers);
    while(iter != ll_end(headers)) {
        if(KVPcompareKey(iter->item, key) == 0){
            return 1;
        }
        iter = ll_next(iter);
    }
    return 0;
}

static int addBuffer(char *buf, int *buflen, int *outIndex, char *out) {
    int outlen = strlen(out);
    int needToCopy = outlen - (*outIndex);
    int freeSpace = CLISOCK_BUFSIZE - (*buflen);
    // fit into writeBuffer
    if ( needToCopy <= freeSpace ) {
        memcpy(buf + (*buflen), out + (*outIndex), needToCopy);
        *buflen += needToCopy;
        *outIndex = 0;
        return 1;
    }
    else {
        memcpy(buf + (*buflen), out + (*outIndex), freeSpace);
        *buflen += freeSpace;
        *outIndex += freeSpace;
        return 0;
    }
}

// BUG:
//  There's a bug in this code. When IO error happened reading file from
//  disk, the data put into the write buffer could be unknown.
//  Note that this will not crash the web server, but it could be a
//  vulnerability.
static int readFileContent(char *buf, int *buflen, FILE *fp, int fsize) {
    int needToCopy = fsize - ftell(fp);
    int freeSpace = CLISOCK_BUFSIZE - (*buflen);
    logger(LOG_DEBUG, "left file bytes: %d, buffer free size: %d", needToCopy, freeSpace);
    if ( needToCopy <= freeSpace ) {
        // fit into writeBuffer
        fread(buf + (*buflen), 1, needToCopy, fp);
        *buflen += needToCopy;
        logger(LOG_DEBUG, "Reading %d bytes from file. Done!", needToCopy);
        return 1;
    }
    else {
        fread(buf + (*buflen), 1, freeSpace, fp);
        *buflen += freeSpace;
        logger(LOG_DEBUG, "Reading %d bytes from file", freeSpace);
        return 0;
    }
}

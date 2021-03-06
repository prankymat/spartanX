
//  Copyright (c) 2016, Yuji
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice, this
//  list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright notice,
//  this list of conditions and the following disclaimer in the documentation
//  and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
//  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
//  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
//  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//  The views and conclusions contained in the software and documentation are those
//  of the authors and should not be interpreted as representing official policies,
//  either expressed or implied, of the FreeBSD Project.
//
//  Created by yuuji on 5/9/16.
//  Copyright © 2016 yuuji. All rights reserved.

#include "SXServer.h"


SXServerRef SXCreateServer(sx_server_setup setup, SXError * err_ret, didReceive msg_handl)
{
    SXError tmp_err;
    SXServerRef server = (SXServerRef)sx_calloc(1, sizeof(sx_server_t));
    sx_obj_init(&server->obj, &SXFreeServer);
    sx_int16 port_num = setup.port;
    
    switch (setup.domain) {
    case 1:
        server->sock = SXCreateServerSocket(port_num, AF_INET, SOCK_STREAM, 0, &tmp_err);
        if (tmp_err != SX_SUCCESS) {
            sx_free(server);
            goto exit;
        }
        break;
    default:
        server->sock = SXCreateServerSocket(port_num, AF_INET6, SOCK_STREAM, 0, &tmp_err);
        if (tmp_err != SX_SUCCESS) {
            sx_free(server);
            goto exit;
        }
    }
    
    server->dataHandler_block = msg_handl;
    server->backlog = setup.backlog;
    server->dataSize = setup.dataSize;
    server->max_guest = setup.max_guest;
    server->failable = setup.failable;
    
    tmp_err = SX_SUCCESS;
    
exit:
    if (err_ret != NULL)
        *err_ret = tmp_err;
    return server;
}

void SXServerSetBlockDidStart
(SXServerRef server, didStart b ) {
    server->didStart_block = b;
}

void SXServerSetBlockShouldConnect
(SXServerRef server, shouldConnect b )
{
    server->shouldConnect_block = b;
}

void SXServerSetBlockDidKill
(SXServerRef server, didKill b ) {
    server->didKill_block = b;
}

SXError SXFreeServer(SXServerRef server)
{
    if (server->status != sx_status_idle)
        SXKillServer(server);
    
    SX_RETURN_ERR(SXFreeSocket(server->sock));
    
    memset(server, 0, sizeof(sx_server_t));
    sx_free(server);
    server= NULL;
    return SX_SUCCESS;
}

#define SXCHECK_COMPATIBLES(arr, c) SX_RETURN_ERR(SXCheckCompatibleStatus(server->status,arr, c))
#define SXCHECK_INCOMPATIBLES(arr, c) SX_RETURN_ERR(SXCheckIncompatibleStatus(server->status,arr, c))

SXError SXSuspendServer(SXServerRef server)
{
    if (server== NULL || server->ref_count == 0)
        return SX_ERROR_INVALID_SERVER;
    
    sx_status_t a[2] = {sx_status_idle, sx_status_running};
    SXCHECK_COMPATIBLES(a, 2);
    
    server->status = sx_status_suspend;
    return SX_SUCCESS;
}

SXError SXResumeServer(SXServerRef server)
{
    if (server== NULL || server->ref_count == 0)
        return SX_ERROR_INVALID_SERVER;
    
    SX_RETURN_ERR(SXCheckStatus(server->status, sx_status_suspend));
    
    server->status = sx_status_resuming;
    return SX_SUCCESS;
}

SXError SXKillServer(SXServerRef server)
{
    if (server== NULL || server->ref_count == 0)
        return SX_ERROR_INVALID_SERVER;
    
    if (server->status == sx_status_suspend) SXResumeServer(server);
    
    sx_status_t s = sx_status_should_terminate;
    SXCHECK_INCOMPATIBLES(&s, 1);
    
    server->status = sx_status_should_terminate;
    close(server->sock->sockfd);
    server->sock = SXCreateServerSocket(server->sock->port,
                                          server->sock->domain,
                                          server->sock->type,
                                          server->sock->protocol,
                                          NULL);
    return SX_SUCCESS;
}


#undef SXCHECK_COMPATIBLES
#undef SXCHECK_INCOMPATIBLES

SXError SXServerStart1(SXServerRef server,
                       dispatch_queue_priority_t listen_queue_priority,
                       dispatch_queue_priority_t operate_queue_priority) //,
                       //bool use_blocks_handler)
{
    return SXServerStart(server, ^dispatch_queue_t{
        return dispatch_get_global_queue(listen_queue_priority, 0);
    }, ^dispatch_queue_t{
        return dispatch_get_global_queue(operate_queue_priority, 0);
    });
    //, use_blocks_handler);
}

SXError SXServerStart2(SXServerRef server,
                       dispatch_queue_priority_t priority)//,
                       //bool use_blocks_handler)
{
    return SXServerStart(server, ^dispatch_queue_t{
        return dispatch_get_global_queue(priority, 0);
    }, ^dispatch_queue_t{
        return dispatch_get_global_queue(priority, 0);
    });
}

#define has_handler(fname) !(server->CAT(fname, _block) == NULL)
#define has_handler_q(fname) !(queue->CAT(fname, _block) == NULL)

#define SERVER_HAS_STATUS(stat) (server->status == stat)

#define use_block(fname, args) server->CAT(fname, _block) args
#define use_block_q(fname, args) queue->CAT(fname, _block) args

#define eval(fname, args) (use_block(fname, args))
#define eval_q(fname, args) (use_block_q(fname, args))

#define eval_if_exist(fname, args) if (has_handler(fname)) eval(fname, args);
#define eval_if_exist_q(fname, args) if (has_handler_q(fname)) eval_q(fname, args);

#ifdef __DISPATCH_PUBLIC__

SXError SXServerStart(SXServerRef server,
                      block_queue_generate_policy preprocessQueue,
                      block_queue_generate_policy operatingQueue)
{
    if (server== NULL) return SX_ERROR_INVALID_SERVER;
    if (preprocessQueue == NULL) return SX_ERROR_CREATE_THREAD;
    if (operatingQueue == NULL) return SX_ERROR_CREATE_THREAD;
    __block SXError err;
    
    if (server->sock->type == SOCK_DGRAM)
        return SX_ERROR_SERVER_CANNOT_BE_DGRAM;
    
    dispatch_queue_t p_queue = preprocessQueue();

    SXRetain(server);
    
    dispatch_async(p_queue, ^{
        server->status = sx_status_running;
        eval_if_exist(didStart, (server));
        __block int count = 0;

        while (server->status != sx_status_should_terminate)
        {
            SXSocketListen(server->sock);
            
            if (server->status == sx_status_should_terminate)  break;
            else if (server->status == sx_status_suspend) continue;
            
            if (count >= server->max_guest)
                continue;
            
            ++count;
            sx_socket_t sock;
            
            if ((sock.sockfd = accept(server->sock->sockfd, (struct sockaddr *)&sock.addr, (socklen_t *)&sock.addr.ss_len)) == -1) {
                perror("accept");
                server->errHandler_block(server, SX_ERROR_SYS_ACCEPT);
                continue;
            }

            sock.domain = AF_UNSPEC;
            
            if (has_handler(shouldConnect))
                if (!eval(shouldConnect, (server, (SXSocketRef)&sock)))
                    continue;
            
            
            dispatch_queue_t r_queue = operatingQueue();
            
            dispatch_async(r_queue, ^{
                size_t s = 1;
                sx_socket_t socket = sock;
                socket.obj.ref_count = sx_weak_object;
                SXQueueRef queue = SXCreateQueue((SXSocketRef)&sock, r_queue, NULL);
                queue->status = sx_status_running;
                
                
                // set the default handlers of the queue
                transfer_fn(queue, server, didConnect);
                transfer_fn(queue, server, dataHandler);
                transfer_fn(queue, server, didDisconnect);
                transfer_fn(queue, server, willSuspend);
                transfer_fn(queue, server, didResume);
                transfer_fn(queue, server, willKill);
                
                SXRetain(server);
                bool suspended = false;
                eval_if_exist_q(didConnect, (queue));
                
                sx_byte * buf = (sx_byte *)sx_calloc(server->dataSize, sizeof(sx_byte));
                
                do {
                    memset(buf, 0, sizeof(sx_byte) * server->dataSize);
                    
                    if (server->status != sx_status_running)
                        queue->status = server->status;
                    
                    switch (queue->status)
                    {
                    case sx_status_running:
                        s = recv(sock.sockfd, buf, server->dataSize, 0);
                        if (s == -1)
                            goto exit;
                        s = eval_q(dataHandler, (queue, buf, s));
                        break;
                        
                    case sx_status_resuming:
                        queue->status = sx_status_running;
                        eval_if_exist_q(didResume, (queue));
                        break;
                        
                    case sx_status_suspend: {
                        if (!suspended)
                            eval_if_exist_q(willSuspend, (queue));
                        suspended = true;
                        
                        
                        size_t len = recv(sock.sockfd, buf, server->dataSize, 0);
                        if (len == 0 || len == -1) goto exit;
                        
                        // check the status when the message recvieved
                        switch (queue->status) {
                        case sx_status_should_terminate:
                        case sx_status_idle:
                            goto exit;
                            
                        case sx_status_resuming:
                        case sx_status_running:
                            s = eval_q(dataHandler, (queue, buf, len));
                            break;
                            
                        default:
                            break;
                        }
                        break;
                    }
                    case sx_status_should_terminate:
                    case sx_status_idle:
                        eval_if_exist_q(willKill, (queue));
                        goto exit;
                        
                    default:
                        break;
                    }
                    
                } while (s > 0);
                
            exit:
                close(socket.sockfd);
                eval_if_exist_q(didDisconnect, (queue));
                SXRelease(server);
                SXRelease(queue);
                --count;
            });
        }
        
        server->status = sx_status_idle;
        SXRelease(server);
        
        eval_if_exist(didKill, (server));
        
    });
    return SX_SUCCESS;
}

SXError SXServerStart_kqueue(SXServerRef server, dispatch_queue_t gcd_queue)
{
    if (server->sock->type == SOCK_DGRAM)
        return SX_ERROR_SERVER_CANNOT_BE_DGRAM;
    
    SXRetain(server);
    dispatch_async(gcd_queue, ^{
        SXError err;
        server->status = sx_status_running;
        eval_if_exist(didStart, (server));
        size_t count = 0, n_ev_changes;
        int kq;
        
        #ifdef __APPLE__
            struct kevent64_s events[server->max_guest + 1];
            struct kevent64_s change;
        #else
            struct kevent events[server->max_guest + 1];
            struct kevent change;
        #endif
        if ((kq = kqueue()) == -1) {
            perror("kqueue");
            err = SX_ERROR_SYS_KQUEUE;
            return;
        }
        
        SXSocketListen(server->sock);
        
        #ifdef __APPLE__
                EV_SET64(&change, server->sock->sockfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0, 0, 0);
                kevent64(kq, &change, 1, NULL, 0, 0, NULL);
        #else
            EV_SET(&change, server->sock->sockfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, 0);
            kevent(kq, &change, 1, NULL, 0, NULL);
        #endif

        while (server->status != sx_status_should_terminate) {
    
            #ifdef __APPLE__
                if ((n_ev_changes = kevent64(kq, NULL, 0, events, (int)server->max_guest + 1, 0, 0)) == -1) {
                    perror("kevent64");
                    err = SX_ERROR_SYS_KEVENT;
                    return;
                }
            #else
                if ((n_ev_changes = kevent(kq, NULL, 0, events, server->max_guest + 1, 0)) == -1) {
                    perror("kevent");
                    err = SX_ERROR_SYS_KEVENT;
                    return;
                }
            #endif
            
            for (int i = 0; i < n_ev_changes; ++i)
            {

                if (events[i].ident == server->sock->sockfd) {

                    if (!(count >= server->max_guest))
                    {
                        SXSocketRef socket = (SXSocketRef)sx_calloc(1, sizeof(sx_socket_t));
                        sx_obj_init(socket, &SXFreeSocket);
                        socket->domain = AF_UNSPEC;
                        if ((socket->sockfd = accept(server->sock->sockfd,
                                                    (struct sockaddr *)&socket->addr,
                                                    (socklen_t *)&socket->addr)) == -1) {
                            perror("accept");
                            SXRelease(socket);
                        } else {
                            SXQueueRef queue = SXCreateQueue((SXSocketRef)socket, NULL, &err);
                            if (queue != NULL) {
                            #ifdef __APPLE__
                                EV_SET64(&change, socket->sockfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, (unsigned long)queue, 0, 0);
                                kevent64(kq, &change, 1, NULL, 0, 0, 0);
                            #else
                                EV_SET(&change, socket->sockfd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, queue);
                                kevent(kq, &change, 1, NULL, 0, NULL);
                            #endif
                                queue->status = sx_status_running;
                                count++;
                            }
                        }
                    }
                } else {
                    SXQueueRef queue = (SXQueueRef)events[i].udata;
                    size_t s;
                    bool suspended;
                    sx_byte buf[server->dataSize];
                    memset(buf, 0, sizeof(sx_byte) * server->dataSize);
                    
                    if (server->status != sx_status_running)
                        queue->status = server->status;
                    switch (queue->status) {
                        case sx_status_running:
                            s = recv(queue->sock->sockfd, buf, server->dataSize, 0);
                            if (s != -1) {
                                if ((s = eval(dataHandler, (queue, buf, s))) == -1)
                                    goto exit;
                                break;
                            } else {
                                goto exit;
                            }
                        case sx_status_resuming:
                            queue->status = sx_status_running;
                            eval_if_exist(didResume, (queue));
                            break;
                            
                        case sx_status_suspend: {
                            if (!suspended)
                                eval_if_exist(willSuspend, (queue));
                            suspended = true;
                            
                            
                            size_t len = recv(queue->sock->sockfd, buf, server->dataSize, 0);
                            if (len == 0 || len == -1) goto exit;
                            
                            // check the status when the message recvieved
                            switch (queue->status) {
                                case sx_status_should_terminate:
                                case sx_status_idle:
                                    goto exit;
                                    
                                case sx_status_resuming:
                                case sx_status_running:
                                    s = eval(dataHandler, (queue, buf, len));
                                    break;
                                    
                                default:
                                    break;
                            }
                        }
                        case sx_status_should_terminate:
                            
                        case sx_status_idle:
                            eval_if_exist(willKill, (queue));
                            goto exit;
                            
                        default:
                            break;
                    }
                    
                    if (s == 0)
                        goto exit;

                    continue;
                exit:
                #ifdef __APPLE__
                    EV_SET64(&change, queue->sock->sockfd, EVFILT_READ, EV_DELETE | EV_DISABLE, 0, 0, 0, 0, 0);
                    kevent64(kq, &change, 1, NULL, 0, 0, 0);
                #else
                    EV_SET(&change, queue->sock->sockfd, EVFILT_READ, EV_DELETE | EV_DISABLE, 0, 0, 0);
                    kevent(kq, &change, 1, NULL, 0, 0);
                #endif
                    close(queue->sock->sockfd);
                    SXRelease(queue);
                    eval_if_exist(didDisconnect, (queue));

                    --count;
                }
            }
        }
    });
    SXRelease(server);
    return SX_SUCCESS;
}
#endif
#undef SERVER_HAS_STATUS
#undef use_block
//#undef use_fn
#undef eval
#undef eval_if_exist
#undef has_handler
#undef use_block_q
//#undef use_fn_q
#undef eval_q
#undef eval_if_exist_q
#undef has_handler_q

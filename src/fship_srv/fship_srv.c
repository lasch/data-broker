/*
 * Copyright © 2020 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "logutil.h"
#include "common/utility.h"
#include <libdatabroker.h>
#include "libdatabroker_int.h"
#include "common/resolve_addr.h"
#include "fship_srv.h"
#include "lib/backend.h"

#include "common/request_queue.h"
#include "common/completion.h"

#include <errno.h> // errno
#include <unistd.h> // fork
#include <stdlib.h> // exit
#include <sys/socket.h> // socket
#include <pthread.h> // pthread_create/join/...
#include <event2/event.h> // libevent
#include <event2/thread.h> // evthread_use_pthreads
#include <string.h> // strncmp

#define DBR_MCTX_RC( a, rc ) ( (rc) == 0 ? (a) : (rc) )

int dbrFShip_main_context_destroy( dbrFShip_main_context_t *ctx, const int rc )
{
  if( ctx == NULL )
    return DBR_MCTX_RC( -EINVAL, rc );

  if( ctx->_mctx )
    dbrMain_exit();

  if( ctx->_cctx )
    free( ctx->_cctx );

  if( ctx->_r_buf )
    dbBE_Transport_sr_buffer_free( ctx->_r_buf );

  if( ctx->_s_buf )
    dbBE_Transport_sr_buffer_free( ctx->_s_buf );

  if( ctx->_conn_queue )
    dbBE_Connection_queue_destroy( ctx->_conn_queue );

  free( ctx );
  dbrMain_exit();

  return DBR_MCTX_RC( 0, rc );
}

dbrFShip_main_context_t* dbrFShip_main_context_create( dbrFShip_config_t *cfg )
{
  if( cfg == NULL )
    return NULL;

  dbrFShip_main_context_t *ctx = ( dbrFShip_main_context_t* )calloc( 1, sizeof( dbrFShip_main_context_t ));
  if( ctx == NULL )
    return NULL;

  memcpy( &ctx->_cfg, cfg, sizeof( dbrFShip_config_t ) );

  ctx->_mctx = dbrCheckCreateMainCTX();
  if( ctx->_mctx == NULL )
  {
    free( ctx );
    return NULL;
  }

  ctx->_conn_queue = dbBE_Connection_queue_create( DBR_FSHIP_CONNECTIONS_LIMIT );
  ctx->_r_buf = dbBE_Transport_sr_buffer_allocate( cfg->_max_mem >> 1 );
  if( ctx->_r_buf == NULL )
  {
    dbrFShip_main_context_destroy( ctx, -ENOMEM );
    return NULL;
  }

  ctx->_s_buf = dbBE_Transport_sr_buffer_allocate( cfg->_max_mem >> 1 );
  if( ctx->_s_buf == NULL )
  {
    dbrFShip_main_context_destroy( ctx, -ENOMEM );
    return NULL;
  }
  return ctx;
}

int main( int argc, char **argv )
{
  dbrFShip_config_t cfg;
  if( dbrFShip_parse_cmdline( argc, argv, &cfg ) != 0 )
    return 1;

  // daemonize if (-d)
  if( cfg._daemon != 0 )
  {
    pid_t pid = fork();
    if( pid < 0 )
    {
      LOG( DBG_ERR, stderr, "failed to fork fship_srv daemon.\n" );
      exit( 1 );
    }
    if( pid != 0 )
      exit( 0 );
  }

  dbrFShip_main_context_t *context = dbrFShip_main_context_create( &cfg );
  if( context == NULL )
    return ENOMEM;


  evthread_use_pthreads();
  struct event_base *evbase = event_base_new();

  // create/listen on passive socket
  //  socket/bind/listen/accept
  pthread_t listener;

  dbrFShip_threadio_t tio;
  memset( &tio, 0, sizeof( tio ));
  tio._evbase = evbase;
  tio._threadrc = 0;
  tio._cfg = &context->_cfg;
  tio._conn_queue = context->_conn_queue;
  tio._keep_running = 1;

  if( pthread_create( &listener, NULL, dbrFShip_listen_start, &tio ) != 0 )
  {
    return dbrFShip_main_context_destroy( context, -ECHILD );
  }


  // loop
  int rc = 0;
  while( 1 )
  {
    rc = dbrFShip_inbound( &tio, context );
    if( rc < 0 )
      break;

    rc = dbrFShip_outbound( &tio, context );
    if( rc < 0 )
      break;
  }

  pthread_join( listener, NULL );

  if( tio._threadrc != 0 )
    LOG( DBG_ERR, stderr, "Listener thread exited with rc=%d\n", tio._threadrc );

  return dbrFShip_main_context_destroy( context, rc );
}

int dbrFShip_inbound( dbrFShip_threadio_t *tio, dbrFShip_main_context_t *context )
{
  // check for new inbound requests
  //
  if( context->_total_pending > 0 )
    event_base_loop( tio->_evbase, EVLOOP_ONCE | EVLOOP_NONBLOCK );
  else
    event_base_loop( tio->_evbase, 0 );

  dbBE_Connection_t *active = dbBE_Connection_queue_pop( tio->_conn_queue );
  if( active == NULL )
    return 0;

  // find the corresponding client context
  dbrFShip_client_context_t *cctx = (dbrFShip_client_context_t*)active->_context;
  if( cctx == NULL ) // unknown connection context
  {
    LOG( DBG_ERR, stderr, "FATAL: Found active connection without valid connection context.\n" )
    return -1;
  }


  // recv() +  deserialize()
  dbBE_Request_t *req = NULL;
  ssize_t parsed = -EAGAIN;
  ssize_t total = 0;
  while( parsed == -EAGAIN )
  {
    ssize_t rcvd = recv( cctx->_conn->_socket,
                         dbBE_Transport_sr_buffer_get_available_position( context->_r_buf ),
                         dbBE_Transport_sr_buffer_remaining( context->_r_buf ),
                         0 );
    if(( rcvd == 0 ) && ( errno == EAGAIN ))
      continue;

    if( rcvd < 0 )
      return -1;
    dbBE_Transport_sr_buffer_add_data( context->_r_buf, rcvd, 0 );

    parsed = dbBE_Request_deserialize( dbBE_Transport_sr_buffer_get_processed_position( context->_r_buf ),
                                       dbBE_Transport_sr_buffer_available( context->_r_buf ),
                                       &req );
    if( parsed > 0 )
    {
      total += parsed;
      dbBE_Transport_sr_buffer_advance( context->_r_buf, parsed );
    }
  }

  //   create request()
//    int64_t rc;
  dbrFShip_request_ctx_t *rctx = (dbrFShip_request_ctx_t*)calloc( 1, sizeof( dbrFShip_request_ctx_t ));
  if( rctx == NULL )
    return -ENOMEM;
  rctx->_user_in = req->_user;
  rctx->_cctx = cctx;
  rctx->_req = req;
  req->_user = rctx;
  dbBE_Request_handle_t be_req = g_dbBE.post( context->_mctx->_be_ctx, req, 0 );
  if( be_req == NULL )
  {
    // todo: create error response with DBR_ERR_BE_POST
    return -1;
  }
  ++context->_total_pending;
  return dbrFShip_request_ctx_queue_push( cctx->_pending, rctx );
}

int dbrFShip_outbound( dbrFShip_threadio_t *tio, dbrFShip_main_context_t *context )
{
  // check for completions

  dbBE_Completion_t *comp = NULL;
  if( (comp = g_dbBE.test_any( context->_mctx->_be_ctx )) == NULL )
    return 0;

  dbrFShip_request_ctx_t *rctx = (dbrFShip_request_ctx_t*)comp->_user;
  if( rctx == NULL )
    return -EPROTO;

  // restore user ptr
  comp->_user = rctx->_user_in;

  ssize_t serlen = dbBE_Completion_serialize( rctx->_req->_opcode, comp, rctx->_req->_sge, rctx->_req->_sge_count,
                                              dbBE_Transport_sr_buffer_get_available_position( context->_s_buf ),
                                              dbBE_Transport_sr_buffer_remaining( context->_s_buf ) );
  if( serlen < 0 )
    return (int)serlen;

  dbBE_Transport_sr_buffer_add_data( context->_s_buf, serlen, 1 );

  ssize_t sent = -EAGAIN;
  ssize_t total = 0;
  while(( sent == -EAGAIN ) && ( total < (ssize_t)dbBE_Transport_sr_buffer_available( context->_s_buf )) )
  {
    sent = send( rctx->_cctx->_conn->_socket,
                 dbBE_Transport_sr_buffer_get_processed_position( context->_s_buf ),
                 dbBE_Transport_sr_buffer_available( context->_s_buf ),
                 0 );
    if( sent < 0 )
      return -1;
    dbBE_Transport_sr_buffer_advance( context->_s_buf, sent );

    total += sent;
  }

  // clean Request from rctx-queue (note: out-of-order completion possible, therefore assume we can't just q_pop())
  dbBE_Request_free( rctx->_req );
  dbrFShip_client_context_t *cctx = rctx->_cctx;
  rctx->_req = NULL;
  rctx->_cctx = NULL;
  rctx->_user_in = NULL;
  if( rctx == cctx->_pending->_head )
  {
    rctx = dbrFShip_request_ctx_queue_pop( cctx->_pending );
    if( rctx != NULL )
      free( rctx );
  }

  --context->_total_pending;

  return 0;
}

void usage()
{
  fprintf( stderr, " fship_srv [options]\n\n"\
                   "   -h        display help\n"\
                   "   -d        run as daemon\n"\
                   "   -l <url>  listen at provided URL\n"\
                   "   -M <MB>   max buffering memory size in MB\n\n");
}

int dbrFShip_parse_cmdline( int argc, char **argv, dbrFShip_config_t *cfg )
{
  if( cfg == NULL )
    return -EINVAL;

  int option;
  cfg->_daemon = 0;
  cfg->_listenaddr = "localhost";
  cfg->_max_mem = 512; // reserve 512M by default
  while(( option = getopt(argc, argv, "dhl:M:")) != -1 )
  {
    // locally check common options; callback for extra options
    switch( option )
    {
      case 'h':
        usage();
        exit(0);
      case 'd': // daemonize
        cfg->_daemon = 1;
        break;
      case 'l': // listener
        cfg->_listenaddr = optarg;
        break;
      case 'M': // max memory for data buffering
        cfg->_max_mem = strtol( optarg, NULL, 10 );
        break;
      default:
        usage();
        return -EINVAL;
    }
  }
  return 0;
}

void dbrFShip_connection_wakeup( evutil_socket_t socket, short ev_type, void *arg )
{
  LOG( DBG_TRACE, stderr, "Triggered callback for socket=%d, ev_type=%d, arg=%p\n", socket, ev_type, arg );

  dbrFShip_event_info_t *info = (dbrFShip_event_info_t*)arg;
  if( info == NULL )
  {
    LOG( DBG_ERR, stderr, "Triggered callback with NULL ptr argument.\n" );
    return;
  }

  dbBE_Connection_t *conn = info->_cctx->_conn;
  dbBE_Connection_queue_t *queue = info->_queue;

  if(( conn == NULL ) || ( queue == NULL ))
  {
    LOG( DBG_ERR, stderr, "Triggered callback with invalid connection=%p or queue=%p data\n", conn, queue );
    return;
  }

  LOG( DBG_TRACE, stderr,
       "Triggered callback for connection=%p, socket=%d, ev_type=%d\n",
       conn, conn->_socket, ev_type );
  if((( ev_type & EV_TIMEOUT ) != 0 ) && (( ev_type & EV_READ ) == 0 ))
  {
    LOG( DBG_VERBOSE, stderr, "Connection timeout detected (sock=%d).\n", conn->_socket );
  }
  else if( ( ev_type & EV_READ ) != 0 )
  {
    LOG( DBG_TRACE, stderr, "Connection activated (sock=%d)\n", conn->_socket );
  }
  else
  {
    LOG( DBG_ERR, stderr, "event_mgr_callback: invalid event type triggered.\n" );
  }

  dbBE_Connection_queue_push( queue, conn );
}

void* dbrFShip_listen_start( void *arg )
{
  if( arg == NULL )
    return NULL;

  struct dbrFShip_threadio *tio = (struct dbrFShip_threadio*)arg;

  struct event_base *evbase = tio->_evbase;
  if( evbase == NULL )
    return NULL;

  int s;

  char *url = tio->_cfg->_listenaddr;
  struct addrinfo *addrs = dbBE_Common_resolve_address( url, 0 );
  struct addrinfo *cur = addrs;
  while( cur != NULL )
  {
    s = socket( AF_INET, SOCK_STREAM, 0 );
    if( s < 0 )
    {
      tio->_threadrc = -errno;
      break;
    }

    if( bind( s, cur->ai_addr, cur->ai_addrlen ) == 0 )
    {
      break;
    }

    tio->_threadrc = -errno;
    close( s );
    cur = cur->ai_next;
  }

  dbBE_Common_release_addrinfo( &addrs );

  if( s < 0 )
    return tio;

  int backlog = 128;
  if( listen( s, backlog ) != 0 )
  {
    tio->_threadrc = -errno;
    close( s );
    return tio;
  }

  while( tio->_keep_running )
  {
    struct sockaddr naddr;
    socklen_t naddrlen = sizeof( naddr );

    int nes = accept( s, &naddr, &naddrlen );
    if( nes > 0 )
    {
      dbBE_Connection_t *connection = dbBE_Connection_create();
      connection->_socket = nes;
      connection->_status = DBBE_CONNECTION_STATUS_AUTHORIZED;
      connection->_address = dbBE_Network_address_copy( &naddr, naddrlen );
      if( dbBE_Network_address_to_string( connection->_address, connection->_url, DBBE_URL_MAX_LENGTH ) == NULL )
      {
        LOG( DBG_ERR, stderr, "Network address translation to URL failed.\n" );
        return NULL;
      }

      dbrFShip_client_context_t *cctx = (dbrFShip_client_context_t*)calloc( 1, sizeof( dbrFShip_client_context_t ));
      if( cctx == NULL )
        return NULL;

      // bidirectional linking between connection and its context
      cctx->_conn = connection;
      cctx->_pending = dbrFShip_request_ctx_queue_create();
      connection->_context = (void*)cctx;
      gettimeofday( &connection->_last_alive, NULL );

      dbrFShip_event_info_t *evinfo = (dbrFShip_event_info_t*)malloc( sizeof( dbrFShip_event_info_t ));
      if( evinfo == NULL )
        return NULL;
      evinfo->_cctx = cctx;
      evinfo->_queue = tio->_conn_queue;

      // add to libevent socket polling
      struct event* ev = event_new( evbase, nes, EV_READ | EV_ET, dbrFShip_connection_wakeup, evinfo );
      if( ev == NULL )
        return NULL;

      struct timeval timeout;
      timeout.tv_sec = 1;
      timeout.tv_usec = 0;
      event_add( ev, &timeout );
    }
  }

  return tio;
}
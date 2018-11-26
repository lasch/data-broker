/*
 * Copyright © 2018 IBM Corporation
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
#include "libdatabroker_int.h"

#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

DBR_Errorcode_t dbrMapErrno_to_errorcode( const int rc )
{
  switch( rc )
  {
    case 0:
      return DBR_SUCCESS;
    case EINVAL:
    case EMSGSIZE:
      return DBR_ERR_INVALID;
    case ETIMEDOUT:
      return DBR_ERR_TIMEOUT;
    case ENODATA:
    case ENOENT:
      return DBR_ERR_UNAVAIL;
    case EEXIST:
      return DBR_ERR_EXISTS;
    case ENOMEM:
      return DBR_ERR_NOMEMORY;
    case EBADF:
      return DBR_ERR_NOFILE;
    case EPERM:
      return DBR_ERR_NOAUTH;
    case ENOTCONN:
      return DBR_ERR_NOCONNECT;
    case ENOTSUP:
    case ENOSYS:
      return DBR_ERR_NOTIMPL;
    case EBADMSG:
      return DBR_ERR_INVALIDOP;
    case ENOMSG:
      return DBR_ERR_BE_POST;
    case EPROTO:
      return DBR_ERR_BE_PROTO;
    default:
      break;
//      DBR_ERR_HANDLE,  /**< An invalid handle was encountered*/
//      DBR_ERR_INPROGRESS, /**< A request is still in progress, check again later*/
//      DBR_ERR_UBUFFER, /**< Provided user buffer problem (too small, not available)*/
//      DBR_ERR_NSBUSY, /**< There are still clients attached to a namespace*/
//      DBR_ERR_NSINVAL, /**< Invalid name space*/
//      DBR_ERR_TAGERROR, /**< The returned tag is an error*/
//      DBR_ERR_CANCELLED, /**< Operation was cancelled*/
//      DBR_ERR_GENERIC, /**< A general or unknown error has occurred*/
  }
  return DBR_ERR_BE_GENERAL;
}


DBR_Errorcode_t dbrCheck_response( dbrRequestContext_t *rctx )
{
  if( rctx == NULL )
    return DBR_ERR_INVALID;

  dbBE_Request_t *req = &rctx->_req;
  dbBE_Completion_t *cpl = &rctx->_cpl;

  int64_t rsize = dbrSGE_extract_size( req );
  DBR_Errorcode_t rc = DBR_SUCCESS;

// todo: we can do better error reporting here....
  // the status mostly contains a very generic error with _rc carrying the actual error code (-errno) from the backend)
  if(( req->_opcode != DBBE_OPCODE_READ )&&( cpl->_rc < 0 ))
    return dbrMapErrno_to_errorcode( -cpl->_status );

  switch( req->_opcode )
  {
    case DBBE_OPCODE_PUT:
      // good if completion rc bytes match 1 or more (number of inserted items)
      if( cpl->_rc < 1 )
        rc = DBR_ERR_UBUFFER;
      break;
    case DBBE_OPCODE_READ:
      if( cpl->_rc < 0 )
      {
        rc = DBR_ERR_UNAVAIL;
        cpl->_rc = 0;
      }
      // no break on purpose
    case DBBE_OPCODE_GET:
      // good if the completion rc bytes is less or equal the size in SGEs
      if( rsize < cpl->_rc )
        rc = DBR_ERR_UBUFFER;
      // operation in progress if the returned data size is NULL buy completion says: success
      if( cpl->_status == DBR_SUCCESS )
      {
        if( cpl->_rc < 0 )
          rc = DBR_ERR_INVALID;
        else if( rctx->_rc ) // check if not NULL!
          *rctx->_rc = cpl->_rc; // set the return value
      }
      else
        rc = cpl->_status;
      break;

    case DBBE_OPCODE_DIRECTORY:
      // good if the completion rc bytes is less or equal the size in SGEs
      if( rsize < cpl->_rc )
        rc = DBR_ERR_UBUFFER;
      if( cpl->_status == DBR_SUCCESS )
      {
        if( cpl->_rc < 0 )
          rc = DBR_ERR_INVALID;
        else if( rctx->_rc )
          *rctx->_rc = cpl->_rc;
      }
      else
        rc = cpl->_status;
      break;

    case DBBE_OPCODE_MOVE:
      fprintf(stderr, "ERR: not yet supported!\n");
      break;

    case DBBE_OPCODE_REMOVE:
      rc = cpl->_status;
      break;

    case DBBE_OPCODE_NSCREATE:
    case DBBE_OPCODE_NSADDUNITS:
    case DBBE_OPCODE_NSREMOVEUNITS:
      // good if the completion rc is 0
      if( cpl->_rc != 0 )
        rc = cpl->_status;
      break;
    case DBBE_OPCODE_NSATTACH:
    case DBBE_OPCODE_NSDETACH:
      // good if the completion rc is > 0
      if( cpl->_rc <= 0 )
        rc = cpl->_status;
      break;
    case DBBE_OPCODE_NSDELETE:
      // good if the completion rc is 0
      if( cpl->_rc != 0 )
      {
        if( cpl->_status == DBR_SUCCESS )
        {
          fprintf( stderr, "BUG in interaction with system library. Empty user-ptr in completion.\n" );
          rc = DBR_ERR_BE_GENERAL;  // this is a backend protocol error (if rc != 0, then it should not be successful)
        }
        else
          rc = cpl->_status;
      }
      break;
    case DBBE_OPCODE_NSQUERY:
      // good if the completion rc is > 0 with the sge locations containing the name space meta data
      if(( rsize < cpl->_rc ) || ( cpl->_rc == 0 ))
        rc = DBR_ERR_UBUFFER;
      break;
    default:
      return DBR_ERR_INVALIDOP;
  }

  return rc;
}


DBR_Errorcode_t dbrProcess_completion( dbrRequestContext_t *rctx,
                                    dbBE_Completion_t *compl )
{
  if(( rctx == NULL ) || ( compl == NULL ))
    return DBR_ERR_INVALID;

  // are we about to complete the correct request?
  if( compl->_user != rctx )
    return DBR_ERR_HANDLE;

  switch( compl->_rc )
  {
    case -ENOENT:
      rctx->_cpl._rc = -1;
      rctx->_cpl._status = DBR_ERR_UNAVAIL;
      rctx->_status = dbrSTATUS_READY;
      break;
    case -ENOTCONN:
      rctx->_cpl._rc = -1;
      rctx->_cpl._status = DBR_ERR_NOCONNECT;
      rctx->_status = dbrSTATUS_READY;
      break;
    default:
      rctx->_cpl._rc = compl->_rc;
      rctx->_cpl._status = compl->_status;
      rctx->_status = dbrSTATUS_READY;
      break;
  }

  return DBR_SUCCESS;
}

/*
 * grabs the first entry of the completion queue
 * processes the completion
 * if it was the desired request, then returns: the status (success/error)
 * if it was a different request, it returns DBR_INPROGRESS
 */
DBR_Errorcode_t dbrTest_request( dbrName_space_t *cs, dbrRequestContext_t *req_rctx )
{
  if( cs == NULL )
    return DBR_ERR_INVALID;

  dbBE_Request_t *request = &req_rctx->_req;
  if( request == NULL )
    return DBR_ERR_NSINVAL;

  DBR_Errorcode_t ret = DBR_ERR_INPROGRESS;

  // first, try to drive the backend and see if we can complete anything (else)
  dbBE_Completion_t *compl = g_dbBE.test_any( cs->_be_ctx );
  if( compl != NULL )
  {
    dbrRequestContext_t *cmpl_rctx = (dbrRequestContext_t*)compl->_user;
    if( cmpl_rctx == NULL )
    {
      fprintf( stderr, "BUG in interaction with system library. Empty user-ptr in completion.\n" );
      return DBR_ERR_BE_GENERAL; // if there was no user ptr attached, then we have a serious problem
    }
    dbrProcess_completion( cmpl_rctx, compl );
    free( compl ); // clean up
  }

  // then see if we completed this request
  if( req_rctx->_status == dbrSTATUS_READY )
  {
    // this guy is complete - clean up and return status
    ret = req_rctx->_cpl._status;
  }

  return ret;
}

DBR_Errorcode_t dbrWait_request( dbrName_space_t *cs,
                                 DBR_Request_handle_t hdl,
                                 int enable_timeout )
{
  if(( hdl == NULL ) || ( cs == NULL ))
    return DBR_ERR_INVALID;

  DBR_Errorcode_t rc;
  struct timeval start_time, now, elapsed;
  start_time.tv_sec = 0;
  elapsed.tv_sec = 0;

  /*
   * This loop should allow to drive the backend while waiting for completions
   * Reduces the requirement for a backend to make independent progress in a separate thread
   * Only uses gettimeofday every N loops to reduce the number of syscalls
   */

  uint64_t timeloops = 0; // have a loop count to reduce the amount of gettimeofday calls
  do
  {
    rc = dbrTest_request( cs, hdl );
    if((rc == DBR_ERR_INPROGRESS)
       && enable_timeout)
    {
      if( ((++timeloops) & 0xFFFF ) == 0 )
      {
        gettimeofday( &now, NULL );
        if( start_time.tv_sec == 0 )
        {
          start_time.tv_sec = now.tv_sec;
          start_time.tv_usec = now.tv_usec;
        }
        elapsed.tv_sec = (now.tv_sec * 1e6 + now.tv_usec) - (start_time.tv_sec * 1e6 + start_time.tv_usec);
        elapsed.tv_sec /= 1e6;
      }
    }
  } while( rc == DBR_ERR_INPROGRESS
           && ( elapsed.tv_sec < cs->_reverse->_config._timeout_sec ));

  // ToDo: if Timeout -> send first a cancel and wait for acknowledge.
  //       otherwise internal structures could be in danger.

  return rc;
}

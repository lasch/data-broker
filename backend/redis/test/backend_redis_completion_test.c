/*
 * Copyright © 2018,2019 IBM Corporation
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

#include "test_utils.h"
#include "common/dbbe_api.h"
#include "redis/definitions.h"
#include "redis/request.h"

#include "redis/complete.h"

int test_errors()
{
  int rc = 0;

  dbBE_Redis_result_t result;
  dbBE_Redis_request_t request;
  dbBE_Completion_t completion;

  memset( &result, 0, sizeof( result ) );
  memset( &request, 0, sizeof( request ) );
  memset( &completion, 0, sizeof( completion ) );

  rc += TEST( dbBE_Redis_complete_command( NULL, NULL, 0 ), NULL );
  rc += TEST( errno, EINVAL );
  rc += TEST( dbBE_Redis_complete_command( NULL, &result, 0 ), NULL );
  rc += TEST( errno, EINVAL );
  rc += TEST( dbBE_Redis_complete_command( &request, NULL, 0 ), NULL );
  rc += TEST( errno, EINVAL );

  // all NULL initialized request needs to run into nullptr spec entry
  rc += TEST( dbBE_Redis_complete_command( &request, &result, 0 ), NULL );
  rc += TEST( errno, EPROTO );

  // attach a completion and expect it back:
  request._completion = &completion;
  rc += TEST( dbBE_Redis_complete_command( &request, &result, 0 ), &completion );

  return rc;
}

int initialize_usr( dbBE_Request_t *usr,
                    dbBE_Opcode op,
                    char *key,
                    dbBE_sge_t *sge,
                    int sgec )
{
  if( usr == NULL )
    return 1;
  usr->_opcode = op;
  usr->_group = DBR_GROUP_EMPTY;
  usr->_key = key;
  usr->_match = "";
  usr->_next = NULL;
  usr->_ns_hdl = NULL;
  usr->_sge_count = sgec;
  memcpy( usr->_sge, sge, sgec * sizeof( dbBE_sge_t ) );
  return 0;
}

int test_put( dbBE_Redis_command_stage_spec_t *stage_specs,
              char *data,
              size_t datalen,
              dbBE_Request_t *usr )
{
  int rc = 0;

  dbBE_Redis_result_t result;   // result already parsed, so it has no impact on the completion of a put, just needs to be there
  dbBE_Redis_request_t *request;
  dbBE_Completion_t completion;
  dbBE_Completion_t *cmp = NULL;

  memset( &result, 0, sizeof( result ) );
  memset( &completion, 0, sizeof( completion ) );

  usr->_opcode = DBBE_OPCODE_PUT;

  // a regular successful put
  rc += TEST_NOT_RC( dbBE_Redis_request_allocate( usr ), NULL, request );

  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, 0 ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, 1 );
    rc += TEST( cmp->_status, DBR_SUCCESS );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }

  // a protocol failure: DBR_ERR_BE_GENERAL: general error in backend
  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, -EPROTO ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, 0 );
    rc += TEST( cmp->_status, DBR_ERR_BE_GENERAL );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }

  // an invalid parameter occurred: DBR_ERR_INVALID
  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, -EINVAL ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, 0 );
    rc += TEST( cmp->_status, DBR_ERR_INVALID );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }

  // an unexpected result type got returned: DBR_ERR_INVALID
  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, -EBADMSG ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, 0 );
    rc += TEST( cmp->_status, DBR_ERR_INVALID );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }

  // somewhere running out of memory: DBR_ERR_NOMEMORY
  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, -ENOMEM ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, 0 );
    rc += TEST( cmp->_status, DBR_ERR_NOMEMORY );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }


  // somewhere running out of memory: DBR_ERR_NOMEMORY
  rc += TEST_NOT_RC( dbBE_Redis_complete_cancel( request ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, 0 );
    rc += TEST( cmp->_status, DBR_ERR_CANCELLED );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }

  dbBE_Redis_request_destroy( request );

  /* todo: cover the remaining cases: either hard to trigger or returned when posting already
   *    * DBR_ERR_INPROGRESS: request not complete; potential timeout
   *    * DBR_ERR_HANDLE: invalid namespace hdl, or namespace not attached/exists
   *    * DBR_ERR_NOAUTH: not authorized to use put on this namespace
   *    * DBR_ERR_NOCONNECT: backend is not connected to storage service
   *    * DBR_ERR_NOIMPL: the requested backend has no PUT operation implemented
   *    * DBR_ERR_BE_POST: failed to post the request at some stage in the BE stack
   */

  return rc;
}

int test_get( dbBE_Redis_command_stage_spec_t *stage_specs,
              char *data,
              int64_t datalen,
              dbBE_Request_t *usr )
{
  int rc = 0;

  dbBE_Redis_result_t result;   // result already parsed, so it has no impact on the completion of a put, just needs to be there
  dbBE_Redis_request_t *request;
  dbBE_Completion_t completion;
  dbBE_Completion_t *cmp = NULL;

  memset( &result, 0, sizeof( result ) );
  memset( &completion, 0, sizeof( completion ) );

  usr->_opcode = DBBE_OPCODE_GET;

  TEST_BREAK( rc, "mem-allocation failed" );

  rc += TEST_NOT_RC( dbBE_Redis_request_allocate( usr ), NULL, request );

  // a regular successful get
  result._type = dbBE_REDIS_TYPE_CHAR;
  result._data._string._data = data;
  result._data._string._size = datalen;

  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, 0 ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, datalen );
    rc += TEST( cmp->_status, DBR_SUCCESS );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }

  // user buffer too small without requesting partial data
  result._type = dbBE_REDIS_TYPE_INT;
  result._data._integer = datalen * 2;

  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, -ENOSPC ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, datalen * 2 );
    rc += TEST( cmp->_status, DBR_ERR_UBUFFER );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }

  // user buffer too small AND requesting partial data
  usr->_flags = DBR_FLAGS_PARTIAL;
  rc += TEST_NOT_RC( dbBE_Redis_complete_command( request, &result, 0 ), NULL, cmp );
  if( cmp )
  {
    rc += TEST( cmp->_rc, datalen * 2 );
    rc += TEST( cmp->_status, DBR_SUCCESS );
    rc += TEST( cmp->_user, usr->_user );
    free( cmp );
  }


  dbBE_Redis_request_destroy( request );

  return rc;
}

int main( int argc, char ** argv )
{

  int rc = 0;

  size_t datalen = 126;
  char *data;
  dbBE_Redis_command_stage_spec_t *stage_specs;
  dbBE_Request_t *usr;

  rc += test_errors();

  rc += TEST_NOT_RC( (dbBE_Request_t*)calloc( 1, sizeof( dbBE_Request_t ) + DBBE_SGE_MAX * sizeof( dbBE_sge_t ) ), NULL, usr );
  rc += TEST_NOT_RC( dbBE_Redis_command_stages_spec_init(), NULL, stage_specs );
  rc += TEST_NOT_RC( generateLongMsg( datalen ), NULL, data );

  TEST_BREAK( rc, "Test preparation already failed" );

  dbBE_sge_t single_sge;
  single_sge.iov_base = data;
  single_sge.iov_len = datalen;
  rc += TEST( initialize_usr( usr, DBBE_OPCODE_UNSPEC, strdup("testkey"), &single_sge, 1 ), 0 );
  rc += test_put( stage_specs, data, datalen, usr );
  rc += test_get( stage_specs, data, datalen, usr );


  if( data != NULL ) free( data );
  dbBE_Redis_command_stages_spec_destroy( stage_specs );
  if( usr->_key != NULL ) free( usr->_key );
  if( usr != NULL ) free( usr );


  printf( "Test exiting with rc=%d\n", rc );
  return rc;
}

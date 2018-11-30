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

#ifdef __APPLE__
#include <stdlib.h>
#else
#include <malloc.h>
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libdatabroker.h>

#include "../backend/redis/locator_conn_list.h"
#include "test_utils.h"


int main( int argc, char ** argv )
{
  int rc = 0;
  int res = 0;

  dbBE_Redis_locator_connection_list_t lcl;
  dbBE_Redis_locator_connection_list_t *lclp;

  // test initialization
  rc += TEST( dbBE_Redis_locator_connection_list_init( NULL ), -EINVAL );
  rc += TEST( dbBE_Redis_locator_connection_list_init( &lcl ), 0 );
  rc += TEST( lcl._active, 0 );
  rc += TEST( lcl._size, 0 );
  rc += TEST( lcl._connections, NULL );


  // test allocation
  rc += TEST_NOT_RC( dbBE_Redis_locator_connection_list_create(), NULL, lclp );
  TEST_BREAK( rc, "LCL Init or allocation failed. Can't continue.\n" );

  rc += TEST( lclp->_active, 0 );
  rc += TEST( lclp->_size, 0 );
  rc += TEST( lclp->_connections, NULL );


  rc += TEST_RC( dbBE_Redis_locator_connection_list_append( NULL, 2 ), -EINVAL, res );
  rc += TEST( lcl._active, 0 );
  rc += TEST( lcl._size, 0 );

  rc += TEST_RC( dbBE_Redis_locator_connection_list_append( &lcl, 2 ), 0, res );
  rc += TEST( lcl._active, 1 );
  rc += TEST( lcl._size, DBBE_REDIS_CONN_LIST_SIZE_GRANULARITY );
  rc += TEST_NOT( lcl._connections, NULL );

  rc += TEST_RC( dbBE_Redis_locator_connection_list_get_first( &lcl ), 2, res );
  rc += TEST_RC( dbBE_Redis_locator_connection_list_append( &lcl, DBBE_REDIS_CONNECTION_INDEX_INVALID ), -EINVAL, res );

  printf( "Test exiting with rc=%d\n", rc );
  return rc;
}

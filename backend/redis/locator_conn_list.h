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

#ifndef BACKEND_REDIS_LOCATOR_CONN_LIST_H_
#define BACKEND_REDIS_LOCATOR_CONN_LIST_H_

#include "definitions.h"
#include "logutil.h"

#include <stddef.h>
#include <errno.h>
#ifdef __APPLE__
#include <stdlib.h>
#else
#include <malloc.h>  // malloc
#endif
#include <string.h>

/*
 * connection list size increases by this granularity
 */
#define DBBE_REDIS_CONN_LIST_SIZE_GRANULARITY ( 5 )

/*
 * signals an invaled connection index
 */
#define DBBE_REDIS_CONNECTION_INDEX_INVALID ( (dbBE_Redis_connection_index_t)( DBBE_REDIS_MAX_CONNECTIONS + 1 ))

/*
 * locator connection list structure to maintain a list of connection indices
 */
typedef struct
{
  dbBE_Redis_connection_index_t *_connections;  // list of connection indices
  int _active; // number of active connections
  int _size; // actually allocated size
} dbBE_Redis_locator_connection_list_t;


/*
 * initialize locator connection list
 */
static inline
int dbBE_Redis_locator_connection_list_init( dbBE_Redis_locator_connection_list_t *lcl )
{
  if( lcl == NULL )
    return -EINVAL;

  lcl->_active = 0;
  lcl->_connections = NULL;
  lcl->_size = 0;

  return 0;
}

/*
 * create a locator connection list
 */
static inline
dbBE_Redis_locator_connection_list_t* dbBE_Redis_locator_connection_list_create()
{
  dbBE_Redis_locator_connection_list_t *lcl = calloc( 1, sizeof( dbBE_Redis_locator_connection_list_t ) );
  if( lcl == NULL )
  {
    LOG( DBG_ERR, stderr, "Failed to allocate Redis locator connection list entry. errno=%d\n", errno )
    return NULL;
  }

  if( dbBE_Redis_locator_connection_list_init( lcl ) == 0 )
    return lcl;

  return NULL;
}

/*
 * reset locator connection list
 */
static inline
int dbBE_Redis_locator_connection_list_reset( dbBE_Redis_locator_connection_list_t *lcl )
{
  if( lcl == NULL )
    return -EINVAL;

  if( lcl->_connections != NULL )
    free( lcl->_connections );

  return dbBE_Redis_locator_connection_list_init( lcl );
}

/*
 * destroy locator connection list
 */
static inline
int dbBE_Redis_locator_connection_list_destroy( dbBE_Redis_locator_connection_list_t *lcl )
{
  int rc = dbBE_Redis_locator_connection_list_reset( lcl );

  if( lcl == NULL )
    return -EINVAL;

  memset( lcl, 0, sizeof( dbBE_Redis_locator_connection_list_t ) );
  free( lcl );

  return rc;
}

/*
 * append an index entry to the end of the connection list
 * and extends the list size if needed
 */
static inline
int dbBE_Redis_locator_connection_list_append( dbBE_Redis_locator_connection_list_t *lcl,
                                               dbBE_Redis_connection_index_t index )
{
  if(( lcl == NULL ) || ( index > DBBE_REDIS_MAX_CONNECTIONS ))
    return -EINVAL;

  int n;
  for( n = 0; n < lcl->_active; ++n )
  {
    if( lcl->_connections[ n ] == index )
      return -EALREADY;
  }

  if( lcl->_active < lcl->_size )
  {
    lcl->_connections[ lcl->_size ] = index;
    ++lcl->_active;
  }
  else
  {
    dbBE_Redis_connection_index_t *tmp = lcl->_connections;
    lcl->_size += DBBE_REDIS_CONN_LIST_SIZE_GRANULARITY;
    lcl->_connections = (dbBE_Redis_connection_index_t*)malloc( lcl->_size * sizeof( dbBE_Redis_connection_index_t ) );
    if( lcl->_connections == NULL )
      return -ENOMEM;

    memcpy( (void*)lcl->_connections,
            (void*)tmp,
            lcl->_active * sizeof( dbBE_Redis_connection_index_t ) );
    for( n = lcl->_active; n < lcl->_size; ++n )
      lcl->_connections[ n ] = DBBE_REDIS_CONNECTION_INDEX_INVALID;
  }

  lcl->_connections[ lcl->_active ] = index;
  ++lcl->_active;

  return 0;
}

/*
 * remove an entry from the list
 */
static inline
int dbBE_Redis_locator_connection_list_remove( dbBE_Redis_locator_connection_list_t *lcl,
                                               dbBE_Redis_connection_index_t index )
{
  if(( lcl == NULL ) || ( index > DBBE_REDIS_MAX_CONNECTIONS ))
    return -EINVAL;

  dbBE_Redis_connection_index_t *tmp = lcl->_connections;
  int shift = 0;
  int n;
  for( n = 0; n < lcl->_active; ++n )
  {
    if( lcl->_connections[ n ] == index )
      shift = 1;
    lcl->_connections[ n ] = lcl->_connections[ n + shift ];
  }
  if( shift == 0 )
    return -ENOENT;

  return 0;
}


/*
 * return the size of the allocated list
 */
static inline
int dbBE_Redis_locator_connection_list_get_size( dbBE_Redis_locator_connection_list_t *lcl )
{
  return lcl != NULL ? lcl->_size : 0;
}

/*
 * return the number of entries in the the allocated list
 */
static inline
int dbBE_Redis_locator_connection_list_get_active( dbBE_Redis_locator_connection_list_t *lcl )
{
  return lcl != NULL ? lcl->_active : 0;
}

/*
 * return the first connection index entry of the list
 */
static inline
dbBE_Redis_connection_index_t dbBE_Redis_locator_connection_list_get_first( dbBE_Redis_locator_connection_list_t *lcl )
{
  return lcl != NULL ? lcl->_connections[ 0 ] : DBBE_REDIS_CONNECTION_INDEX_INVALID;
}


#endif /* BACKEND_REDIS_LOCATOR_CONN_LIST_H_ */

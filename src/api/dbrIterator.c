/*
 * Copyright © 2019 IBM Corporation
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

#include "errorcodes.h"
#include "libdatabroker_int.h"

DBR_Iterator_t
libdbrIterator( DBR_Handle_t cs_handle,
                DBR_Iterator_t iterator,
                DBR_Group_t group,
                DBR_Tuple_template_t match_template,
                DBR_Tuple_name_t tuple_name )
{
  dbrName_space_t *cs = (dbrName_space_t*)cs_handle;

  if(( cs == NULL ) || ( cs->_reverse == NULL ) || ( cs->_status != dbrNS_STATUS_REFERENCED ) || ( tuple_name == NULL ))
    return NULL;

  if( cs->_be_ctx == NULL )
    return NULL;

  DBR_Tag_t tag = dbrTag_get( cs->_reverse );
  if( tag == DB_TAG_ERROR )
    BIGLOCK_UNLOCKRETURN( cs->_reverse, NULL );

  DBR_Errorcode_t rc = DBR_SUCCESS;

  dbrRequestContext_t *ctx = dbrCreate_request_ctx( DBBE_OPCODE_ITERATOR,
                                                    cs_handle,
                                                    group,
                                                    NULL,
                                                    DBR_GROUP_EMPTY,
                                                    0,
                                                    NULL,
                                                    (int64_t*)&iterator,
                                                    tuple_name,
                                                    match_template,
                                                    tag );
  if( ctx == NULL )
  {
    rc = DBR_ERR_NOMEMORY;
    goto error;
  }

  if( dbrInsert_request( cs, ctx ) == DB_TAG_ERROR )
  {
    rc = DBR_ERR_TAGERROR;
    goto error;
  }

  DBR_Request_handle_t req_handle = dbrPost_request( ctx );
  if( req_handle == NULL )
  {
    rc = DBR_ERR_BE_POST;
    goto error;
  }

  rc = dbrWait_request( cs, req_handle, 0 );
  switch( rc ) {
  case DBR_SUCCESS:
    rc = dbrCheck_response( ctx );
    break;
  case DBR_ERR_INPROGRESS:
    break;
  default:
    goto error;
  }

  dbrRemove_request( cs, ctx );
  BIGLOCK_UNLOCKRETURN( cs->_reverse, iterator );

error:
  tuple_name[0] = '\0';
  BIGLOCK_UNLOCKRETURN( cs->_reverse, NULL );
}
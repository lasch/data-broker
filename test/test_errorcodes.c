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

#include <string.h>

#include "logutil.h"
#include "test_utils.h"
#include "errorcodes.h"


#define TEST_EC( A, B, E ) TEST( A, B ); ++(E)

int main( int argc, char ** argv )
{
  int rc = 0;
  int ec = 0; // count the error codes to check whether any newly introduced errors are covered

  rc += TEST_EC( strcmp( dbrGet_error( DBR_SUCCESS ), "Operation successful"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_GENERIC ), "A general or unknown error has occurred"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_INVALID ),  "Invalid argument"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_HANDLE ), "An invalid handle was encountered" ), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_INPROGRESS ),  "Operation in progress"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_TIMEOUT ), "Operation timed out"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_UBUFFER ), "Provided user buffer problem (too small, not available)" ), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_UNAVAIL ),  "Entry not available"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_EXISTS ),  "Entry already exists"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_NSBUSY ), "Namespace still referenced by a client"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_NSINVAL ), "Namespace is invalid"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_NOMEMORY ),  "Insufficient memory or storage"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_TAGERROR ), "Invalid tag"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_NOFILE ), "File not found"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_NOAUTH ), "Access authorization required or failed"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_NOCONNECT ), "Connection to a storage backend failed"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_CANCELLED ), "Operation was cancelled"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_NOTIMPL ), "Operation not implemented"), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_INVALIDOP ), "Invalid operation" ), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_BE_POST ), "Failed to post request to back-end" ), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_BE_PROTO ), "A protocol error in the back-end was detected" ), 0, ec );
  rc += TEST_EC( strcmp( dbrGet_error( DBR_ERR_BE_GENERAL ), "Unspecified back-end error" ), 0, ec );


  rc += TEST( strcmp( dbrGet_error( -1 ), "Unknown Error" ), 0 );
  rc += TEST( strcmp( dbrGet_error( DBR_ERR_MAXERROR ), "Unknown Error" ), 0 );
  rc += TEST( strcmp( dbrGet_error( 10532 ), "Unknown Error" ), 0 );

  rc += TEST( DBR_ERR_MAXERROR, ec );

  printf( "Test exiting with rc=%d\n", rc );
  return rc;
}

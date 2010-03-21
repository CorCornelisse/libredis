/*
* Copyright (c) 2007, Last.fm, All rights reserved.
* Richard Jones <rj@last.fm>
* Christian Muehlhaeuser <chris@last.fm>
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of the Last.fm Limited nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY Last.fm ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Last.fm BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "ketama.h"
#include "md5.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>           /* floor & floorf                       */
#include <string.h>

#include "common.h"
#include "list.h"

typedef struct
{
    unsigned int point;  // point on circle
    char ip[22];
} mcs;

typedef struct
{
	struct list_head list;

    char addr[22];
    unsigned long memory;
} serverinfo;

struct _Ketama
{
    int numpoints;
    unsigned int numservers;
    unsigned long memory;
    mcs* continuum; //array of mcs structs
    struct list_head servers;
};

typedef int (*compfn)( const void*, const void* );
int ketama_compare( mcs *a, mcs *b );


Ketama *Ketama_new()
{
	Ketama *ketama = Alloc_alloc_T(Ketama);
	ketama->numpoints = 0;
	ketama->numservers = 0;
	ketama->memory = 0;
	ketama->continuum = NULL;
	INIT_LIST_HEAD(&ketama->servers);
	return ketama;
}

void Ketama_free(Ketama *ketama)
{
	Alloc_free_T(ketama, Ketama);
}

void Ketama_add_server(Ketama *ketama, const char *addr, int port, unsigned long weight)
{
	serverinfo *info = Alloc_alloc_T(serverinfo);
	sprintf(info->addr, "%s:%d", addr, port);
	info->memory = weight;
	list_add(&info->list, &ketama->servers);
	ketama->numservers += 1;
	ketama->memory += weight;
}

/** \brief Hashing function, converting a string to an unsigned int by using MD5.
  * \param inString The string that you want to hash.
  * \return The resulting hash. */
//unsigned int ketama_hashi( char* inString, size_t inLen );

/** \brief Hashinf function to 16 bytes char array using MD%.
 * \param inString The string that you want to hash.
 * \param md5pword The resulting hash. */
//void ketama_md5_digest( char* inString, size_t inLen, unsigned char md5pword[16] );

/* ketama.h does not expose this function */
void ketama_md5_digest( char* inString, size_t inLen, unsigned char md5pword[16] )
{
    md5_state_t md5state;

    md5_init( &md5state );
    md5_append( &md5state, (unsigned char *)inString, inLen);
    md5_finish( &md5state, md5pword );
}

unsigned int
ketama_hashi( char* inString, size_t inLen )
{
    unsigned char digest[16];

    ketama_md5_digest( inString, inLen, digest );
    return (unsigned int)(( digest[3] << 24 )
                        | ( digest[2] << 16 )
                        | ( digest[1] <<  8 )
                        |   digest[0] );
}


int Ketama_get_server(Ketama *ketama, char* key, size_t key_len)
{
    unsigned int h = ketama_hashi( key, key_len );
    int highp = ketama->numpoints;
    mcs (*mcsarr)[ketama->numpoints] = ketama->continuum;
    int lowp = 0, midp;
    unsigned int midval, midval1;

    // divide and conquer array search to find server with next biggest
    // point after what this key hashes to
    while ( 1 )
    {
        midp = (int)( ( lowp+highp ) / 2 );

        if ( midp == ketama->numpoints )
            return &( (*mcsarr)[0] ); // if at the end, roll back to zeroth

        midval = (*mcsarr)[midp].point;
        midval1 = midp == 0 ? 0 : (*mcsarr)[midp-1].point;

        if ( h <= midval && h > midval1 )
            return &( (*mcsarr)[midp] );

        if ( midval < h )
            lowp = midp + 1;
        else
            highp = midp - 1;

        if ( lowp > highp )
            return &( (*mcsarr)[0] );
    }
}


/** \brief Generates the continuum of servers (each server as many points on a circle).
  * \param key Shared memory key for storing the newly created continuum.
  * \param filename Server definition file, which will be parsed to create this continuum.
  * \return 0 on failure, 1 on success. */
void Ketama_create_continuum(Ketama *ketama)
{
	assert(ketama->numservers > 0);

	DEBUG(("Server definitions read: %u servers, total memory: %lu.\n", ketama->numservers, ketama->memory ));

    /* Continuum will hold one mcs for each point on the circle: */
    assert(ketama->continuum == NULL);
    ketama->continuum = Alloc_alloc(ketama->numservers * sizeof(mcs));
    unsigned int i = 0, k, cont = 0;

    serverinfo *sinfo;
    list_for_each_entry(sinfo, &ketama->servers, list)
    {
        float pct = (float)sinfo->memory / (float)ketama->memory;
        unsigned int ks = floorf( pct * 40.0 * (float)ketama->numservers );
#ifndef NDEBUG
        int hpct = floorf( pct * 100.0 );
        DEBUG(("Server no. %d: %s (mem: %lu = %u%% or %d of %d)\n", i, sinfo->addr, sinfo->memory, hpct, ks, ketama->numservers * 40 ));
#endif

        for( k = 0; k < ks; k++ )
        {
            /* 40 hashes, 4 numbers per hash = 160 points per server */
            char ss[30];
            unsigned char digest[16];

            int len = sprintf( ss, "%s-%d", sinfo->addr, k );
            ketama_md5_digest( ss, len, digest );

            /* Use successive 4-bytes from hash as numbers 
             * for the points on the circle: */
            int h;
            for( h = 0; h < 4; h++ )
            {
                ketama->continuum[cont].point = ( digest[3+h*4] << 24 )
                                      | ( digest[2+h*4] << 16 )
                                      | ( digest[1+h*4] <<  8 )
                                      |   digest[h*4];

                memcpy( ketama->continuum[cont].ip, sinfo->addr, 22 );
                cont++;
            }
        }
        i++;
    }


    //free( slist );

    /* Sorts in ascending order of "point" */
    //qsort( (void*) &ketama->continuum, cont, sizeof( mcs ), (compfn)ketama_compare );

}

/*
void
ketama_print_continuum( ketama_continuum cont )
{
    int a;
    printf( "Numpoints in continuum: %d\n", cont->numpoints );

    if ( cont->array == 0 )
    {
        printf( "Continuum empty\n" );
    }
    else
    {
        mcs (*mcsarr)[cont->numpoints] = cont->array;
        for( a = 0; a < cont->numpoints; a++ )
        {
            printf( "%s (%u)\n", (*mcsarr)[a].ip, (*mcsarr)[a].point );
        }
    }
}
*/

int ketama_compare( mcs *a, mcs *b )
{
    return ( a->point < b->point ) ?  -1 : ( ( a->point > b->point ) ? 1 : 0 );
}

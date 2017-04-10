//
// A concurrent striped hash table
// We use quadratic probing 
//

#ifndef __STRIPED_HASH_H__
#define __STRIPED_HASH_H__

#include "stripedhash.h"

template <>
int stripedhashcounter<std::string>::hash_func( const std::string& s ) const {
    int ret = 0;
    for ( const auto it : s ) {
        ret = ( ret * ha + it ) % hp;
    }
    return ret;
}
#endif

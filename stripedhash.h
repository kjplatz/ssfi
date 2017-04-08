//
// A concurrent striped hash table
// We use quadratic probing 
//

#ifndef __STRIPED_HASH_H__
#define __STRIPED_HASH_H__

#include <cmath>
#include <mutex>
#include <utility> // For std::pair
#include <vector> 

template <typename K, typename D> 
class stripedhash {
    #ifdef HASH_DEFAULT_SIZE
    // Initial size of hash table
    static constexpr int DEFAULT_SIZE=HASH_DEFAULT_SIZE; 
    #else
    static constexpr int DEFAULT_SIZE=8;
    #endif

    #ifdef HASH_DEFAULT_MC
    static constexpr int DEFAULT_MC=HASH_DEFAULT_MC; // Maximum number of collisions before resizing
    #else
    static constexpr int DEFAULT_MC=8;
    #endif

    typedef std::pair<K,D> element;

    std::vector<element*>   table;
    std::vector<std::mutex> locks;
    int hash_func( const K&, int h );
    const K sentinel;
public:
    stripedhash( const K& s, int size=DEFAULT_SIZE, int mc=DEFAULT_MC, int g=DEFAULT_GRANULARITY ) :
        table(nullptr, size), sentinel(s), maxcollisions(mc) {
        locks.resize( sqrt(size) + 1 );
    }

    D contains( const K& key ) const {
    }

    int insert( const K& key, const D& data ) {
    }

    int remove( const K& key ) {
    }

    D& operator[]( const K& key ) {
    }
}
#endif


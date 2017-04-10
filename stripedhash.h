//
// A concurrent striped hash table
// We use quadratic probing here, since it's fast and generally gives good performance
//

#ifndef __STRIPED_HASH_H__
#define __STRIPED_HASH_H__

#include <chrono>
#include <cmath>
#include <mutex>
#include <random>
#include <string>
#include <utility> // For std::pair
#include <vector> 


extern int debug;

template <typename K>
class stripedhashcounter {
    #ifdef HASH_DEFAULT_SIZE
    // Initial size of hash table
    static constexpr int DEFAULT_SIZE=HASH_DEFAULT_SIZE; 
    #else
    static constexpr int DEFAULT_SIZE=8;             // Initial size of hash table
    #endif

    #ifdef HASH_DEFAULT_MC
    static constexpr int DEFAULT_MC=HASH_DEFAULT_MC; // Maximum number of collisions before resizing
    #else
    static constexpr int DEFAULT_MC=8;
    #endif

    typedef std::pair<K,int> element;
    element sentinel;                // Value of this guy doesn't matter, just his address...

    class config {
    public:
        std::vector<element*> table;
        std::vector<element*> deleted;
        std::vector<std::mutex> locks;
        int ha, hb, hp;
        bool resizing;

        config( int sz ) : resizing(false), table(sz, nullptr) {
            locks.resize( sqrt(sz) + 1 );
        }

        ~config() {
            delete table;
            delete locks;
            for( auto it = deleted->begin(); it != deleted->end(); ++it ) {
                delete *it;
                *it = nullptr;
            }
            delete deleted;
        }
    };


    std::shared_ptr<config> cfg, old_cfg;

    int maxcollisions;
    int hash_func( const K&, int ha, int hp ) const;

public:
    stripedhashcounter( int size=DEFAULT_SIZE, int mc=DEFAULT_MC ) :
        cfg(new config(size)), old_cfg(nullptr), maxcollisions(mc) {
    }

    int contains( const K& key ) const {
        std::shared_ptr<config> current = cfg;

        int slot = hash_func( key, cfg->ha, cfg->hp );
        for( int i=0; i<maxcollisions; ++i ) {
            slot = (slot + i * i) % cfg->table->size();
            element *e = current->table->at(slot);
            if ( e == nullptr ) return 0;
            if ( e->first == key ) {
                return e->second.load();
            }
        }
        return 0;
    }

    bool insert( const K& key ) {
        int slot;
        // Repeat forever.
        // In the event of concurrent resizings we may need to try over and over
        while( true ) {
            std::shared_ptr<config> current = cfg;
            slot = hash_func( key, cfg->ha, cfg->hp );
            for( int i=0; i<maxcollisions; ++i ) {
                slot = (slot + i * i) % cfg->table->size();
                element *e = current->table->at(slot);
                if ( e == nullptr || e == &sentinel ) {
                    std::lock_guard<std::mutex> lg( 
                }
            }
        }
        return true;
    }

    int remove( const K& key ) {
        return true;
    }

    int increment( const K& key ) {
        return 0;
    }
};

template <>
int stripedhashcounter<int>::hash_func( const int& i, int ha, int hp ) const {
    return ha * i % hp;
}
template <>
int stripedhashcounter<std::string>::hash_func( const std::string& s, int ha, int hp ) const {
    int ret = 0;
    for ( const auto it : s ) {
        ret = ( ret * ha + it ) % hp;
    }
    return ret;
}
#endif

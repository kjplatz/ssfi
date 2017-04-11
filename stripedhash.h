//
// A concurrent striped hash table
//    This uses a finite number of locks to distribute the 
// We use quadratic probing here, since it's fast and generally gives good performance
//
//
// We can control behaviour of this with the following macros:
//    HASH_DEFAULT_SIZE:  Initial size of hash table
//    HASH_DEFAULT_MC:    Default maximum number of collisions before resizing
//    HASH_DONT_STRIPE:   Do not use lock striping.  One lock to rule the whole hash table
//    

#ifndef __STRIPED_HASH_H__
#define __STRIPED_HASH_H__

#include <algorithm>
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
    static constexpr int DEFAULT_SIZE=32;             // Initial size of hash table
    #endif

    #ifdef HASH_DEFAULT_MC
    static constexpr int DEFAULT_MC=HASH_DEFAULT_MC; // Maximum number of collisions before resizing
    #else
    static constexpr int DEFAULT_MC=8;
    #endif

    #ifndef HASH_NO_STRIPE
    # ifdef HASH_STRIPE_SIZE
    static constexpr int STRIPE_SIZE=HASH_STRIPE_SIZE;
    # else
    static constexpr int STRIPE_SIZE=DEFAULT_SIZE;
    # endif
    #else
    static constexpr int STRIPE_SIZE=1;
    #endif

    typedef std::pair<K,int> element;

    class config {
    public:
        std::vector<element*> table;
        std::vector<element*> deleted;
        bool resizing;

        config( int sz ) : table(sz, nullptr), resizing(false) {
            debug && std::cout << "Calling constructor for config(" << sz << ")" << std::endl;
        }

        ~config() {
            for( auto it : deleted ) {
                delete it;
                it = nullptr;
            }
        }
        int size() const { return table.size(); }
    };

    std::shared_ptr<config> cfg, old_cfg;

    int maxcollisions;
    int hash_func( const K&, int sz ) const;
    
    std::mutex mtx[STRIPE_SIZE];

    // Acquire one lock associated with a slot
    std::unique_lock<std::mutex>&& acquire( int x ) {
        return std::move( std::unique_lock<std::mutex>( mtx[x % STRIPE_SIZE] ));
    }

    // Get ALL the locks (ie, for a resize)
    void acquire_all( std::unique_lock<std::mutex> l_arr[]) {
        for( int i=0; i<STRIPE_SIZE; ++i ) {
            l_arr[i] = std::unique_lock<std::mutex>( mtx[i] );
        }
    }

    config* resize_helper( int size ) {
        config* newcfg = new config(size);
        debug && std::cout << "Config (" << size << ") created." << std::endl;
        for( auto it : cfg->table ) {
            if ( it == nullptr || it->second < 0 ) continue;
            int base = hash_func( it->first, size );
            int i;
            for( i=0; i<maxcollisions; ++i ) {
                int slot = (base + i * i) % size;
                if ( newcfg->table[slot] == nullptr ) {
                    newcfg->table[slot] = it;
                    break;
                }
            }
            // Did we hav maxcollisions collisions?
            if ( i == maxcollisions ) {
                delete newcfg;
                return nullptr;
            }
        }
        return newcfg;
    }

    void resize() {
        std::shared_ptr<config> current = cfg;
        debug && std::cout << "Resizing. Creating lock vector" << std::endl;
        std::unique_lock<std::mutex>l_arr[STRIPE_SIZE];
        acquire_all(l_arr);

        // Did someone already resize the table?
        if ( cfg->size() != current->size() ) return;

        debug && std::cout << "Resizing..." << std::endl;

        int size = current->size();
        while( true ) {
            size *= 2; 
            auto newcfg = resize_helper( size );
            if ( newcfg != nullptr ) {
                cfg = std::shared_ptr<config>(newcfg);
                return;
            }     
        }
        debug && std::cout << "Done resizing..." << std::endl;
    }

public:
    stripedhashcounter( int size=DEFAULT_SIZE, int mc=DEFAULT_MC ) :
        cfg(new config(size)), old_cfg(nullptr), maxcollisions(mc) {
    }

    int contains( const K& key ) const {
        std::shared_ptr<config> current = cfg;

        int base = hash_func(key, current->table.size());
        for( int i=0; i<maxcollisions; ++i ) {
            int slot = (base + i * i) % current->table.size();
            if ( cfg->table.at(slot) == nullptr ) return 0;
            if ( cfg->table.at(slot)->first == key ) {
                return cfg->table.at(slot)->second;
            }
        }
        return 0;
    }

    int increment( const K& key ) {
        hash_insert_retry:
        while(true) {
            // Get the current configuration
            std::shared_ptr<config> current = cfg;
            int base = hash_func( key, current->table.size() );
            for( int i=0; i<maxcollisions; ++i ) {
                int slot = (base + i*i) % current->table.size();
                // Get the lock associated with our slot
                std::unique_lock<std::mutex> lock = acquire( slot );
                // Did someone resize the table?
                // If so we need to start at the beginning.
                if ( current != cfg ) goto hash_insert_retry;
                element* e = current->table.at(slot);
                if ( e == nullptr || e->second < 0 ) {
                    current->table.at(slot) = new element{ key, 1 };
                    return 1;
                } else if ( e->first == key ) {
                    e->second++;
                    return e->second;
                } 
            }
            debug && std::cout << "Gotta resize!" << std::endl;
            resize();
        }    
        return 0;
    }

    bool insert( const K& key ) {
        return (increment(key) == 1);
    }

    // There are currently some issues with paired inserts/removes.  Fortunately we don't need
    // to use remove for this application.
    int remove( const K& key ) {
        hash_remove_retry:
        while(true) {
            //Get the current configuration
            std::shared_ptr<config> current = cfg;
            int base = hash_func( key, current->table.size() );
            for( int i=0; i<maxcollisions; ++i ) {
                int slot = (base + i*i) % current->table.size();
                // Get the lock associated with our slot
                std::unique_lock<std::mutex> lock = acquire( slot );
                // Did someone resize the table?
                // If so we need to start at the beginning.
                if ( current != cfg ) goto hash_remove_retry;
 
                element* e = current->table.at(slot);
                if ( e == nullptr || e->second < 0  ) { 
                    return false;
                } else if ( e->first == key ) {
                    // Mark this as deleted
                    // and replace this value with the "sentinel" value.
                    e->second = -1;
                    current->deleted.push_back( e );
                    return true;
                } 
            }
            // Well, we got too many collisions.  it's not here.
            return false;
        }    
        return false;
    }

    static int pairless( const element& a, const element& b ) {
        return a.second < b.second ||  
               (a.second == b.second && a.first < b.first );
    }
    static int pairmore( const element& a, const element& b ) {
        return a.second > b.second ||  
               (a.second == b.second && a.first > b.first );
    }


    std::vector<std::pair<K,int>> get_top( unsigned count ) {
        std::vector<std::pair<K,int>> ret{ element{"", 0} };
        std::unique_lock<std::mutex> l_arr[STRIPE_SIZE];
        acquire_all(l_arr);

        for( auto it : cfg->table ) {
            if ( it == nullptr || it->second < 0 ) continue;
            if ( ret.size() >= count ) {
                if ( pairless( *it, ret.front() )) continue;
                std::pop_heap( ret.begin(), ret.end(), pairmore );
                ret.pop_back();
            }
            ret.push_back( *it );
            std::push_heap( ret.begin(), ret.end(), pairmore );
        }
        return ret;
    }

};

template <>
int stripedhashcounter<int>::hash_func( const int& i, int sz ) const {
    return i % sz;
}

template <>
int stripedhashcounter<std::string>::hash_func( const std::string& s, int sz ) const {
    int ret = 0;
    for ( const auto it : s ) {
        ret = ( ret * 31 + it + 3 ) % sz;
    }
    return ret;
}
#endif

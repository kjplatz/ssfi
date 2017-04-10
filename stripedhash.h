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
public:
    typedef std::pair<K,int> element;
private:
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

    element sentinel;                // Value of this guy doesn't matter, just his address...

    class config {
    public:
        std::vector<element*> table;
        std::vector<element*> deleted;
        std::vector<std::mutex> locks;
        int ha, hb, hp;
        bool resizing;

        config( int sz, std::default_random_engine g ) : table(sz, nullptr), locks( sqrt(sz) + 1), resizing(false) {
            std::uniform_int_distribution<int> dist(1,sz-1);
            ha = dist(g);
            hp = 2 * sz + 1;
        }

        ~config() {
            for( auto it = deleted.begin(); it != deleted.end(); ++it ) {
                delete *it;
                *it = nullptr;
            }
        }
    };


    std::shared_ptr<config> cfg, oldcfg;

    int maxcollisions;
    std::default_random_engine generator;
    int hash_func( const K&, int ha, int hp ) const;

    config* resize_helper( std::shared_ptr<config> current, int newsz ) {
        config* newcfg = new config(newsz, generator);

        // Since this newcfg is private to our thread (for now),
        // we don't need to worry about locking anything when we insert
        for( auto it : current->table ) {
            if ( it == nullptr || it == &sentinel ) continue;
            int slot = hash_func( it->first, current->ha, current->hp );
            int i;
            for( i=0; i<maxcollisions; ++i ) {
                slot = (slot + (i * i)) % newcfg->table.size();
                // Only need to check for nullptr here
                // There is no chance that someone deleted an entry.
                if ( newcfg->table[slot] == nullptr ) {
                    newcfg->table[slot] = it;
                }
            }
            // Dangit.  Too many collisions.  Try increasing size by 50%
            if ( i == maxcollisions ) {
                delete newcfg;
                return nullptr;
            }
        }
        return newcfg;
    };

    void resize( std::shared_ptr<config> old ) {
        std::shared_ptr<config> current = old;
        int newsz = old->table.size();
        
        // Check to see if someone is resizing...
        // Someone already 
        if ( old->resizing ) return;

        // Need to use unique_lock here, because they are move-assignable.
        // lock_guards are not.
        std::vector<std::unique_lock<std::mutex>> lgs;
        for( unsigned i=0; i<current->locks.size(); ++i ) {
            lgs.emplace_back( current->locks[i] );
        }
        if ( old->resizing ) return;
        config* newcfg;
      
        do {
            newsz *= 2;
            newcfg = resize_helper( old, newsz );
        } while( newcfg == nullptr );
       
        oldcfg = old;
        cfg = std::shared_ptr<config>{ newcfg };
    };
public:
    stripedhashcounter( int size=DEFAULT_SIZE, int mc=DEFAULT_MC ) :
        oldcfg(nullptr), maxcollisions(mc) {
        generator.seed( std::chrono::system_clock::now().time_since_epoch().count() );
        cfg = std::make_shared<config>( size, generator );
    }

    // Contains method:
    //     Returns an integer value if key exists in the hash table.  Specifically, it returns the
    //     count of key's occurrences.  
    //     As this is a concurrent data structure, this value returns a value that is correct at some
    //     point during its execution.
    int contains( const K& key ) const {
        std::shared_ptr<config> current = cfg;

        int slot = hash_func( key, current->ha, current->hp );
        for( int i=0; i<maxcollisions; ++i ) {
            slot = (slot + i * i) % current->table.size();
            element *e = current->table.at(slot);
            if ( e == nullptr ) return 0;
            if ( e->first == key ) {
                return e->second.load();
            }
        }
        return 0;
    }

    int insert( const K& key ) {
        // Repeat forever.
        // In the event of concurrent resizings we may need to try over and over
        retry:
        while( true ) {
            std::shared_ptr<config> current = cfg;
            int base = hash_func( key, current->ha, current->hp );
            for( int i=0; i<maxcollisions; ++i ) {
                // Use quadratic probing here.  This is easy to implement and generally
                // gives a good spread among the slots
                int slot = (base + i * i) % current->table.size();
                std::lock_guard<std::mutex> lg( current->locks[slot % current->locks.size()] );
                // Check if the table is getting resized...
                // Use goto here because we need to break out of two loops.
                if ( current->resizing ) goto retry;
                element *e = current->table.at(slot);

                // Is the slot free?
                if ( e == nullptr || e == &sentinel ) {
                    current->table.at( slot ) = new element( key, 1 );
                    return 1;
                }

                // Does it match our key?  If so, go ahead and increment
                if ( e->first == key ) {
                    e->second += 1;
                    return e->second;
                }
            }
            // We got maxcollisions collisions, resize the table
            resize(cfg);
        }
        return true;
    }

    int remove( const K& key ) {
        retry:
        while( true ) {
            std::shared_ptr<config> current = cfg;
            int base = hash_func( key, current->ha, current->hp );
            for( int i=0; i<maxcollisions; ++i ) {
                int slot = (base + i * i) % current->table.size();
                std::lock_guard<std::mutex> lg( current->locks[slot % current->locks.size()] );
                if ( current->resizing ) goto retry;
                element *e = current->table.at(slot);

                // Is the slot free?
                if ( e == nullptr || e == &sentinel ) {
                    return 0;
                }

                // Does it match our key?  If so, replace with the deleted sentinel value
                if ( e->first == key ) {
                    current->table.at( slot ) = &sentinel;
                    current->deleted.push_back( e );        // And add to the list to be garbage collected
                    return e->second;
                }
  
            }
        }

        return 0;
    }

    int increment( const K& key ) { return insert(key); }

    std::vector<element>& extract_top( int count ) {
        std::vector<element> *heap = new std::vector<element>{ sentinel };
        std::vector<element> ondeck{ sentinel };
        std::vector<std::unique_lock<std::mutex>> lgs;
        std::shared_ptr<config> current = cfg;

        // Need exclusive access...
        for( unsigned i=0; i<current->locks.size(); ++i ) {
            lgs.emplace_back( current->locks[i] );
        }

        for( auto it : current->table ) {
            if ( it == nullptr || it == &sentinel ) continue;

            if ( it->second > ondeck.at(0).second ) {
                heap->push_back( *it );
                std::push_heap( heap->begin(), heap->end(), 
                                []( const element& e1, const element& e2 ) {
                                    return ( e1.second > e2.second );
                                });
                if ( heap->size() >= (unsigned long)count ) {
                    ondeck.clear();
                    int next = heap->begin()->second;
                    while( heap->begin()->second == next ) {
                        std::pop_heap( heap->begin(), heap->end(), 
                                []( const element& e1, const element& e2 ) {
                                    return ( e1.second > e2.second );
                                });
                        ondeck.push_back( heap->back() );
                        heap->pop_back();
                    }
                }
            } else if ( it->second == ondeck.at(0).second ) {
                ondeck.emplace_back( *it );
            }  
        }
        heap->insert( heap->end(), ondeck.begin(), ondeck.end() );
        std::sort( heap->begin(), heap->end(), 
                   [](const element& a, const element& b) { 
                       if ( a.second == b.second ) return a.first < b.first;
                       return a.second > b.second; 
                   } 
                 );
        return *heap;

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

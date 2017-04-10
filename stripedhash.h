//
// A concurrent striped hash table
// We use quadratic probing here, since it's fast and generally gives good performance
//

#ifndef __STRIPED_HASH_H__
#define __STRIPED_HASH_H__

#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <sstream>
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
    static constexpr int DEFAULT_SIZE=8;             // Initial size of hash table
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

        std::string to_string(const element* sentinel) const {
            std::ostringstream os;
            for( unsigned i=0; i<table.size(); ++i ) {
                if ( table.at(i) && table.at(i) != sentinel ) {
                    os << i << "    ";
                    os << table.at(i)->first << ":" << table.at(i)->second << " :: ";
                    os << std::endl;
                } 
            }
            return os.str();
        }
        int nelem(const element* sentinel) const {
            int i=0;
            for( auto it : table ) {
                if ( it != nullptr && it != sentinel ) ++i;
            }
            return i;
        }
    };


    std::shared_ptr<config> cfg, oldcfg;

    int maxcollisions;
    std::default_random_engine generator;
    int hash_func( const K&, int ha, int hp ) const;

    std::shared_ptr<config> resize_helper( std::shared_ptr<config> current, int newsz ) {
        std::shared_ptr<config> newcfg = std::make_shared<config>(newsz, generator);

        debug && std::cout << "Resize helper.  New size = " << newsz << std::endl;

        // Since this newcfg is private to our thread (for now),
        // we don't need to worry about locking anything when we insert
        for( auto it : current->table ) {
            if ( it == nullptr || it == &sentinel ) continue;
            int slot = hash_func( it->first, newcfg->ha, newcfg->hp );
            int i;
            for( i=0; i<maxcollisions; ++i ) {
                slot = (slot + (i * i)) % newcfg->table.size();
                // Only need to check for nullptr here
                // There is no chance that someone deleted an entry.
                if ( newcfg->table[slot] == nullptr ) {
                    newcfg->table[slot] = it;
                    break;
                } 
            }
            // Dangit.  Too many collisions.  Try increasing size by 50%
            if ( i == maxcollisions ) {
                return nullptr;
            }
        }
        if ( debug ) {
            std::cout << "Size of old table: " << current->nelem(&sentinel) << std::endl;
            std::cout << "Size of new table: " << newcfg->nelem(&sentinel) << std::endl;
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
        std::shared_ptr<config> newcfg;
      
        do {
            newsz *= 2;
            newcfg = resize_helper( old, newsz );
        } while( newcfg == nullptr );
       
        oldcfg = old;
        cfg = newcfg;
        if ( debug ) {
            std::cout << "<<< In resize() " << std::endl;
            std::cout << "Size of old table: " << oldcfg->nelem(&sentinel) << std::endl;
            std::cout << "Size of new table: " << cfg->nelem(&sentinel) << std::endl;

            std::cout << "==== OLD ====" << std::endl;
            std::cout << oldcfg->to_string(&sentinel) << std::endl;

            std::cout << "==== NEW ====" << std::endl;
            std::cout << newcfg->to_string(&sentinel) << std::endl;

            std::cout << "==== CURRENT ====" << std::endl;
            std::cout << cfg->to_string(&sentinel) << std::endl;
        }
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
                    debug && std::cout << "Inserting [" << key << "] at slot " << slot << std::endl;;
                    current->table.at( slot ) = new element( key, 1 );
                    return 1;
                } 

                // Does it match our key?  If so, go ahead and increment
                if ( e->first == key ) {
                    debug && std::cout << "Found [" << key << "] at slot " << slot << ".  Incrementing from " << e->second << " to " << (e->second + 1) << std::endl;
                    e->second += 1;
                    return e->second;
                }

                debug && std::cout << "Collision for [" << key << "] at slot " << slot << ".  (Found: " << e->first << ")" << std::endl;
              
            }
            // We got maxcollisions collisions, resize the table
            resize(cfg);
            debug && std::cout << "Had to resize for [" << key << "] will re-attempt." << std::endl;
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
        std::vector<element> *heap = new std::vector<element>{ count, sentinel };
        std::vector<std::unique_lock<std::mutex>> lgs;
        std::shared_ptr<config> current = cfg;

        // Need exclusive access...
        for( unsigned i=0; i<current->locks.size(); ++i ) {
            lgs.emplace_back( current->locks[i] );
        }

        for( auto it : current->table ) {
            if ( it == nullptr || it == &sentinel ||
                 it->second < heap->front().second ||
                 (it->second == heap->front().second &&
                  it->first > heap->front().first) ) continue;
            std::pop_heap( heap->begin(), heap->end(), 
                           []( const element& e1, const element& e2 ) {
                               return ( e1.second > e2.second ) || ( e1.second == e2.second && e1.first < e2.first);
                           });
            heap->pop_back();
       
            heap->push_back( *it );
            std::push_heap( heap->begin(), heap->end(), 
                            []( const element& e1, const element& e2 ) {
                               return ( e1.second > e2.second ) || ( e1.second == e2.second && e1.first < e2.first);
                            });
        } 
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

/*
 * hashcounter.h
 * A hash-based counter
 * Based upon a refinable striped cuckoo hash set
 *   by Herlihy, Shavit, and Tzafrir
 *   "Concurrent cuckoo hashing" 
 *   Technical report - Brown University, 2007
 *
 * Implemented by Kenneth Platz @kjplatz
 */
#ifndef CUCKOO_HASH_COUNTER

#include <array>
#include <atomic>
#include <mutex>
#include <random>
#include <vector>
#include <utility>
#include <boost/thread/recursive_mutex.hpp>

template <typename T>
class cuckooHashCounter {
public:
#ifdef CUCKOO_PROBE_SIZE
    static constexpr int PROBE_SIZE=CUCKOO_PROBE_SIZE;
#else
    static constexpr int PROBE_SIZE=4;
#endif
#ifdef CUCKOO_THRESHOLD
    static constexpr int THRESHOLD=CUCKOO_THRESHOLD;
#else
    static constexpr int THRESHOLD=PROBE_SIZE+4;
#endif
    typedef std::pair<T, int> itemtype;
private:
    std::vector<boost::recursive_mutex>    locks;
    std::vector<std::array<itemtype,THRESHOLD>> table;
    std::default_random_engine generator;
    
    volatile int capacity;
    int a[2], p[2]; // Hash function parameters

    int hashcode( const T& x, int y );

    void generateHashFuncs( int c ) {
        std::uniform_int_distribution<int> dist(1,c);

        p[0] = dist(generator) + c;
        while( p[0] != p[1] ) {
            p[1] = dist(generator) + c;
        }

        a[0] = dist(generator);
        while( a[0] != a[1] ) {
            a[1] = dist(generator);
        }
    }

    // Get both locks for element x
    void acquire( const T& x ) {
    }

public:
    cuckooHashCounter(int cap=8) : capacity(cap) {
        table.resize(2*capacity);
        generateHashFuncs(capacity);
    }

};

template <typename T>
constexpr int cuckooHashCounter<T>::THRESHOLD;
template <typename T>
constexpr int cuckooHashCounter<T>::PROBE_SIZE;
#endif

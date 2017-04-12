//
// ssfi - Super Simple File Indexer
// Author: Kenneth Platz @kjplatz 

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif // _GNU_SOURCE
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <regex>
#include <thread>

#include <ftw.h>
#include <getopt.h> // For getopt_long

#include "bdqueue.h"     // For lock-based bounded queue
#include "stripedhash.h" // for striped hash counter

using namespace std;
using kjp::unboundedQueue;

int debug = false;
unboundedQueue<string> bq;
#ifdef USE_MAP
#include <unordered_map>
unordered_map<string,int> word_map;
void worker_process_file( int mytid, const string& name, unordered_map<string,int>* );
void print_results( const unordered_map<string,int>&, unsigned );
#else
stripedhashcounter<string> word_map;
void worker_process_file( int mytid, const string& name );
void print_results( unsigned );
#endif
mutex donemtx;
condition_variable donecv;

void worker( int mytid, unboundedQueue<string>* q, atomic<int>* dc );
int nftw_process_file(const char *name, const struct stat *status, int type, struct FTW *fb);

void display_help( const char* fname, ostream& os ) {
    os << "Usage: " << basename(fname) << " -N <num> [-d] [-h]" << endl
       << "     -N <num> : Indicate number of worker threads" << endl
       << "     -c <num> : Number of entries to print (default 10)" << endl
       << "     -h       : Display this help and exit" << endl
       << "     -d       : Enable debugging" << endl;
}
int main( int argc, char** argv ) {
    int c;
    int option_index = 0;
    int nthreads = 0;
    int count = 10;
    atomic<int> donecount(0);

    struct option long_options[] = {
        { "nthreads", required_argument, 0, 'N' },
        { "count" , required_argument, 0, 'c' },
        { "debug", no_argument, &debug, 'd' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 } };

    // Process command-line options
    do {
        c = getopt_long( argc, argv, "N:dhc:", long_options, &option_index );
   
        switch(c) {
        case 'h' : display_help(argv[0], cout); return 0;
        case 'd' : debug = true; break;
        case 'N' : nthreads = atoi( optarg ); break;
        case 'c' : count = atoi( optarg ); break;
        case 0:
        case -1:   break;
 
        default : display_help(argv[0], cerr);
                  return 1;
        }

    } while ( c >= 0 );

    debug && cout << "Debugging enabled." << endl;
    debug && cout << "Number of threads: " << nthreads << endl;

    if ( nthreads <= 0 ) {
        cerr << "Error: Number of threads must be specified and greater than zero" << endl;
        return 1;
    }


    for( int i=0; i<nthreads; ++i ) {
        // thread t = new thread( worker, bq, ht, done );
        thread* t = new thread( worker, (i+1), &bq, &donecount );
        t->detach(); 
    }

    for( ; optind < argc; ++optind ) {
        if ( nftw( argv[optind], nftw_process_file, 32, 0 ) < 0 ) {
            cerr << "Error processing directory " << argv[optind] << ": "
                 << strerror( errno ) << endl;
        }
    }

    // Terminal value, let each worker thread know it's done
    for( int i=0; i<nthreads; ++i ) {
        bq.enq( string{""} );
    }

    // Sleep until everyone is done.
    // No need to busy-wait here
    while( donecount.load() < nthreads ) {
        unique_lock<mutex> lg( donemtx );
        donecv.wait( lg );
    }
   
#ifdef USE_MAP
    print_results( word_map, count );
#else
    print_results( count );
#endif
    return 0;
}

int nftw_process_file(const char *name, const struct stat *status, int type, struct FTW *fb) {
    if ( type != FTW_F ) return 0;

    int l = strlen(name);
    if ( l < 4 || 
         strcasecmp( name + l - 4, ".txt" ) != 0 ) {
        // debug && cout << name << ": does not end in .txt" << endl;
        return 0;
    }
    debug && cout << "Processing: " << name << endl;
    bq.enq( string{name} ); 
    
    return 0;
}

//int worker( int mytid, unboundedQueue<string>* q, atomic<int>* dc );
void worker( int mytid, unboundedQueue<string>* q, atomic<int>* done ) {
    #ifdef USE_MAP
    unordered_map<string,int> my_map;
    #endif

    string s;
    debug && cout << "[" << mytid << "]" << " Starting..." << endl;
    s = q->deq();
    while( s.length() > 0 ) {
        debug && cout << "[" << mytid << "]" << " Processing " << s << endl;
        
#ifdef USE_MAP
        worker_process_file( mytid, s, &my_map );
#else
        worker_process_file( mytid, s );
#endif

        s = q->deq();
    }

    debug && cout << "[" << mytid << "] Done processing..." << endl;
    unique_lock<mutex> dlg(donemtx);        
    
#ifdef USE_MAP
    for( auto it : my_map ) {
        int count = word_map[it.first] + it.second;
        word_map[it.first] = count;
        debug && cout << "[" << mytid << "] Updated global map [" << it.first << "] : " << count << endl;
    }
#endif
    done->fetch_add(1);
    donecv.notify_all();
    debug && cout << "[" << mytid << "] Done processing..." << endl;
}

#ifdef USE_MAP
void worker_process_file( int mytid, const string& name, unordered_map<string,int> *my_map ) {
#else
void worker_process_file( int mytid, const string& name ) {
#endif
    // Definition of a word == one or more alphanumeric characters
    static const regex re_word( "[[:alnum:]]+" );
    static const regex_iterator<string::iterator> re_end;

    ifstream infile( name );
    if ( !infile.good() ) {
        ostringstream os;
        os << "[" << mytid << "] Cannot open: " << name << ": " << strerror(errno) << endl;
        cout << os.str();
        return;
    }

    string line;
    // Prime the loop with the first line from the file
    getline( infile, line );
    while( infile.good() ) {
        int count;
        debug && cout << "[" << mytid << "] Got line: " << line << endl;

        // Create a regex iterator to extract words
        regex_iterator<string::iterator> rit( line.begin(), line.end(), re_word );
        while( rit != re_end ) {
            string word(std::move(rit->str()));
            // Convert word to lowercase
            transform( word.begin(), word.end(), word.begin(), ::tolower );
            // debug && cout << "[" << mytid << "] Got word: " << word << endl;
#ifdef USE_MAP
            // Increment word count
            count = (*my_map)[word] + 1;
            (*my_map)[word]=count;
#else
            count = word_map.increment(word);
#endif
            debug && cout << "[" << mytid << "] Got word: " << word << " : [" << count << "]" << endl;
            ++rit;
        }
        // Read the next line and... go!
        getline( infile, line );
    }
}

int pairless( pair<string,int> a, pair<string,int> b ) {
    return a.second < b.second || 
           (a.second == b.second && a.first < b.first );
}
int pairmore( pair<string,int> a, pair<string,int> b ) {
    return a.second > b.second || 
           (a.second == b.second && a.first > b.first );
}
int displaysort( pair<string,int> a, pair<string,int> b ) {
    return a.second > b.second || 
           (a.second == b.second && a.first < b.first );
}

#ifdef USE_MAP
void print_results( const unordered_map<string, int>& words, unsigned count ) {
    vector<pair<string,int>> vec;
    vec.push_back( make_pair("", 0) );

    for( auto it : words ) {
        if ( vec.size() >= count && pairless( it, vec.front() ) ) {
            debug && cout << "vec.size() = " << vec.size() 
                          << " it = " << it.first << ":" << it.second << " -- vec.front() = " 
                          << vec.front().first << vec.front().second << endl;
            continue;
        }

        if ( vec.size() >= count ) {
            pop_heap(vec.begin(), vec.end(), pairmore);
            vec.pop_back();
        }
        vec.push_back( it );
        push_heap( vec.begin(), vec.end(), pairmore);
    }
#else
void print_results( unsigned count ) {
    // vector<stripedhashcounter<string>::element> vec = word_map.get_top(count);
    vector<pair<string,int>> vec = word_map.get_top(count);
    sort( vec.begin(), vec.end(), displaysort );
#endif
    if ( debug ) {
        cout << "Results from heap..." << endl;
        for( auto it : vec ) {
            if ( !it.second ) continue;
            cout << it.first << " : " << it.second << endl;
        }
        cout << "Results after sorting..." << endl;
    }

    sort( vec.begin(), vec.end(), displaysort );
    for( auto it : vec ) {
        if ( !it.second ) continue;
        cout << it.first << " : " << it.second << endl;
    }
}

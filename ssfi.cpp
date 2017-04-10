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
#include "stripedhash.h" // for cuckooHashCounter

using namespace std;
using kjp::unboundedQueue;

int debug = false;
unboundedQueue<string> bq;
stripedhashcounter<string> hc;
mutex donemtx;
condition_variable donecv;

void worker( int mytid, unboundedQueue<string>* q, atomic<int>* dc );
int ntfw_process_file(const char *name, const struct stat *status, int type, struct FTW *fb);
void worker_process_file( int mytid, const string& name );

void display_help( const char* fname, ostream& os ) {
    os << "Usage: " << basename(fname) << " -N <num> [-d] [-h]" << endl
       << "     -N <num> : Indicate number of worker threads" << endl
       << "     -c <num> : Extract the top <num> frequently occurring words (default:10)" << endl
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
        { "debug", no_argument, &debug, 'd' },
        { "count", required_argument, &count, 'c' },
        { "nthreads", required_argument, 0, 'N' },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 } };

    do {
        c = getopt_long( argc, argv, "N:dhc:", long_options, &option_index );
   
        switch(c) {
        case 'h' : display_help(argv[0], cout); return 0;
        case 'd' : debug = true; break;
        case 'N' : nthreads = atoi( optarg ); break;
        case 0:
        case -1:   break;
 
        default : display_help(argv[0], cerr);
                  return 1;
        }

    } while ( c >= 0 );

    debug && cout << "Debugging enabled." << endl;
    debug && cout << "Number of threads: " << nthreads << endl;
    debug && cout << "Number of words: " << count << endl;

    if ( nthreads <= 0 ) {
        cerr << "Error: Number of threads must be specified and greater than zero" << endl;
        return 1;
    }


    // hashTable<string,int> ht;         // Hash table for handling entries

    for( int i=0; i<nthreads; ++i ) {
        // thread t = new thread( worker, bq, ht, done );
        thread* t = new thread( worker, (i+1), &bq, &donecount );
        t->detach(); 
    }

    for( ; optind < argc; ++optind ) {
        if ( nftw( argv[optind], ntfw_process_file, 32, 0 ) < 0 ) {
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
   
    std::vector<stripedhashcounter<string>::element> entries = hc.extract_top( count );
    for( auto it : entries ) {
        cout << it.first << " : " << it.second << endl;
    }
    return 0;
}

int ntfw_process_file(const char *name, const struct stat *status, int type, struct FTW *fb) {
    if ( type != FTW_F ) return 0;

    int l = strlen(name);
    if ( l < 4 ) {
        // debug && cout << name << ": too short, skipping" << endl; 
        return 0;
    }
    if ( strcasecmp( name + l - 4, ".txt" ) != 0 ) {
        // debug && cout << name << ": does not end in .txt" << endl;
        return 0;
    }
    debug && cout << "Processing: " << name << endl;
    bq.enq( string{name} ); 
    
    return 0;
}

//int worker( int mytid, unboundedQueue<string>* q, atomic<int>* dc );
void worker( int mytid, unboundedQueue<string>* q, atomic<int>* done ) {
    string s;
    debug && cout << "[" << mytid << "]" << " Starting..." << endl;
    s = q->deq();
    while( s.length() > 0 ) {
        debug && cout << "[" << mytid << "]" << " Processing " << s << endl;
        
        worker_process_file( mytid, s );
        s = q->deq();
    }
    done->fetch_add(1);
    unique_lock<mutex> dlg(donemtx);        
    donecv.notify_all();
}

void worker_process_file( int mytid, const string& name ) {
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
    // Prime the loop
    getline( infile, line );
    while( infile.good() ) {
        debug && cout << "[" << mytid << "] Got line: " << line << endl;
        regex_iterator<string::iterator> rit( line.begin(), line.end(), re_word );
        while( rit != re_end ) {
            string word(std::move(rit->str()));
            transform( word.begin(), word.end(), word.begin(), ::tolower );
            int count = hc.insert( word );
            debug && cout << "[" << mytid << "] Got word: " << word << "[" << count << "]" << endl;
            ++rit;
        }
        // Read the next line and... go!
        getline( infile, line );
    }
}

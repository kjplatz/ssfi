#include "hashcounter.h"

#include <stdexcept>
#include <string>
using namespace std;

template <>
int cuckooHashCounter<string>::hashcode( const string& x, int h ){
    if ( h < 0 || h > 1 ) throw logic_error( "Invalid call to hashcode" );
    int res=0;
    for ( const auto it : x ) {
        res += ( a[h] * it ) % p[h];        
    }

    return res;
}

//
// An unbounded Lock-Based Queue
// Based upon one by Scherer, Lea, and Scott
//    "Scalable Synchronous Queues" in PPoPP '06
//
// Implemented by Kenneth Platz @kjplatz
//
#ifndef __LFQUEUE__
#define __LFQUEUE__

#if __cplusplus < 201103L
#error "This program requires C++11 support"
#endif

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace kjp {

    template <typename T>
    class unboundedQueue {
        struct Node {
            T value;
            Node* next;

            Node() : next(nullptr) {}
            Node( const T& val ) : 
                value(val), next(nullptr) {}
            ~Node() {};
        };

        std::mutex enqMtx,                  // Lock for enqueueing
                   deqMtx;                  // Lock for dequeueing
        std::condition_variable notemptyCV; // CV to wait on for empty
        volatile Node *head, *tail;         // Head & tail of queue 
        std::atomic<int> size;              // # of entries in queue
    public:
        unboundedQueue() : size(0) {
            head = new Node;
            tail = head;
        }
        ~unboundedQueue() {
            auto e = head;
            while( head != nullptr ) {
                e = head->next;
                delete head;
                head = e;
            }
        }

        // Enqueue an item into the queue
        void enq( const T& item ) {
            bool mustWake = false;
            { 
                std::unique_lock<std::mutex> elg( enqMtx ); // Acquire enqueue lock

                Node* n = new Node(item);
                tail->next = n; 
                tail = n;

                // Might there be dequeuers waiting?
                if ( size.fetch_add(1) == 0 ) {
                    mustWake = true;
                }
            } // enqMtx gets released here...

            // Wake dequeuers if necessary
            if ( mustWake ) {
                std::unique_lock<std::mutex> dlg( deqMtx ); // Acquire dequeue lock
                notemptyCV.notify_all();
            }
        }

        // Dequeue an entry from the queue
        //   This will block if the queue is empty.
        T deq() {
            // Note: This must be a VALUE, not a reference.
            //   Otherwise we could run into ABA problems.
            T result;
            {
                std::unique_lock<std::mutex> dlg( deqMtx ); // Acquire dequeue lock

                // Check if queue is empty.  If so, wait on condition variable
                while( size.load() == 0 ) {
                    notemptyCV.wait(dlg);
                }
                Node* n = head->next;
                size.fetch_add(-1);
                result = std::move(n->value); 
                delete head;      // Old head is now unused, we can release it.
                head = n;         // Update to the next entry
            }
            return result;
        }
    };
}

#endif

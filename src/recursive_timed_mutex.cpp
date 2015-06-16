
//          Copyright Oliver Kowalke 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "boost/fiber/recursive_timed_mutex.hpp"

#include <algorithm>

#include <boost/assert.hpp>

#include "boost/fiber/fiber_manager.hpp"
#include "boost/fiber/interruption.hpp"
#include "boost/fiber/operations.hpp"

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_PREFIX
#endif

namespace boost {
namespace fibers {

bool
recursive_timed_mutex::lock_if_unlocked_() {
    if ( mutex_status::unlocked == state_) {
        state_ = mutex_status::locked;
        BOOST_ASSERT( ! owner_);
        owner_ = this_fiber::get_id();
        ++count_;
        return true;
    } else if ( this_fiber::get_id() == owner_) {
        ++count_;
        return true;
    }

    return false;
}

recursive_timed_mutex::recursive_timed_mutex() :
#if defined(BOOST_FIBERS_THREADSAFE)
    splk_(),
#endif
	state_( mutex_status::unlocked),
    owner_(),
    count_( 0),
    waiting_() {
}

recursive_timed_mutex::~recursive_timed_mutex() {
    BOOST_ASSERT( ! owner_);
    BOOST_ASSERT( 0 == count_);
    BOOST_ASSERT( waiting_.empty() );
}

void
recursive_timed_mutex::lock() {
    fiber_context * f( detail::scheduler::instance()->active() );
    BOOST_ASSERT( nullptr != f);
    for (;;) {
#if defined(BOOST_FIBERS_THREADSAFE)
        std::unique_lock< detail::spinlock > lk( splk_);
#endif

        if ( lock_if_unlocked_() ) {
            return;
        }

        // store this fiber in order to be notified later
        BOOST_ASSERT( waiting_.end() == std::find( waiting_.begin(), waiting_.end(), f) );
        waiting_.push_back( f);

#if defined(BOOST_FIBERS_THREADSAFE)
        // suspend this fiber
        detail::scheduler::instance()->wait( lk);
#else
        // suspend this fiber
        detail::scheduler::instance()->wait();
#endif
    }
}

bool
recursive_timed_mutex::try_lock() {
#if defined(BOOST_FIBERS_THREADSAFE)
    std::unique_lock< detail::spinlock > lk( splk_);
#endif

    if ( lock_if_unlocked_() ) {
        return true;
    }

#if defined(BOOST_FIBERS_THREADSAFE)
    lk.unlock();
#endif
    // let other fiber release the lock
    this_fiber::yield();
    return false;
}

bool
recursive_timed_mutex::try_lock_until( std::chrono::high_resolution_clock::time_point const& timeout_time) {
    fiber_context * f( detail::scheduler::instance()->active() );
    BOOST_ASSERT( nullptr != f);
    for (;;) {
#if defined(BOOST_FIBERS_THREADSAFE)
        std::unique_lock< detail::spinlock > lk( splk_);
#endif

        if ( std::chrono::high_resolution_clock::now() > timeout_time) {
            return false;
        }

        if ( lock_if_unlocked_() ) {
            return true;
        }

        // store this fiber in order to be notified later
        BOOST_ASSERT( waiting_.end() == std::find( waiting_.begin(), waiting_.end(), f) );
        waiting_.push_back( f);

#if defined(BOOST_FIBERS_THREADSAFE)
        // suspend this fiber until notified or timed-out
        if ( ! detail::scheduler::instance()->wait_until( timeout_time, lk) ) {
            lk.lock();
            std::deque< fiber_context * >::iterator i( std::find( waiting_.begin(), waiting_.end(), f) );
            if ( waiting_.end() != i) {
                // remove fiber from waiting-list
                waiting_.erase( i);
            }
            lk.unlock();
            return false;
        }
#else
        // suspend this fiber until notified or timed-out
        if ( ! detail::scheduler::instance()->wait_until( timeout_time) ) {
            std::deque< fiber_context * >::iterator i( std::find( waiting_.begin(), waiting_.end(), f) );
            if ( waiting_.end() != i) {
                // remove fiber from waiting-list
                waiting_.erase( i);
            }
            return false;
        }
#endif
    }
}

void
recursive_timed_mutex::unlock() {
    BOOST_ASSERT( mutex_status::locked == state_);
    BOOST_ASSERT( this_fiber::get_id() == owner_);

#if defined(BOOST_FIBERS_THREADSAFE)
    std::unique_lock< detail::spinlock > lk( splk_);
#endif
    fiber_context * f( nullptr);
    if ( 0 == --count_) {
        if ( ! waiting_.empty() ) {
            f = waiting_.front();
            waiting_.pop_front();
            BOOST_ASSERT( nullptr != f);
        }
        owner_ = fiber_context::id();
        state_ = mutex_status::unlocked;
#if defined(BOOST_FIBERS_THREADSAFE)
        lk.unlock();
#endif
        if ( nullptr != f) {
            BOOST_ASSERT( ! f->is_terminated() );
            f->set_ready();
        }
    }
}

}}

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_SUFFIX
#endif

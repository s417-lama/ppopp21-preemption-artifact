/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 2.0
//              Copyright (2014) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_ARGOBOTSEXEC_HPP
#define KOKKOS_ARGOBOTSEXEC_HPP

#include <Kokkos_Macros.hpp>
#if defined( KOKKOS_ENABLE_ARGOBOTS )

#include <Kokkos_Argobots.hpp>

#include <impl/Kokkos_Traits.hpp>
#include <impl/Kokkos_HostThreadTeam.hpp>

#include <Kokkos_Atomic.hpp>

#include <Kokkos_UniqueToken.hpp>

#include <iostream>
#include <sstream>
#include <fstream>

#include <abt.h>

#define HANDLE_ERROR(ret, msg)                        \
    if (ret != ABT_SUCCESS) {                         \
        fprintf(stderr, "ERROR[%d]: %s\n", ret, msg); \
        exit(EXIT_FAILURE);                           \
    }

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

struct busytime_measure_t {
  uint64_t acc;
  uint64_t begin_time;
  uint64_t last_time;
  int executing;
} __attribute__((aligned(64)));

namespace Kokkos { namespace Impl {

extern ABT_pool* g_pools;
extern ABT_pool* g_analysis_pools;
extern int g_num_xstreams;
extern int g_num_threads;
extern int g_num_pgroups;
extern ABT_barrier g_barrier;
extern ABT_mutex g_mutex;
extern ABT_xstream* g_xstreams;
extern ABT_sched* g_scheds;
extern ABT_preemption_group* g_preemption_groups;
extern int g_enable_preemption;
extern busytime_measure_t* g_busytimes;

//----------------------------------------------------------------------------

}} // namespace Kokkos::Impl

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {

inline Argobots::Argobots() noexcept
{}

#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
inline
void Argobots::fence( Argobots const& instance ) noexcept {}
#else
void Argobots::fence() {}
#endif

inline
#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
bool Argobots::is_initialized() noexcept
#else
bool Argobots::impl_is_initialized() noexcept
#endif
{ return ABT_initialized() == ABT_SUCCESS; }

inline
#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
int Argobots::thread_pool_size() noexcept
#else
int Argobots::impl_thread_pool_size() noexcept
#endif
{
  return Impl::g_num_threads;
}

inline
#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
int Argobots::thread_pool_size( int depth )
#else
int Argobots::impl_thread_pool_size( int depth )
#endif
{
  return depth < 2
#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
         ? thread_pool_size()
#else
         ? impl_thread_pool_size()
#endif
         : 1;
}

inline
bool Argobots::in_parallel( Argobots const& ) noexcept
{
  return 0;
}

KOKKOS_INLINE_FUNCTION
#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
int Argobots::hardware_thread_id() noexcept
#else
int Argobots::impl_hardware_thread_id() noexcept
#endif
{
  return -1 ;
}

inline
#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
int Argobots::max_hardware_threads() noexcept
#else
int Argobots::impl_max_hardware_threads() noexcept
#endif
{
  return Impl::g_num_threads;
}

KOKKOS_INLINE_FUNCTION
#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
int Argobots::thread_pool_rank() noexcept
#else
int Argobots::impl_thread_pool_rank() noexcept
#endif
{
#if defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
  int tid = 0;
  ABT_thread self;
  ABT_thread_self(&self);
  ABT_thread_get_arg(self, (void **)&tid);
  return tid;
#else
  return -1 ;
#endif
}

namespace Experimental {

template<>
class MasterLock<Argobots>
{
public:
  void lock()     { fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__); exit(1); }
  void unlock()   { fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__); exit(1); }
  bool try_lock() { fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__); exit(1); }

  MasterLock()  { fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__); exit(1); }
  ~MasterLock() { fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__); exit(1); }

  MasterLock( MasterLock const& ) = delete;
  MasterLock( MasterLock && )     = delete;
  MasterLock & operator=( MasterLock const& ) = delete;
  MasterLock & operator=( MasterLock && )     = delete;

private:

};

template<>
class UniqueToken< Argobots, UniqueTokenScope::Instance>
{
public:
  using execution_space = Argobots;
  using size_type       = int;

  /// \brief create object size for concurrency on the given instance
  ///
  /// This object should not be shared between instances
  UniqueToken( execution_space const& = execution_space() ) noexcept {}

  /// \brief upper bound for acquired values, i.e. 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  int size() const noexcept
    {
#if defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
#if defined( KOKKOS_ENABLE_DEPRECATED_CODE )
      return Kokkos::Argobots::thread_pool_size();
#else
      return Kokkos::Argobots::impl_thread_pool_size();
#endif
#else
      return 0 ;
#endif
    }

  /// \brief acquire value such that 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  int acquire() const  noexcept
    {
#if defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
#if defined( KOKKOS_ENABLE_DEPRECATED_CODE )
      return Kokkos::Argobots::thread_pool_rank();
#else
      return Kokkos::Argobots::impl_thread_pool_rank();
#endif
#else
      return 0 ;
#endif
    }

  /// \brief release a value acquired by generate
  KOKKOS_INLINE_FUNCTION
  void release( int ) const noexcept {}
};

template<>
class UniqueToken< Argobots, UniqueTokenScope::Global>
{
public:
  using execution_space = Argobots;
  using size_type       = int;

  /// \brief create object size for concurrency on the given instance
  ///
  /// This object should not be shared between instances
  UniqueToken( execution_space const& = execution_space() ) noexcept {}

  /// \brief upper bound for acquired values, i.e. 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  int size() const noexcept
    {
#if defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
      return Kokkos::Impl::g_num_xstreams;
#else
      return 0 ;
#endif
    }

  /// \brief acquire value such that 0 <= value < size()
  KOKKOS_INLINE_FUNCTION
  int acquire() const noexcept
    {
#if defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
      int rank;
      ABT_xstream_self_rank(&rank);
      return rank;
#else
      return 0 ;
#endif
    }

  /// \brief release a value acquired by generate
  KOKKOS_INLINE_FUNCTION
  void release( int ) const noexcept {}
};

} // namespace Experimental

} // namespace Kokkos

#endif
#endif /* #ifndef KOKKOS_ARGOBOTSEXEC_HPP */

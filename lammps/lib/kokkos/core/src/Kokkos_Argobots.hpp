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

#ifndef KOKKOS_ARGOBOTS_HPP
#define KOKKOS_ARGOBOTS_HPP

#include <Kokkos_Macros.hpp>
#if defined( KOKKOS_ENABLE_ARGOBOTS )

#include <Kokkos_Core_fwd.hpp>

#include <abt.h>

#include <cstddef>
#include <iosfwd>

#include <Kokkos_HostSpace.hpp>
#include <Kokkos_ScratchSpace.hpp>
#include <Kokkos_Parallel.hpp>
//#include <Kokkos_MemoryTraits.hpp>
#include <Kokkos_ExecPolicy.hpp>
//#include <Kokkos_TaskScheduler.hpp> // Uncomment when Tasking working.
#include <Kokkos_Layout.hpp>
#include <impl/Kokkos_Tags.hpp>
#include <KokkosExp_MDRangePolicy.hpp>
#include <impl/Kokkos_HostThreadTeam.hpp>

/*--------------------------------------------------------------------------*/

namespace Kokkos {

namespace Impl {

} // namespace Impl

} // namespace Kokkos

/*--------------------------------------------------------------------------*/

namespace Kokkos {

/** \brief  Execution space supported by Argobots */
class Argobots {
public:
  //! \name Type declarations that all Kokkos devices must provide.
  //@{
  inline
  Argobots() noexcept;

  //! Tag this class as an execution space
  typedef Argobots                 execution_space;
  typedef Kokkos::HostSpace        memory_space;
  //! This execution space preferred device_type
  typedef Kokkos::Device< execution_space, memory_space > device_type;

  typedef Kokkos::LayoutRight      array_layout;
  typedef memory_space::size_type  size_type;

  typedef ScratchMemorySpace< Argobots > scratch_memory_space;

  //@}
  /*------------------------------------------------------------------------*/

#ifdef KOKKOS_ENABLE_DEPRECATED_CODE
  static void initialize();
  static bool is_initialized() noexcept;
  static void finalize();
  inline
  static int thread_pool_size() noexcept;
  KOKKOS_INLINE_FUNCTION
  static int thread_pool_rank() noexcept;
  inline
  static int thread_pool_size( int depth );
  static void fence( Argobots const& = Argobots() ) noexcept;
  inline
  static int max_hardware_threads() noexcept;
  KOKKOS_INLINE_FUNCTION
  static int hardware_thread_id() noexcept;
#else
  static void impl_initialize();
  static bool impl_is_initialized() noexcept;
  static void impl_finalize();
  inline
  static int impl_thread_pool_size() noexcept;
  KOKKOS_INLINE_FUNCTION
  static int impl_thread_pool_rank() noexcept;
  inline
  static int impl_thread_pool_size( int depth );
  void fence() const;
  inline
  static int impl_max_hardware_threads() noexcept;
  KOKKOS_INLINE_FUNCTION
  static int impl_hardware_thread_id() noexcept;
#endif

  inline
  static bool in_parallel( Argobots const& = Argobots() ) noexcept;

  /** \brief Print configuration information to the given output stream. */
  static void print_configuration( std::ostream &, const bool detail = false );

  static int concurrency() {return 1;};

  static constexpr const char* name() noexcept { return "Argobots"; }
};

} // namespace Kokkos

/*--------------------------------------------------------------------------*/

namespace Kokkos {

namespace Impl {

template<>
struct MemorySpaceAccess
  < Kokkos::Argobots::memory_space
  , Kokkos::Argobots::scratch_memory_space
  >
{
  enum { assignable = false };
  enum { accessible = true };
  enum { deepcopy   = false };
};

template<>
struct VerifyExecutionCanAccessMemorySpace
  < Kokkos::Argobots::memory_space
  , Kokkos::Argobots::scratch_memory_space
  >
{
  enum { value = true };
  inline static void verify( void ) {}
  inline static void verify( const void * ) {}
};

} // namespace Impl

} // namespace Kokkos

/*--------------------------------------------------------------------------*/

#include <Argobots/Kokkos_ArgobotsExec.hpp>
#include <Argobots/Kokkos_Argobots_Team.hpp>
#include <Argobots/Kokkos_Argobots_Parallel.hpp>

#endif // #define KOKKOS_ENABLE_ARGOBOTS
#endif // #define KOKKOS_ARGOBOTS_HPP

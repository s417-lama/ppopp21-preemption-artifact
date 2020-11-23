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

#ifndef KOKKOS_ARGOBOTS_PARALLEL_HPP
#define KOKKOS_ARGOBOTS_PARALLEL_HPP

#include <Kokkos_Macros.hpp>
#if defined( KOKKOS_ENABLE_ARGOBOTS )

#include <vector>

#include <Kokkos_Parallel.hpp>

#include <impl/Kokkos_FunctorAdapter.hpp>

#include <Argobots/Kokkos_ArgobotsExec.hpp>
#include <KokkosExp_MDRangePolicy.hpp>

#include "logger.h"

//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

//----------------------------------------------------------------------------

struct task_base {
  virtual void * execute() = 0;
};

template<typename Func>
struct callable_task : task_base {
  Func func;
  callable_task(Func func_) : func(func_) {}
  void* execute() { func(); return NULL; }
};

template< class FunctorType , class ... Traits >
class ParallelFor< FunctorType
                 , Kokkos::RangePolicy< Traits ... >
                 , Kokkos::Argobots
                 >
{
private:

  typedef Kokkos::RangePolicy< Traits ... >  Policy ;

  typedef typename Policy::work_tag     WorkTag ;
  typedef typename Policy::member_type  Member ;
  typedef typename Policy::WorkRange    WorkRange ;

  const FunctorType  m_functor ;
  const Policy       m_policy ;

  static void invoke(void *arg_)
  {
    task_base *arg = (task_base *)arg_;
    arg->execute();
  }

  template< class TagType >
  inline static
  typename std::enable_if< std::is_same< TagType , void >::value >::type
  exec_range( const FunctorType & functor , const Member ibeg , const Member iend )
    {
      for ( Member i = ibeg ; i < iend ; ++i ) {
        functor( i );
      }
    }

  template< class TagType >
  inline static
  typename std::enable_if< ! std::is_same< TagType , void >::value >::type
  exec_range( const FunctorType & functor , const Member ibeg , const Member iend )
    {
      const TagType t{} ;
      for ( Member i = ibeg ; i < iend ; ++i ) {
        functor( t , i );
      }
    }

public:

  inline
  void execute() const
  {
    int ret;
    ABT_thread threads[g_num_threads];
    task_base* ts[g_num_threads];

    for (int i = 0; i < g_num_threads; i++) {
      const WorkRange range(m_policy, i, g_num_threads);
      Member b = range.begin();
      Member e = range.end();
      auto thread_fn = [=]() {
        ABT_thread self;
        ABT_thread_self(&self);
        ABT_thread_set_arg(self, (void *)i);

        int rank;
        ABT_xstream_self_rank(&rank);
        void *bp = logger_begin_tl(rank);

        exec_range<WorkTag>(m_functor, b, e);

        logger_end_tl(rank, bp, "simulation");
      };
      ts[i] = new callable_task<decltype(thread_fn)>(thread_fn);
      ABT_thread_attr attr;
      ABT_thread_attr_create(&attr);
      ABT_thread_attr_set_preemption_type(attr, ABT_PREEMPTION_DISABLED);
      ret = ABT_thread_create(g_pools[i % g_num_xstreams],
                              invoke,
                              ts[i],
                              attr,
                              &threads[i]);
      HANDLE_ERROR(ret, "ABT_thread_create");
    }

    for (int i = 0; i < g_num_threads; i++) {
      ret = ABT_thread_free(&threads[i]);
      HANDLE_ERROR(ret, "ABT_thread_free");
      delete ts[i];
    }
  }

  ParallelFor( const FunctorType & arg_functor
             , const Policy      & arg_policy
             )
    : m_functor( arg_functor )
    , m_policy(  arg_policy )
    { }
};

// MDRangePolicy impl
template< class FunctorType , class ... Traits >
class ParallelFor< FunctorType
                 , Kokkos::MDRangePolicy< Traits ... >
                 , Kokkos::Argobots
                 >
{
private:

  typedef Kokkos::MDRangePolicy< Traits ... > MDRangePolicy ;
  typedef typename MDRangePolicy::impl_range_policy         Policy ;
  typedef typename MDRangePolicy::work_tag                  WorkTag ;

  typedef typename Policy::WorkRange    WorkRange ;
  typedef typename Policy::member_type  Member ;

  typedef typename Kokkos::Impl::HostIterateTile< MDRangePolicy, FunctorType, typename MDRangePolicy::work_tag, void > iterate_type;

  const FunctorType   m_functor ;
  const MDRangePolicy m_mdr_policy ;
  const Policy        m_policy ;  // construct as RangePolicy( 0, num_tiles ).set_chunk_size(1) in ctor

  static void invoke(void *arg_)
  {
    task_base *arg = (task_base *)arg_;
    arg->execute();
  }

  inline static
  void
  exec_range( const MDRangePolicy & mdr_policy
            , const FunctorType & functor
            , const Member ibeg , const Member iend )
    {
      for ( Member iwork = ibeg ; iwork < iend ; ++iwork ) {
        iterate_type( mdr_policy, functor )( iwork );
      }
    }

public:

  inline void execute() const
  {
    int ret;
    ABT_thread threads[g_num_threads];
    task_base* ts[g_num_threads];
    for (int i = 0; i < g_num_threads; i++) {
      const WorkRange range(m_policy, i, g_num_threads);
      Member b = range.begin();
      Member e = range.end();
      auto thread_fn = [=]() {
        ABT_thread self;
        ABT_thread_self(&self);
        ABT_thread_set_arg(self, (void *)i);

        int rank;
        ABT_xstream_self_rank(&rank);
        void *bp = logger_begin_tl(rank);

        exec_range(m_mdr_policy, m_functor, b, e);

        logger_end_tl(rank, bp, "simulation");
      };
      ts[i] = new callable_task<decltype(thread_fn)>(thread_fn);
      ABT_thread_attr attr;
      ABT_thread_attr_create(&attr);
      ABT_thread_attr_set_preemption_type(attr, ABT_PREEMPTION_DISABLED);
      ret = ABT_thread_create(g_pools[i % g_num_xstreams],
                              invoke,
                              ts[i],
                              attr,
                              &threads[i]);
      HANDLE_ERROR(ret, "ABT_thread_create");
    }

    for (int i = 0; i < g_num_threads; i++) {
      ret = ABT_thread_free(&threads[i]);
      HANDLE_ERROR(ret, "ABT_thread_free");
      delete ts[i];
    }
  }

  inline
  ParallelFor( const FunctorType & arg_functor
             , MDRangePolicy arg_policy )
    : m_functor( arg_functor )
    , m_mdr_policy( arg_policy )
    , m_policy( Policy(0, m_mdr_policy.m_num_tiles).set_chunk_size(1) )
    {}
};

//----------------------------------------------------------------------------

template< class FunctorType , class ReducerType , class ... Traits >
class ParallelReduce< FunctorType
                    , Kokkos::RangePolicy< Traits ... >
                    , ReducerType
                    , Kokkos::Argobots
                    >
{
private:

  typedef Kokkos::RangePolicy< Traits ... > Policy ;

  typedef typename Policy::work_tag     WorkTag ;
  typedef typename Policy::WorkRange    WorkRange ;
  typedef typename Policy::member_type  Member ;

  typedef FunctorAnalysis< FunctorPatternInterface::REDUCE , Policy , FunctorType > Analysis ;

  typedef Kokkos::Impl::if_c< std::is_same<InvalidType,ReducerType>::value, FunctorType, ReducerType> ReducerConditional;
  typedef typename ReducerConditional::type ReducerTypeFwd;
  typedef typename Kokkos::Impl::if_c< std::is_same<InvalidType,ReducerType>::value, WorkTag, void>::type WorkTagFwd;

  // Static Assert WorkTag void if ReducerType not InvalidType

  typedef Kokkos::Impl::FunctorValueInit<   ReducerTypeFwd, WorkTagFwd > ValueInit ;
  typedef Kokkos::Impl::FunctorValueJoin<   ReducerTypeFwd, WorkTagFwd > ValueJoin ;

  typedef typename Analysis::pointer_type    pointer_type ;
  typedef typename Analysis::reference_type  reference_type ;

  const FunctorType   m_functor ;
  const Policy        m_policy ;
  const ReducerType   m_reducer ;
  const pointer_type  m_result_ptr ;

  mutable size_t m_pool_reduce_bytes;
  mutable pointer_type m_reduce_ptr;

  static void invoke(void *arg_)
  {
    task_base *arg = (task_base *)arg_;
    arg->execute();
  }

  template< class TagType >
  inline static
  typename std::enable_if< std::is_same< TagType , void >::value >::type
  exec_range( const FunctorType & functor
            , const Member ibeg , const Member iend
            , reference_type update )
    {
      for ( Member iwork = ibeg ; iwork < iend ; ++iwork ) {
        functor( iwork , update );
      }
    }

  template< class TagType >
  inline static
  typename std::enable_if< ! std::is_same< TagType , void >::value >::type
  exec_range( const FunctorType & functor
            , const Member ibeg , const Member iend
            , reference_type update )
    {
      const TagType t{} ;
      for ( Member iwork = ibeg ; iwork < iend ; ++iwork ) {
        functor( t , iwork , update );
      }
    }

public:

  inline
  void execute() const
  {
    size_t pool_reduce_bytes =
      Analysis::value_size( ReducerConditional::select(m_functor, m_reducer));
    pool_reduce_bytes = ((pool_reduce_bytes + 63) / 64) * 64;
    if (m_pool_reduce_bytes < pool_reduce_bytes) {
      m_pool_reduce_bytes = pool_reduce_bytes;
      if (m_reduce_ptr) free(m_reduce_ptr);
      posix_memalign((void**)&m_reduce_ptr, 64, m_pool_reduce_bytes * g_num_threads);
    }

    int ret;
    ABT_thread threads[g_num_threads];
    task_base* ts[g_num_threads];

    for (int i = 0; i < g_num_threads; i++) {
      const WorkRange range(m_policy, i, g_num_threads);
      Member b = range.begin();
      Member e = range.end();

      auto thread_fn = [=]() {
        ABT_thread self;
        ABT_thread_self(&self);
        ABT_thread_set_arg(self, (void *)i);

        int rank;
        ABT_xstream_self_rank(&rank);
        void *bp = logger_begin_tl(rank);

        reference_type update =
          ValueInit::init(  ReducerConditional::select(m_functor , m_reducer) , (pointer_type)((char*)m_reduce_ptr + i * m_pool_reduce_bytes) );

        ParallelReduce::template
          exec_range< WorkTag >( m_functor
                               , b
                               , e
                               , update );

        logger_end_tl(rank, bp, "simulation");
      };
      ts[i] = new callable_task<decltype(thread_fn)>(thread_fn);
      ABT_thread_attr attr;
      ABT_thread_attr_create(&attr);
      ABT_thread_attr_set_preemption_type(attr, ABT_PREEMPTION_DISABLED);
      ret = ABT_thread_create(g_pools[i % g_num_xstreams],
                              invoke,
                              ts[i],
                              attr,
                              &threads[i]);
      HANDLE_ERROR(ret, "ABT_thread_create");
    }

    for (int i = 0; i < g_num_threads; i++) {
      ret = ABT_thread_free(&threads[i]);
      HANDLE_ERROR(ret, "ABT_thread_free");
      delete ts[i];
    }

    // Reduction:

    for ( int i = 1 ; i < g_num_threads; ++i ) {
      ValueJoin::join( ReducerConditional::select(m_functor , m_reducer)
                     , m_reduce_ptr
                     , (pointer_type)((char*)m_reduce_ptr + i * m_pool_reduce_bytes) );
    }

    Kokkos::Impl::FunctorFinal<  ReducerTypeFwd , WorkTagFwd >::final( ReducerConditional::select(m_functor , m_reducer) , m_reduce_ptr );

    if ( m_result_ptr ) {
      const int n = Analysis::value_count( ReducerConditional::select(m_functor , m_reducer) );

      for ( int j = 0 ; j < n ; ++j ) { m_result_ptr[j] = m_reduce_ptr[j] ; }
    }
  }

  template< class ViewType >
  ParallelReduce( const FunctorType  & arg_functor
                , const Policy       & arg_policy
                , const ViewType & arg_result_view
                , typename std::enable_if<Kokkos::is_view< ViewType >::value &&
                                          !Kokkos::is_reducer_type< ReducerType >::value
                                          , void*>::type = NULL)
    : m_functor( arg_functor )
    , m_policy( arg_policy )
    , m_reducer( InvalidType() )
    , m_result_ptr( arg_result_view.data() )
    , m_pool_reduce_bytes(0)
    , m_reduce_ptr(NULL)
    { }

  ParallelReduce( const FunctorType & arg_functor
                , Policy       arg_policy
                , const ReducerType& reducer )
    : m_functor( arg_functor )
    , m_policy( arg_policy )
    , m_reducer( reducer )
    , m_result_ptr( reducer.result_view().data() )
    , m_pool_reduce_bytes(0)
    , m_reduce_ptr(NULL)
    { }
};

// MDRangePolicy impl
template< class FunctorType , class ReducerType, class ... Traits >
class ParallelReduce< FunctorType
                    , Kokkos::MDRangePolicy< Traits ...>
                    , ReducerType
                    , Kokkos::Argobots
                    >
{
private:

  typedef Kokkos::MDRangePolicy< Traits ... > MDRangePolicy ;
  typedef typename MDRangePolicy::impl_range_policy         Policy ;

  typedef typename MDRangePolicy::work_tag                  WorkTag ;
  typedef typename Policy::WorkRange                        WorkRange ;
  typedef typename Policy::member_type                      Member ;

  typedef FunctorAnalysis< FunctorPatternInterface::REDUCE , MDRangePolicy , FunctorType > Analysis ;

  typedef Kokkos::Impl::if_c< std::is_same<InvalidType,ReducerType>::value, FunctorType, ReducerType> ReducerConditional;
  typedef typename ReducerConditional::type ReducerTypeFwd;
  typedef typename Kokkos::Impl::if_c< std::is_same<InvalidType,ReducerType>::value, WorkTag, void>::type WorkTagFwd;

  typedef Kokkos::Impl::FunctorValueInit<   ReducerTypeFwd, WorkTagFwd > ValueInit ;
  typedef Kokkos::Impl::FunctorValueJoin<   ReducerTypeFwd, WorkTagFwd > ValueJoin ;

  typedef typename Analysis::pointer_type    pointer_type ;
  typedef typename Analysis::value_type      value_type ;
  typedef typename Analysis::reference_type  reference_type ;

  using iterate_type = typename Kokkos::Impl::HostIterateTile< MDRangePolicy
                                                             , FunctorType
                                                             , WorkTag
                                                             , reference_type
                                                             >;
  const FunctorType   m_functor ;
  const MDRangePolicy m_mdr_policy ;
  const Policy        m_policy ;     // construct as RangePolicy( 0, num_tiles ).set_chunk_size(1) in ctor
  const ReducerType   m_reducer ;
  const pointer_type  m_result_ptr ;

public:

  inline void execute() const
    {
      fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__);
      exit(1);
    }

  //----------------------------------------

  template< class ViewType >
  inline
  ParallelReduce( const FunctorType & arg_functor
                , MDRangePolicy       arg_policy
                , const ViewType    & arg_view
                , typename std::enable_if<
                           Kokkos::is_view< ViewType >::value &&
                           !Kokkos::is_reducer_type<ReducerType>::value
                  ,void*>::type = NULL)
    : m_functor( arg_functor )
    , m_mdr_policy(  arg_policy )
    , m_policy( Policy(0, m_mdr_policy.m_num_tiles).set_chunk_size(1) )
    , m_reducer( InvalidType() )
    , m_result_ptr(  arg_view.data() )
    {
    }

  inline
  ParallelReduce( const FunctorType & arg_functor
                , MDRangePolicy       arg_policy
                , const ReducerType& reducer )
    : m_functor( arg_functor )
    , m_mdr_policy(  arg_policy )
    , m_policy( Policy(0, m_mdr_policy.m_num_tiles).set_chunk_size(1) )
    , m_reducer( reducer )
    , m_result_ptr(  reducer.view().data() )
    {
    }

};

//----------------------------------------------------------------------------

template< class FunctorType , class ... Traits >
class ParallelScan< FunctorType
                  , Kokkos::RangePolicy< Traits ... >
                  , Kokkos::Argobots
                  >
{
private:

  typedef Kokkos::RangePolicy< Traits ... >  Policy ;

  typedef typename Policy::work_tag     WorkTag ;
  typedef typename Policy::WorkRange    WorkRange ;
  typedef typename Policy::member_type  Member ;

  typedef Kokkos::Impl::FunctorValueTraits< FunctorType, WorkTag > ValueTraits ;
  typedef Kokkos::Impl::FunctorValueInit<   FunctorType, WorkTag > ValueInit ;

  typedef typename ValueTraits::pointer_type    pointer_type ;
  typedef typename ValueTraits::reference_type  reference_type ;

  const FunctorType  m_functor ;
  const Policy       m_policy ;

public:

  inline
  void execute() const
    {
      fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__);
      exit(1);
    }

  ParallelScan( const FunctorType & arg_functor
              , const Policy      & arg_policy
              )
    : m_functor( arg_functor )
    , m_policy( arg_policy )
    {
    }
};

//----------------------------------------------------------------------------

template< class FunctorType , class ... Properties >
class ParallelFor< FunctorType
                 , Kokkos::TeamPolicy< Properties ... >
                 , Kokkos::Argobots
                 >
{
private:

  typedef Kokkos::Impl::TeamPolicyInternal< Kokkos::Argobots, Properties ... > Policy ;
  typedef typename Policy::work_tag             WorkTag ;
  typedef typename Policy::schedule_type::type  SchedTag ;
  typedef typename Policy::member_type          Member ;

  const FunctorType    m_functor;
  const Policy         m_policy;
  const int            m_shmem_size;

public:

  inline
  void execute() const
    {
      fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__);
      exit(1);
    }


  inline
  ParallelFor( const FunctorType & arg_functor ,
               const Policy      & arg_policy )
    : m_functor( arg_functor )
    , m_policy(  arg_policy )
    , m_shmem_size( arg_policy.scratch_size(0) +
                    arg_policy.scratch_size(1) +
                    FunctorTeamShmemSize< FunctorType >
                      ::value( arg_functor , arg_policy.team_size() ) )
    {}
};

//----------------------------------------------------------------------------

template< class FunctorType , class ReducerType, class ... Properties >
class ParallelReduce< FunctorType
                    , Kokkos::TeamPolicy< Properties ... >
                    , ReducerType
                    , Kokkos::Argobots
                    >
{
private:

  typedef Kokkos::Impl::TeamPolicyInternal< Kokkos::Argobots, Properties ... >         Policy ;

  typedef FunctorAnalysis< FunctorPatternInterface::REDUCE , Policy , FunctorType > Analysis ;

  typedef typename Policy::work_tag             WorkTag ;
  typedef typename Policy::schedule_type::type  SchedTag ;
  typedef typename Policy::member_type          Member ;

  typedef Kokkos::Impl::if_c< std::is_same<InvalidType,ReducerType>::value
                            , FunctorType, ReducerType> ReducerConditional;

  typedef typename ReducerConditional::type ReducerTypeFwd;
  typedef typename Kokkos::Impl::if_c< std::is_same<InvalidType,ReducerType>::value, WorkTag, void>::type WorkTagFwd;

  typedef Kokkos::Impl::FunctorValueInit<   ReducerTypeFwd , WorkTagFwd >  ValueInit ;
  typedef Kokkos::Impl::FunctorValueJoin<   ReducerTypeFwd , WorkTagFwd >  ValueJoin ;

  typedef typename Analysis::pointer_type    pointer_type ;
  typedef typename Analysis::reference_type  reference_type ;

  const FunctorType    m_functor;
  const Policy         m_policy;
  const ReducerType    m_reducer;
  const pointer_type   m_result_ptr;
  const int            m_shmem_size;

public:

  inline
  void execute() const
    {
      fprintf(stderr, "unimplemented (%s:%d).\n", __FILE__, __LINE__);
      exit(1);
    }

  //----------------------------------------

  template< class ViewType >
  inline
  ParallelReduce( const FunctorType  & arg_functor ,
                  const Policy       & arg_policy ,
                  const ViewType     & arg_result ,
                  typename std::enable_if<
                    Kokkos::is_view< ViewType >::value &&
                    !Kokkos::is_reducer_type<ReducerType>::value
                    ,void*>::type = NULL)
    : m_functor( arg_functor )
    , m_policy(  arg_policy )
    , m_reducer( InvalidType() )
    , m_result_ptr( arg_result.data() )
    , m_shmem_size( arg_policy.scratch_size(0) +
                    arg_policy.scratch_size(1) +
                    FunctorTeamShmemSize< FunctorType >
                      ::value( arg_functor , arg_policy.team_size() ) )
    {}

  inline
  ParallelReduce( const FunctorType & arg_functor
    , Policy       arg_policy
    , const ReducerType& reducer )
  : m_functor( arg_functor )
  , m_policy(  arg_policy )
  , m_reducer( reducer )
  , m_result_ptr(  reducer.view().data() )
  , m_shmem_size( arg_policy.scratch_size(0) +
                  arg_policy.scratch_size(1) +
                  FunctorTeamShmemSize< FunctorType >
                    ::value( arg_functor , arg_policy.team_size() ) )
  {
  }

};

} // namespace Impl
} // namespace Kokkos

#endif
#endif /* #define KOKKOS_ARGOBOTS_PARALLEL_HPP */

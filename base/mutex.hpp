#pragma once

#include "base/assert.hpp"

#include <pthread.h>

namespace threads
{
  class Condition;
  namespace impl
  {
    class ConditionImpl;
    class ImplWinVista;
  }

  /// Mutex primitive, used only for synchronizing this process threads
  /// based on Critical Section under Win32 and pthreads under Linux
  /// @author Siarhei Rachytski
  /// @deprecated As the MacOS implementation doesn't support recursive mutexes we should emulate them by ourselves.
  /// The code is taken from @a http://www.omnigroup.com/mailman/archive/macosx-dev/2002-March/036465.html
  class Mutex
  {
  private:

    pthread_mutex_t m_Mutex;

    Mutex & operator=(Mutex const &);
    Mutex(Mutex const &);

    friend class threads::impl::ConditionImpl;
    friend class threads::impl::ImplWinVista;
    friend class threads::Condition;

  public:

    Mutex()
    {
      ::pthread_mutex_init(&m_Mutex, 0);
    }

    ~Mutex()
    {
      ::pthread_mutex_destroy(&m_Mutex);
    }
    
    void Lock()
    {
      VERIFY(0 == ::pthread_mutex_lock(&m_Mutex), ());
    }

    bool TryLock()
    {
      return (0 == ::pthread_mutex_trylock(&m_Mutex));
    }

    void Unlock()
    {
      VERIFY(0 == ::pthread_mutex_unlock(&m_Mutex), ());
    }

  };

  /// ScopeGuard wrapper around mutex
  class MutexGuard
  {
  public:
  	MutexGuard(Mutex & mutex): m_Mutex(mutex) { m_Mutex.Lock(); }
  	~MutexGuard() { m_Mutex.Unlock(); }
  private:
    Mutex & m_Mutex;
  };
  
} // namespace threads

#ifndef _PASSENGER_SYSTEM_H_
#define _PASSENGER_SYSTEM_H_

#include <boost/thread.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <ctime>

/**
 * Support for interruption of blocking system calls and C library calls.
 *
 * This file provides a framework for writing multithreading code that can
 * be interrupted, even when blocked on system calls or C library calls.
 *
 * One must first call Passenger::setupSysCallInterruptionSupport().
 * Then one may use the functions in Passenger::InterruptableCalls
 * as drop-in replacements for system calls or C library functions.
 * Thread::interrupt() and Thread::interruptAndJoin() should be used
 * for interrupting threads.
 */

// This is one of the things that Java is good at and C++ sucks at. Sigh...

namespace Passenger {

	using namespace boost;
	
	static const int INTERRUPTION_SIGNAL = SIGINT;
	
	/**
	 * Setup system call interruption support.
	 * This function may only be called once. It installs a signal handler
	 * for INTERRUPTION_SIGNAL, so one should not install a different signal
	 * handler for that signal after calling this function.
	 */
	void setupSyscallInterruptionSupport();
	
	/**
	 * Thread class with system call interruption support.
	 */
	class Thread: public thread {
	public:
		template <class F>
		explicit Thread(F f, unsigned int stackSize = 0)
			: thread(f, stackSize) {}
		
		/**
		 * Interrupt the thread. This method behaves just like
		 * boost::thread::interrupt(), but will also respect the interruption
		 * points defined in Passenger::InterruptableCalls.
		 *
		 * Note that an interruption request may get lost, depending on the
		 * current execution point of the thread. Thus, one should call this
		 * method in a loop, until a certain goal condition has been fulfilled.
		 * interruptAndJoin() is a convenience method that implements this
		 * pattern.
		 */
		void interrupt() {
			int ret;
			
			thread::interrupt();
			do {
				ret = pthread_kill(native_handle(),
					INTERRUPTION_SIGNAL);
			} while (ret == EINTR);
		}
		
		/**
		 * Keep interrupting the thread until it's done, then join it.
		 *
		 * @throws boost::thread_interrupted
		 */
		void interruptAndJoin() {
			bool done = false;
			while (!done) {
				interrupt();
				done = timed_join(posix_time::millisec(10));
			}
		}
	};
	
	/**
	 * System call and C library call wrappers with interruption support.
	 * These functions are interruption points, i.e. they throw boost::thread_interrupted
	 * whenever the calling thread is interrupted by Thread::interrupt() or
	 * Thread::interruptAndJoin().
	 */
	namespace InterruptableCalls {
		ssize_t read(int fd, void *buf, size_t count);
		ssize_t write(int fd, const void *buf, size_t count);
		int close(int fd);
		
		int socketpair(int d, int type, int protocol, int sv[2]);
		ssize_t recvmsg(int s, struct msghdr *msg, int flags);
		ssize_t sendmsg(int s, const struct msghdr *msg, int flags);
		int shutdown(int s, int how);
		
		FILE *fopen(const char *path, const char *mode);
		int fclose(FILE *fp);
		
		time_t time(time_t *t);
		int usleep(useconds_t usec);
		
		pid_t fork();
		int kill(pid_t pid, int sig);
		pid_t waitpid(pid_t pid, int *status, int options);
	}

} // namespace Passenger

namespace boost {
namespace this_thread {

	/**
	 * @intern
	 */
	extern thread_specific_ptr<bool> _syscalls_interruptable;
	
	/**
	 * Check whether system calls should be interruptable in
	 * the calling thread.
	 */
	bool syscalls_interruptable();

	/**
	 * Create this struct on the stack to temporarily enable system
	 * call interruption, until the object goes out of scope.
	 */
	struct enable_syscall_interruption {
		bool lastValue;
		
		enable_syscall_interruption() {
			if (_syscalls_interruptable.get() == NULL) {
				lastValue = true;
				_syscalls_interruptable.reset(new bool(true));
			} else {
				lastValue = *_syscalls_interruptable;
				*_syscalls_interruptable = true;
			}
		}
		
		~enable_syscall_interruption() {
			*_syscalls_interruptable = lastValue;
		}
	};
	
	/**
	 * Create this struct on the stack to temporarily disable system
	 * call interruption, until the object goes out of scope.
	 */
	struct disable_syscall_interruption {
		bool lastValue;
		
		disable_syscall_interruption() {
			if (_syscalls_interruptable.get() == NULL) {
				lastValue = true;
				_syscalls_interruptable.reset(new bool(false));
			} else {
				lastValue = *_syscalls_interruptable;
				*_syscalls_interruptable = false;
			}
		}
		
		~disable_syscall_interruption() {
			*_syscalls_interruptable = lastValue;
		}
	};

} // namespace this_thread
} // namespace boost

#endif /* _PASSENGER_SYSTEM_H_ */


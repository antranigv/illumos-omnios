/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2017 Hayashi Naoyuki
 */

#include "lint.h"
#include <sys/feature_tests.h>
/*
 * setcontext() really can return, if UC_CPU is not specified.
 * Make the compiler shut up about it.
 */
#if defined(__NORETURN)
#undef	__NORETURN
#endif
#define	__NORETURN
#include "thr_uberdata.h"
#include "asyncio.h"
#include <signal.h>
#include <siginfo.h>
#include <sys/systm.h>

/* maskable signals */
const sigset_t maskset = {MASKSET0, MASKSET1, MASKSET2, MASKSET3};

/*
 * Return true if the valid signal bits in both sets are the same.
 */
int
sigequalset(const sigset_t *s1, const sigset_t *s2)
{
	/*
	 * We only test valid signal bits, not rubbish following MAXSIG
	 * (for speed).  Algorithm:
	 * if (s1 & fillset) == (s2 & fillset) then (s1 ^ s2) & fillset == 0
	 */
/* see lib/libc/inc/thr_uberdata.h for why this must be true */
#if (MAXSIG > (2 * 32) && MAXSIG <= (3 * 32))
	return (!((s1->__sigbits[0] ^ s2->__sigbits[0]) |
	    (s1->__sigbits[1] ^ s2->__sigbits[1]) |
	    ((s1->__sigbits[2] ^ s2->__sigbits[2]) & FILLSET2)));
#else
#error "fix me: MAXSIG out of bounds"
#endif
}

/*
 * Common code for calling the user-specified signal handler.
 */
void
call_user_handler(int sig, siginfo_t *sip, ucontext_t *ucp)
{
	ulwp_t *self = curthread;
	uberdata_t *udp = self->ul_uberdata;
	struct sigaction uact;
	volatile struct sigaction *sap;

	/*
	 * If we are taking a signal while parked or about to be parked
	 * on __lwp_park() then remove ourself from the sleep queue so
	 * that we can grab locks.  The code in mutex_lock_queue() and
	 * cond_wait_common() will detect this and deal with it when
	 * __lwp_park() returns.
	 */
	unsleep_self();
	set_parking_flag(self, 0);

	if (__td_event_report(self, TD_CATCHSIG, udp)) {
		self->ul_td_evbuf.eventnum = TD_CATCHSIG;
		self->ul_td_evbuf.eventdata = (void *)(intptr_t)sig;
		tdb_event(TD_CATCHSIG, udp);
	}

	/*
	 * Get a self-consistent set of flags, handler, and mask
	 * while holding the sig's sig_lock for the least possible time.
	 * We must acquire the sig's sig_lock because some thread running
	 * in sigaction() might be establishing a new signal handler.
	 * The code in sigaction() acquires the writer lock; here
	 * we acquire the readers lock to ehance concurrency in the
	 * face of heavy signal traffic, such as generated by java.
	 *
	 * Locking exceptions:
	 * No locking for a child of vfork().
	 * If the signal is SIGPROF with an si_code of PROF_SIG,
	 * then we assume that this signal was generated by
	 * setitimer(ITIMER_REALPROF) set up by the dbx collector.
	 * If the signal is SIGEMT with an si_code of EMT_CPCOVF,
	 * then we assume that the signal was generated by
	 * a hardware performance counter overflow.
	 * In these cases, assume that we need no locking.  It is the
	 * monitoring program's responsibility to ensure correctness.
	 */
	sap = &udp->siguaction[sig].sig_uaction;
	if (self->ul_vfork ||
	    (sip != NULL &&
	    ((sig == SIGPROF && sip->si_code == PROF_SIG) ||
	    (sig == SIGEMT && sip->si_code == EMT_CPCOVF)))) {
		/* we wish this assignment could be atomic */
		(void) memcpy(&uact, (void *)sap, sizeof (uact));
	} else {
		rwlock_t *rwlp = &udp->siguaction[sig].sig_lock;
		lrw_rdlock(rwlp);
		(void) memcpy(&uact, (void *)sap, sizeof (uact));
		if ((sig == SIGCANCEL || sig == SIGAIOCANCEL) &&
		    (sap->sa_flags & SA_RESETHAND))
			sap->sa_sigaction = SIG_DFL;
		lrw_unlock(rwlp);
	}

	/*
	 * Set the proper signal mask and call the user's signal handler.
	 * (We overrode the user-requested signal mask with maskset
	 * so we currently have all blockable signals blocked.)
	 *
	 * We would like to ASSERT() that the signal is not a member of the
	 * signal mask at the previous level (ucp->uc_sigmask) or the specified
	 * signal mask for sigsuspend() or pollsys() (self->ul_tmpmask) but
	 * /proc can override this via PCSSIG, so we don't bother.
	 *
	 * We would also like to ASSERT() that the signal mask at the previous
	 * level equals self->ul_sigmask (maskset for sigsuspend() / pollsys()),
	 * but /proc can change the thread's signal mask via PCSHOLD, so we
	 * don't bother with that either.
	 */
	ASSERT(ucp->uc_flags & UC_SIGMASK);
	if (self->ul_sigsuspend) {
		ucp->uc_sigmask = self->ul_sigmask;
		self->ul_sigsuspend = 0;
		/* the sigsuspend() or pollsys() signal mask */
		sigorset(&uact.sa_mask, &self->ul_tmpmask);
	} else {
		/* the signal mask at the previous level */
		sigorset(&uact.sa_mask, &ucp->uc_sigmask);
	}
	if (!(uact.sa_flags & SA_NODEFER))	/* add current signal */
		(void) sigaddset(&uact.sa_mask, sig);
	self->ul_sigmask = uact.sa_mask;
	self->ul_siglink = ucp;
	(void) __lwp_sigmask(SIG_SETMASK, &uact.sa_mask);

	/*
	 * If this thread has been sent SIGCANCEL from the kernel
	 * or from pthread_cancel(), it is being asked to exit.
	 * The kernel may send SIGCANCEL without a siginfo struct.
	 * If the SIGCANCEL is process-directed (from kill() or
	 * sigqueue()), treat it as an ordinary signal.
	 */
	if (sig == SIGCANCEL) {
		if (sip == NULL || SI_FROMKERNEL(sip) ||
		    sip->si_code == SI_LWP) {
			do_sigcancel();
			goto out;
		}
		/* SIGCANCEL is ignored by default */
		if (uact.sa_sigaction == SIG_DFL ||
		    uact.sa_sigaction == SIG_IGN)
			goto out;
	}

	/*
	 * If this thread has been sent SIGAIOCANCEL (SIGLWP) and
	 * we are an aio worker thread, cancel the aio request.
	 */
	if (sig == SIGAIOCANCEL) {
		aio_worker_t *aiowp = pthread_getspecific(_aio_key);

		if (sip != NULL && sip->si_code == SI_LWP && aiowp != NULL)
			siglongjmp(aiowp->work_jmp_buf, 1);
		/* SIGLWP is ignored by default */
		if (uact.sa_sigaction == SIG_DFL ||
		    uact.sa_sigaction == SIG_IGN)
			goto out;
	}

	if (!(uact.sa_flags & SA_SIGINFO))
		sip = NULL;
	__sighndlr(sig, sip, ucp, uact.sa_sigaction);

#if defined(sparc) || defined(__sparc)
	/*
	 * If this is a floating point exception and the queue
	 * is non-empty, pop the top entry from the queue.  This
	 * is to maintain expected behavior.
	 */
	if (sig == SIGFPE && ucp->uc_mcontext.fpregs.fpu_qcnt) {
		fpregset_t *fp = &ucp->uc_mcontext.fpregs;

		if (--fp->fpu_qcnt > 0) {
			unsigned char i;
			struct _fq *fqp;

			fqp = fp->fpu_q;
			for (i = 0; i < fp->fpu_qcnt; i++)
				fqp[i] = fqp[i+1];
		}
	}
#endif	/* sparc */

out:
	(void) setcontext(ucp);
	thr_panic("call_user_handler(): setcontext() returned");
}

/*
 * take_deferred_signal() is called when ul_critical and ul_sigdefer become
 * zero and a deferred signal has been recorded on the current thread.
 * We are out of the critical region and are ready to take a signal.
 * The kernel has all signals blocked on this lwp, but our value of
 * ul_sigmask is the correct signal mask for the previous context.
 *
 * We call __sigresend() to atomically restore the signal mask and
 * cause the signal to be sent again with the remembered siginfo.
 * We will not return successfully from __sigresend() until the
 * application's signal handler has been run via sigacthandler().
 */
void
take_deferred_signal(int sig)
{
	extern int __sigresend(int, siginfo_t *, sigset_t *);
	ulwp_t *self = curthread;
	siguaction_t *suap = &self->ul_uberdata->siguaction[sig];
	siginfo_t *sip;
	int error;

	ASSERT((self->ul_critical | self->ul_sigdefer | self->ul_cursig) == 0);

	/*
	 * If the signal handler was established with SA_RESETHAND,
	 * the kernel has reset the handler to SIG_DFL, so we have
	 * to reestablish the handler now so that it will be entered
	 * again when we call __sigresend(), below.
	 *
	 * Logically, we should acquire and release the signal's
	 * sig_lock around this operation to protect the integrity
	 * of the signal action while we copy it, as is done below
	 * in _libc_sigaction().  However, we may be on a user-level
	 * sleep queue at this point and lrw_wrlock(&suap->sig_lock)
	 * might attempt to sleep on a different sleep queue and
	 * that would corrupt the entire sleep queue mechanism.
	 *
	 * If we are on a sleep queue we will remove ourself from
	 * it in call_user_handler(), called from sigacthandler(),
	 * before entering the application's signal handler.
	 * In the meantime, we must not acquire any locks.
	 */
	if (suap->sig_uaction.sa_flags & SA_RESETHAND) {
		struct sigaction tact = suap->sig_uaction;
		tact.sa_flags &= ~SA_NODEFER;
		tact.sa_sigaction = self->ul_uberdata->sigacthandler;
		tact.sa_mask = maskset;
		(void) __sigaction(sig, &tact, NULL);
	}

	if (self->ul_siginfo.si_signo == 0)
		sip = NULL;
	else
		sip = &self->ul_siginfo;

	/* EAGAIN can happen only for a pending SIGSTOP signal */
	while ((error = __sigresend(sig, sip, &self->ul_sigmask)) == EAGAIN)
		continue;
	if (error)
		thr_panic("take_deferred_signal(): __sigresend() failed");
}

void
sigacthandler(int sig, siginfo_t *sip, void *uvp)
{
	ucontext_t *ucp = uvp;
	ulwp_t *self = curthread;

	/*
	 * Do this in case we took a signal while in a cancelable system call.
	 * It does no harm if we were not in such a system call.
	 */
	self->ul_sp = 0;
	if (sig != SIGCANCEL)
		self->ul_cancel_async = self->ul_save_async;

	/*
	 * If this thread has performed a longjmp() from a signal handler
	 * back to main level some time in the past, it has left the kernel
	 * thinking that it is still in the signal context.  We repair this
	 * possible damage by setting ucp->uc_link to NULL if we know that
	 * we are actually executing at main level (self->ul_siglink == NULL).
	 * See the code for setjmp()/longjmp() for more details.
	 */
	if (self->ul_siglink == NULL)
		ucp->uc_link = NULL;

	/*
	 * If we are not in a critical region and are
	 * not deferring signals, take the signal now.
	 */
	if ((self->ul_critical + self->ul_sigdefer) == 0) {
		call_user_handler(sig, sip, ucp);
		/*
		 * On the surface, the following call seems redundant
		 * because call_user_handler() cannot return. However,
		 * we don't want to return from here because the compiler
		 * might recycle our frame. We want to keep it on the
		 * stack to assist debuggers such as pstack in identifying
		 * signal frames. The call to thr_panic() serves to prevent
		 * tail-call optimisation here.
		 */
		thr_panic("sigacthandler(): call_user_handler() returned");
	}

	/*
	 * We are in a critical region or we are deferring signals.  When
	 * we emerge from the region we will call take_deferred_signal().
	 */
	ASSERT(self->ul_cursig == 0);
	self->ul_cursig = (char)sig;
	if (sip != NULL)
		(void) memcpy(&self->ul_siginfo,
		    sip, sizeof (siginfo_t));
	else
		self->ul_siginfo.si_signo = 0;

	/*
	 * Make sure that if we return to a call to __lwp_park()
	 * or ___lwp_cond_wait() that it returns right away
	 * (giving us a spurious wakeup but not a deadlock).
	 */
	set_parking_flag(self, 0);

	/*
	 * Return to the previous context with all signals blocked.
	 * We will restore the signal mask in take_deferred_signal().
	 * Note that we are calling the system call trap here, not
	 * the setcontext() wrapper.  We don't want to change the
	 * thread's ul_sigmask by this operation.
	 */
	ucp->uc_sigmask = maskset;
	(void) __setcontext(ucp);
	thr_panic("sigacthandler(): __setcontext() returned");
}

#pragma weak _sigaction = sigaction
int
sigaction(int sig, const struct sigaction *nact, struct sigaction *oact)
{
	ulwp_t *self = curthread;
	uberdata_t *udp = self->ul_uberdata;
	struct sigaction oaction;
	struct sigaction tact;
	struct sigaction *tactp = NULL;
	int rv;

	if (sig <= 0 || sig >= NSIG) {
		errno = EINVAL;
		return (-1);
	}

	if (!self->ul_vfork)
		lrw_wrlock(&udp->siguaction[sig].sig_lock);

	oaction = udp->siguaction[sig].sig_uaction;

	if (nact != NULL) {
		tact = *nact;	/* make a copy so we can modify it */
		tactp = &tact;
		delete_reserved_signals(&tact.sa_mask);

#if !defined(_LP64)
		tact.sa_resv[0] = tact.sa_resv[1] = 0;	/* cleanliness */
#endif
		/*
		 * To be compatible with the behavior of SunOS 4.x:
		 * If the new signal handler is SIG_IGN or SIG_DFL, do
		 * not change the signal's entry in the siguaction array.
		 * This allows a child of vfork(2) to set signal handlers
		 * to SIG_IGN or SIG_DFL without affecting the parent.
		 *
		 * This also covers a race condition with some thread
		 * setting the signal action to SIG_DFL or SIG_IGN
		 * when the thread has also received and deferred
		 * that signal.  When the thread takes the deferred
		 * signal, even though it has set the action to SIG_DFL
		 * or SIG_IGN, it will execute the old signal handler
		 * anyway.  This is an inherent signaling race condition
		 * and is not a bug.
		 *
		 * A child of vfork() is not allowed to change signal
		 * handlers to anything other than SIG_DFL or SIG_IGN.
		 */
		if (self->ul_vfork) {
			if (tact.sa_sigaction != SIG_IGN)
				tact.sa_sigaction = SIG_DFL;
		} else if (sig == SIGCANCEL || sig == SIGAIOCANCEL) {
			/*
			 * Always catch these signals.
			 * We need SIGCANCEL for pthread_cancel() to work.
			 * We need SIGAIOCANCEL for aio_cancel() to work.
			 */
			udp->siguaction[sig].sig_uaction = tact;
			if (tact.sa_sigaction == SIG_DFL ||
			    tact.sa_sigaction == SIG_IGN)
				tact.sa_flags = SA_SIGINFO;
			else {
				tact.sa_flags |= SA_SIGINFO;
				tact.sa_flags &=
				    ~(SA_NODEFER | SA_RESETHAND | SA_RESTART);
			}
			tact.sa_sigaction = udp->sigacthandler;
			tact.sa_mask = maskset;
		} else if (tact.sa_sigaction != SIG_DFL &&
		    tact.sa_sigaction != SIG_IGN) {
			udp->siguaction[sig].sig_uaction = tact;
			tact.sa_flags &= ~SA_NODEFER;
			tact.sa_sigaction = udp->sigacthandler;
			tact.sa_mask = maskset;
		}
	}

	if ((rv = __sigaction(sig, tactp, oact)) != 0)
		udp->siguaction[sig].sig_uaction = oaction;
	else if (oact != NULL &&
	    oact->sa_sigaction != SIG_DFL &&
	    oact->sa_sigaction != SIG_IGN)
		*oact = oaction;

	/*
	 * We detect setting the disposition of SIGIO just to set the
	 * _sigio_enabled flag for the asynchronous i/o (aio) code.
	 */
	if (sig == SIGIO && rv == 0 && tactp != NULL) {
		_sigio_enabled =
		    (tactp->sa_handler != SIG_DFL &&
		    tactp->sa_handler != SIG_IGN);
	}

	if (!self->ul_vfork)
		lrw_unlock(&udp->siguaction[sig].sig_lock);
	return (rv);
}

/*
 * This is a private interface for the linux brand interface.
 */
void
setsigacthandler(void (*nsigacthandler)(int, siginfo_t *, void *),
    void (**osigacthandler)(int, siginfo_t *, void *))
{
	ulwp_t *self = curthread;
	uberdata_t *udp = self->ul_uberdata;

	if (osigacthandler != NULL)
		*osigacthandler = udp->sigacthandler;

	udp->sigacthandler = nsigacthandler;
}

/*
 * Tell the kernel to block all signals.
 * Use the schedctl interface, or failing that, use __lwp_sigmask().
 * This action can be rescinded only by making a system call that
 * sets the signal mask:
 *	__lwp_sigmask(), __sigprocmask(), __setcontext(),
 *	__sigsuspend() or __pollsys().
 * In particular, this action cannot be reversed by assigning
 * scp->sc_sigblock = 0.  That would be a way to lose signals.
 * See the definition of restore_signals(self).
 */
void
block_all_signals(ulwp_t *self)
{
	volatile sc_shared_t *scp;

	enter_critical(self);
	if ((scp = self->ul_schedctl) != NULL ||
	    (scp = setup_schedctl()) != NULL)
		scp->sc_sigblock = 1;
	else
		(void) __lwp_sigmask(SIG_SETMASK, &maskset);
	exit_critical(self);
}

/*
 * setcontext() has code that forcibly restores the curthread
 * pointer in a context passed to the setcontext(2) syscall.
 *
 * Certain processes may need to disable this feature, so these routines
 * provide the mechanism to do so.
 *
 * (As an example, branded 32-bit x86 processes may use %gs for their own
 * purposes, so they need to be able to specify a %gs value to be restored
 * on return from a signal handler via the passed ucontext_t.)
 */
static int setcontext_enforcement = 1;

void
set_setcontext_enforcement(int on)
{
	setcontext_enforcement = on;
}

#pragma weak _setcontext = setcontext
int
setcontext(const ucontext_t *ucp)
{
	ulwp_t *self = curthread;
	int ret;
	ucontext_t uc;

	/*
	 * Returning from the main context (uc_link == NULL) causes
	 * the thread to exit.  See setcontext(2) and makecontext(3C).
	 */
	if (ucp == NULL)
		thr_exit(NULL);
	(void) memcpy(&uc, ucp, sizeof (uc));

	/*
	 * Restore previous signal mask and context link.
	 */
	if (uc.uc_flags & UC_SIGMASK) {
		block_all_signals(self);
		delete_reserved_signals(&uc.uc_sigmask);
		self->ul_sigmask = uc.uc_sigmask;
		if (self->ul_cursig) {
			/*
			 * We have a deferred signal present.
			 * The signal mask will be set when the
			 * signal is taken in take_deferred_signal().
			 */
			ASSERT(self->ul_critical + self->ul_sigdefer != 0);
			uc.uc_flags &= ~UC_SIGMASK;
		}
	}
	self->ul_siglink = uc.uc_link;

	/*
	 * We don't know where this context structure has been.
	 * Preserve the curthread pointer, at least.
	 *
	 * Allow this feature to be disabled if a particular process
	 * requests it.
	 */
	if (setcontext_enforcement) {
#if defined(__sparc)
		uc.uc_mcontext.gregs[REG_G7] = (greg_t)self;
#elif defined(__amd64)
		uc.uc_mcontext.gregs[REG_FS] = (greg_t)0; /* null for fsbase */
#elif defined(__i386)
		uc.uc_mcontext.gregs[GS] = (greg_t)LWPGS_SEL;
#elif defined(__aarch64__)
		uc.uc_mcontext.gregs[REG_TP] = (greg_t)self;
#else
#error Unknown ISA
#endif
	}

	/*
	 * Make sure that if we return to a call to __lwp_park()
	 * or ___lwp_cond_wait() that it returns right away
	 * (giving us a spurious wakeup but not a deadlock).
	 */
	set_parking_flag(self, 0);
	self->ul_sp = 0;
	ret = __setcontext(&uc);

	/*
	 * It is OK for setcontext() to return if the user has not specified
	 * UC_CPU.
	 */
	if (uc.uc_flags & UC_CPU)
		thr_panic("setcontext(): __setcontext() returned");
	return (ret);
}

#pragma weak _thr_sigsetmask = thr_sigsetmask
int
thr_sigsetmask(int how, const sigset_t *set, sigset_t *oset)
{
	ulwp_t *self = curthread;
	sigset_t saveset;

	if (set == NULL) {
		enter_critical(self);
		if (oset != NULL)
			*oset = self->ul_sigmask;
		exit_critical(self);
	} else {
		switch (how) {
		case SIG_BLOCK:
		case SIG_UNBLOCK:
		case SIG_SETMASK:
			break;
		default:
			return (EINVAL);
		}

		/*
		 * The assignments to self->ul_sigmask must be protected from
		 * signals.  The nuances of this code are subtle.  Be careful.
		 */
		block_all_signals(self);
		if (oset != NULL)
			saveset = self->ul_sigmask;
		switch (how) {
		case SIG_BLOCK:
			self->ul_sigmask.__sigbits[0] |= set->__sigbits[0];
			self->ul_sigmask.__sigbits[1] |= set->__sigbits[1];
			self->ul_sigmask.__sigbits[2] |= set->__sigbits[2];
			self->ul_sigmask.__sigbits[3] |= set->__sigbits[3];
			break;
		case SIG_UNBLOCK:
			self->ul_sigmask.__sigbits[0] &= ~set->__sigbits[0];
			self->ul_sigmask.__sigbits[1] &= ~set->__sigbits[1];
			self->ul_sigmask.__sigbits[2] &= ~set->__sigbits[2];
			self->ul_sigmask.__sigbits[3] &= ~set->__sigbits[3];
			break;
		case SIG_SETMASK:
			self->ul_sigmask.__sigbits[0] = set->__sigbits[0];
			self->ul_sigmask.__sigbits[1] = set->__sigbits[1];
			self->ul_sigmask.__sigbits[2] = set->__sigbits[2];
			self->ul_sigmask.__sigbits[3] = set->__sigbits[3];
			break;
		}
		delete_reserved_signals(&self->ul_sigmask);
		if (oset != NULL)
			*oset = saveset;
		restore_signals(self);
	}

	return (0);
}

#pragma weak _pthread_sigmask = pthread_sigmask
int
pthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
	return (thr_sigsetmask(how, set, oset));
}

#pragma weak _sigprocmask = sigprocmask
int
sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	int error;

	/*
	 * Guard against children of vfork().
	 */
	if (curthread->ul_vfork)
		return (__sigprocmask(how, set, oset));

	if ((error = thr_sigsetmask(how, set, oset)) != 0) {
		errno = error;
		return (-1);
	}

	return (0);
}

/*
 * Called at library initialization to set up signal handling.
 * All we really do is initialize the sig_lock rwlocks.
 * All signal handlers are either SIG_DFL or SIG_IGN on exec().
 * However, if any signal handlers were established on alternate
 * link maps before the primary link map has been initialized,
 * then inform the kernel of the new sigacthandler.
 */
void
signal_init()
{
	uberdata_t *udp = curthread->ul_uberdata;
	struct sigaction *sap;
	struct sigaction act;
	rwlock_t *rwlp;
	int sig;

	for (sig = 0; sig < NSIG; sig++) {
		rwlp = &udp->siguaction[sig].sig_lock;
		rwlp->rwlock_magic = RWL_MAGIC;
		rwlp->mutex.mutex_flag = LOCK_INITED;
		rwlp->mutex.mutex_magic = MUTEX_MAGIC;
		sap = &udp->siguaction[sig].sig_uaction;
		if (sap->sa_sigaction != SIG_DFL &&
		    sap->sa_sigaction != SIG_IGN &&
		    __sigaction(sig, NULL, &act) == 0 &&
		    act.sa_sigaction != SIG_DFL &&
		    act.sa_sigaction != SIG_IGN) {
			act = *sap;
			act.sa_flags &= ~SA_NODEFER;
			act.sa_sigaction = udp->sigacthandler;
			act.sa_mask = maskset;
			(void) __sigaction(sig, &act, NULL);
		}
	}
}

/*
 * Common code for cancelling self in _sigcancel() and pthread_cancel().
 * First record the fact that a cancellation is pending.
 * Then, if cancellation is disabled or if we are holding unprotected
 * libc locks, just return to defer the cancellation.
 * Then, if we are at a cancellation point (ul_cancelable) just
 * return and let _canceloff() do the exit.
 * Else exit immediately if async mode is in effect.
 */
void
do_sigcancel(void)
{
	ulwp_t *self = curthread;

	ASSERT(self->ul_critical == 0);
	ASSERT(self->ul_sigdefer == 0);
	self->ul_cancel_pending = 1;
	if (self->ul_cancel_async &&
	    !self->ul_cancel_disabled &&
	    self->ul_libc_locks == 0 &&
	    !self->ul_cancelable)
		pthread_exit(PTHREAD_CANCELED);
	set_cancel_pending_flag(self, 0);
}

/*
 * Set up the SIGCANCEL handler for threads cancellation,
 * needed only when we have more than one thread,
 * or the SIGAIOCANCEL handler for aio cancellation,
 * called when aio is initialized, in __uaio_init().
 */
void
setup_cancelsig(int sig)
{
	uberdata_t *udp = curthread->ul_uberdata;
	rwlock_t *rwlp = &udp->siguaction[sig].sig_lock;
	struct sigaction act;

	ASSERT(sig == SIGCANCEL || sig == SIGAIOCANCEL);
	lrw_rdlock(rwlp);
	act = udp->siguaction[sig].sig_uaction;
	lrw_unlock(rwlp);
	if (act.sa_sigaction == SIG_DFL ||
	    act.sa_sigaction == SIG_IGN)
		act.sa_flags = SA_SIGINFO;
	else {
		act.sa_flags |= SA_SIGINFO;
		act.sa_flags &= ~(SA_NODEFER | SA_RESETHAND | SA_RESTART);
	}
	act.sa_sigaction = udp->sigacthandler;
	act.sa_mask = maskset;
	(void) __sigaction(sig, &act, NULL);
}

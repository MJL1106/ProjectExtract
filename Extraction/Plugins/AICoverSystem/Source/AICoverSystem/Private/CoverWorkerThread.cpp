//// Copyright, (c) Sami Kangasmaa 2022


#include "CoverWorkerThread.h"

// ------------------------------------------------------------------------ //

DECLARE_STATS_GROUP(TEXT("AICoverSystemThreading"), STATGROUP_CoverThreading, STATCAT_Advanced);

// ------------------------------------------------------------------------ //

void FCoverWorkerTaskBase::RunSynchronously()
{
	check(!IsDone());
	check(IsInGameThread());

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("RunSynchronousTask"), STAT_RunSynchronousTask, STATGROUP_CoverThreading);

	// Run the task in game thread
	RunTask([]()
		{
			// Empty budget lambda - thread is not allowed to sleep while running in game thread.
		});
}

// ------------------------------------------------------------------------ //

// Helpers for chrono clocks
namespace CoverWorkerAtomicClock
{
	inline FCoverThreadAtomicClock GetClockNow()
	{
		const std::chrono::time_point<std::chrono::steady_clock> ClockNow = std::chrono::steady_clock::now();
		const std::chrono::nanoseconds SinceEpoch = std::chrono::duration_cast<std::chrono::nanoseconds>(ClockNow.time_since_epoch());
		return (FCoverThreadAtomicClock)SinceEpoch.count();
	}

	inline void SetAtomicClock(FCoverThreadAtomicClock& ClockRef, std::chrono::time_point<std::chrono::steady_clock> TimePoint)
	{
		const std::chrono::nanoseconds SinceEpoch = std::chrono::duration_cast<std::chrono::nanoseconds>(TimePoint.time_since_epoch());;
		ClockRef = (FCoverThreadAtomicClockType)SinceEpoch.count();
	}

	inline std::chrono::time_point<std::chrono::steady_clock> GetAtomicClock(const FCoverThreadAtomicClock& ClockRef)
	{
		const long long Clocks = (long long)ClockRef;
		const std::chrono::steady_clock::duration ClockDuration = std::chrono::nanoseconds(Clocks);
		return std::chrono::time_point<std::chrono::steady_clock>(ClockDuration);
	}

	inline bool NowExeedsClock(const FCoverThreadAtomicClock& ClockRef)
	{
		return GetClockNow() > ClockRef;
	}
}

FCoverWorkerThread::FCoverWorkerThread(FCoverThreadAtomicClockType InWorkBudget)
{
	TaskBudgetNanoSeconds = InWorkBudget;
	TaskBudgetClock = 0;
	Thread = FRunnableThread::Create(this, TEXT("CoverSystemWorker"));
}

FCoverWorkerThread::~FCoverWorkerThread()
{
	ensure(Thread == nullptr);
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
	}
	Thread = nullptr;
}

bool FCoverWorkerThread::Init()
{
	return true;
}

uint32 FCoverWorkerThread::Run()
{
	bShutdown = false;
	bWorkingTask = false;

	// Set the atomic budget clock to the time we are now at
	CoverWorkerAtomicClock::SetAtomicClock(TaskBudgetClock, std::chrono::steady_clock::now());

	// Lamda to evaluate if we have time budget to process.
	auto EvalTaskBudget = [this]()
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("WaitForTaskBudget"), STAT_WaitForTaskBudget, STATGROUP_CoverThreading);
			if (!bUpdateTaskBudgetClock && CoverWorkerAtomicClock::NowExeedsClock(TaskBudgetClock))
			{
				FPlatformProcess::ConditionalSleep([this]() -> bool
					{
						// Sleep unless we have budget or we are shutting down
						return bShutdown || bDisableBudget || bUpdateTaskBudgetClock;
					});
			}

			if (bUpdateTaskBudgetClock)
			{
				// Sleep exited -> set new time for task budget as requested
				bUpdateTaskBudgetClock = false;
				CoverWorkerAtomicClock::SetAtomicClock(TaskBudgetClock, std::chrono::steady_clock::now() + std::chrono::nanoseconds(TaskBudgetNanoSeconds));
			}
		};

	while (!bShutdown)
	{
		// Wait for work condition
		{
			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("WaitForWork"), STAT_WaitForWork, STATGROUP_CoverThreading);
			FPlatformProcess::ConditionalSleep([this]() -> bool
				{
					// Sleep until there is task to work with or this is going to be shutdown
					return bWorkingTask || bShutdown;
				});
		}

		// Work on task unless we are shutting down
		if (!bShutdown && Task)
		{
			// When task is done, set state to "idle"
			if (Task->IsDone())
			{
				Task = nullptr;
			}
			else
			{
				// Else work on the task
				DECLARE_SCOPE_CYCLE_COUNTER(TEXT("RunThreadedTask"), STAT_RunThreadedTask, STATGROUP_CoverThreading);
				Task->RunTask(EvalTaskBudget);
			}
		}
		else
		{
			// Abaddon the task in case this is going to be shutdown or just mark that the task has been completed
			bWorkingTask = false;
		}
	}

	return 0;
}


void FCoverWorkerThread::Exit()
{
	// TODO Cleanup (in this thread)
}

void FCoverWorkerThread::Stop()
{
	bShutdown = true;
}

void FCoverWorkerThread::KillThreadAndWait()
{
	check(Thread != nullptr);
	Thread->Kill(true);
	Thread = nullptr;
}

void FCoverWorkerThread::RunWork()
{
	check(IsInGameThread());

	// ask the worker thread to update task budget clock
	bUpdateTaskBudgetClock = true;
}

void FCoverWorkerThread::SetTaskFromGameThread(FCoverWorkerTaskBase* InTask)
{
	check(IsInGameThread());

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("SetTaskFromGameThread"), STAT_SetTaskFromGameThread, STATGROUP_CoverThreading);

	check(InTask);
	{
		// Lock while swapping task pointer
		FScopeLock TaskLock(&TaskAccessCriticalSection);
		Task = InTask;
	}

	// Atomic assignment
	bWorkingTask = true;
}

// ------------------------------------------------------------------------ //

FCoverWorkerTaskManager::FCoverWorkerTaskManager(double WorkBugetPerFrame)
{
	const FCoverThreadAtomicClockType BudgetNanoseconds = WorkBugetPerFrame * 1000000;
	StartWorkerThread(BudgetNanoseconds);
}

FCoverWorkerTaskManager::~FCoverWorkerTaskManager()
{
	ShutdownWorkerThread();
}

void FCoverWorkerTaskManager::StartWorkerThread(FCoverThreadAtomicClockType InTaskBudgetNanoSeconds)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("StartWorkerThread"), STAT_StartWorkerThread, STATGROUP_CoverThreading);

	check(!Thread.IsValid());
	Thread = MakeShared<FCoverWorkerThread>(InTaskBudgetNanoSeconds);
}

void FCoverWorkerTaskManager::ShutdownWorkerThread()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ShutdownWorkerThread"), STAT_ShutdownWorkerThread, STATGROUP_CoverThreading);

	if (Thread.IsValid())
	{
		Thread->KillThreadAndWait();
		Thread.Reset();
	}
}

void FCoverWorkerTaskManager::RunWork()
{
	if (!Thread.IsValid())
	{
		return;
	}

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("DispatchRunWork"), STAT_DispatchRunWork, STATGROUP_CoverThreading);

	// Queue more work to the worker thread
	if (!Thread->IsWorkingTask())
	{
		TaskBeingExecuted.Reset();
		if (TaskQueue.Num() > 0)
		{
			// Pick next task
			TaskBeingExecuted = TaskQueue[0];
			TaskQueue.RemoveAt(0);

			// No longer in queue
			TaskBeingExecuted->bIsInQueue = false;

			// Tell thread to work on the new task
			Thread->SetTaskFromGameThread(TaskBeingExecuted.Get());
		}
	}

	// Give more budget and run work on background thread
	Thread->RunWork();
}

void FCoverWorkerTaskManager::InternalAbandonTask(TSharedPtr<FCoverWorkerTaskBase> Task)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("AbandonTask"), STAT_AbandonTask, STATGROUP_CoverThreading);

	if (Task->IsInQueue())
	{
		const int32 RemoveCount = TaskQueue.Remove(Task);
		check(RemoveCount > 0);
		return;
	}

#if WITH_EDITOR
	check(!TaskQueue.Contains(Task));
#endif

	if (!TaskBeingExecuted.IsValid() || TaskBeingExecuted != Task)
	{
		// Task was never queued here or it has already been processed -> do nothing
		return;
	}

	// We are executing the task
	if (TaskBeingExecuted.IsValid() && TaskBeingExecuted == Task)
	{
		if (Task->IsDone() && !Thread->IsWorkingTask())
		{
			TaskBeingExecuted.Reset();
			return; // Task done and thread is idle -> we can reset the task being executed and return
		}
		else
		{
			// Else we must wait for worker thread to finish. NOTE This code path should be avoided as it blocks game thread!

			DECLARE_SCOPE_CYCLE_COUNTER(TEXT("WaitTaskCompletion"), STAT_WaitTaskCompletion, STATGROUP_CoverThreading);

			// Disable budgeting, allow to run the process without budget to complete the task as soon as possible
			Thread->SetEnableBudget(false);

			// Continue when worker thread stops working on the task, sleep game thread until then
			FPlatformProcess::ConditionalSleep([this]() -> bool
				{
					return !Thread->IsWorkingTask();
				});
			TaskBeingExecuted.Reset();

			// Enable budgeting again
			Thread->SetEnableBudget(true);
		}
	}
}
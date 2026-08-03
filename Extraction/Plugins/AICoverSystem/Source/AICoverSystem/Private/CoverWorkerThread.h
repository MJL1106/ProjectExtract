//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Async/AsyncWork.h"
#include "GenericPlatform/GenericPlatformProcess.h"

// Chrono lib to measure budget using high resolution clock
#include <chrono>

// Atomics to use lockless time budget sync
#include <atomic>


// ------------------------------------------------------- //

class FCoverWorkerTaskManager;

/**
* Abstract base for TCoverWorkerTask
*/
class FCoverWorkerTaskBase
{
private:

	friend class FCoverWorkerThread;
	friend class FCoverWorkerTaskManager;

	std::atomic<bool> bIsDone = false;
	bool bIsInQueue = false;

protected:

	virtual void RunTask(TFunctionRef<void ()> EvalThreadSleepClock) = 0;
	void Complete() { bIsDone = true; }

public:

	virtual ~FCoverWorkerTaskBase() {}

	/**
	 * Is this done with processing?
	 */
	inline bool IsDone() const { return bIsDone; }

	/**
	 * Is this in task manager queue.
	 * When in queue, this has not yet been dispatched to worker thread but will be in the future.
	 * Abandon task can safely called when this is in queue without locking game thread.
	 */
	inline bool IsInQueue() const { return bIsInQueue && !bIsDone; }

	/**
	 * Runs the task in game thread
	 */
	void RunSynchronously();
};

/*
* Task than can be queued to FCoverWorkerThreadManager to be processed by FCoverWorkerThread
*/
template<typename TTask>
class TCoverWorkerTask : public FCoverWorkerTaskBase
{
	TTask Task;

public:

	TCoverWorkerTask()
		: FCoverWorkerTaskBase(), Task()
	{
	}

	virtual ~TCoverWorkerTask() {}

	template <typename Arg0Type, typename... ArgTypes>
	TCoverWorkerTask(Arg0Type&& Arg0, ArgTypes&&... Args)
		: FCoverWorkerTaskBase(), Task(Forward<Arg0Type>(Arg0), Forward<ArgTypes>(Args)...)
	{
	}

	virtual void RunTask(TFunctionRef<void ()> EvalTaskBudget) final
	{
		Task.Run(EvalTaskBudget);
		Complete();
	}
};

/**
 * Stub class to use a base class for worker tasks
 */
class FCoverThreadedTask
{
public:

	virtual ~FCoverThreadedTask() {}

	/**
	 * Override to implement actual processing.
	 * Call EvalTaskBudget during the process to check remaining time budget.
	 * In case that worker thread has exeeded the time budget, the task is paused until we get more budget
	 */
	virtual void Run(TFunctionRef<void()> EvalTaskBudget) {}
};

// ------------------------------------------------------- //

using FCoverThreadAtomicClockType = int64;
using FCoverThreadAtomicClock = FCoverThreadAtomicClockType;

/**
 * Thread for async cover generation.
 */
class FCoverWorkerThread : public FRunnable
{
private:

	FCoverWorkerThread();

public:

	FCoverWorkerThread(FCoverThreadAtomicClockType InWorkBudget);
	virtual ~FCoverWorkerThread();

	virtual bool Init() override;

	// Starts running worker thread to wait for tasks to process
	virtual uint32 Run() override;

	// Runs in worker thread when the thread exits
	virtual void Exit() override;

	// Stops the thread, does not wait completion
	virtual void Stop() override;

	// Shutdown the thread and waits until it has exited gracefully
	void KillThreadAndWait();

public:

	// Runs work by giving more budget to work on the task
	void RunWork();

	// Is the thread working on task?
	inline bool IsWorkingTask() const { return bWorkingTask; }

	// Enabled/disabled budgeting
	inline void SetEnableBudget(bool bEnable) { bDisableBudget = !bEnable; }

	// Sets a new task to process from game thread
	void SetTaskFromGameThread(FCoverWorkerTaskBase* InTask);

private:

	// Actual thread
	FRunnableThread* Thread;

	// When this is true, while loop in Run function exits which shutdown the thread
	bool bShutdown = false;

	// Atomic value of budget clock. This is requested to be set by gamethread and used by worker to check if it has exeed the budget
	FCoverThreadAtomicClock TaskBudgetClock;

	// Budget that the thread has each time RunWork is called
	FCoverThreadAtomicClockType TaskBudgetNanoSeconds;

	// When game thread sets this to true, worker thread updates task budget
	std::atomic<bool> bUpdateTaskBudgetClock = false;

	// Is time budgeting enabled?
	std::atomic<bool> bDisableBudget = false;

	// Is the tread currently working with a given task?
	std::atomic<bool> bWorkingTask = false;

	// Task to work
	FCriticalSection TaskAccessCriticalSection;
	FCoverWorkerTaskBase* Task = nullptr;
};

// ------------------------------------------------------- //

/**
 * Manager that queue work for worker thread
 */
class FCoverWorkerTaskManager
{
private:

	// Disallow contruction without initializing time budget
	FCoverWorkerTaskManager() {}

	// Creates a FCoverWorkerThread and starts running it
	void StartWorkerThread(FCoverThreadAtomicClockType InTaskBudgetNanoSeconds);

	// Shuts down FCoverWorkerThread. Waits that it has exited gracefully
	void ShutdownWorkerThread();

public:

	FCoverWorkerTaskManager(double WorkBugetPerFrame);
	~FCoverWorkerTaskManager();

public:

	// Adds task to queue to be processed in worker thread
	template<typename TTaskType>
	void DispatchTask(TSharedPtr<TTaskType> Task)
	{
		check(Task.IsValid());
		TSharedPtr<FCoverWorkerTaskBase> TaskBase = StaticCastSharedPtr<FCoverWorkerTaskBase, TTaskType>(Task);
		TaskBase->bIsInQueue = true;
		TaskQueue.Add(TaskBase);
	}

	// Abandons given task. If it is still in queue, it is removed from it, otherwise we wait worker thread to finish it.
	template<typename TTaskType>
	void AbandonTask(TSharedPtr<TTaskType> Task)
	{
		check(Task.IsValid());
		TSharedPtr<FCoverWorkerTaskBase> TaskBase = StaticCastSharedPtr<FCoverWorkerTaskBase, TTaskType>(Task);
		InternalAbandonTask(TaskBase);
	}

	// Run work on worker thread, time budget in milliseconds
	void RunWork();

private:

	// Pointer to worker thread
	TSharedPtr<FCoverWorkerThread> Thread;

	// Queue where tasks are put when DispatchTask is called. Task manager moves task to TaskBeingExecuted when the worker thread is idle.
	TArray<TSharedPtr<FCoverWorkerTaskBase>, TInlineAllocator<128>> TaskQueue;

	// Task that the manager picked from TaskQueue for processing. This is given for worker thread to process.
	TSharedPtr<FCoverWorkerTaskBase> TaskBeingExecuted;

	void InternalAbandonTask(TSharedPtr<FCoverWorkerTaskBase> Task);
};
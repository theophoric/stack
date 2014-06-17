/*
 * Timer functionalities
 *
 *    Bernat Gaston <bernat.gaston@i2cat.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

#ifndef LIBRINA_TIMER_H
#define LIBRINA_TIMER_H

#ifdef __cplusplus

#include <ctime>
#include <map>

#include "librina/concurrency.h"

namespace rina {

/// Interface for tasks to be scheduled in a timer
class TimerTask {
public:
	virtual ~TimerTask(){};
	virtual void run() = 0;
};

class LockableMap : public Lockable {
public:
	LockableMap();
	~LockableMap() throw();
	void insert(std::pair<double, TimerTask*> pair);
	void clear();
	void runTasks();
	void cancelTask(TimerTask *task);
private:
	std::map<double, TimerTask*> tasks_;
};

/// Class that implements a timer which contains a thread
class Timer {
public:
	Timer();
	~Timer();
	void scheduleTask(TimerTask* task, double delay_ms);
	void cancelTask(TimerTask *task);
	void clear();
private:
	Thread *thread_;
	LockableMap lockableMap_;
};

}

#endif

#endif
/**
 * Copyright (c) 2014 DataStax
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _THREAD_POOL_HPP
#define _THREAD_POOL_HPP

#include <deque>
#include <vector>

#include <uv.h>

/**
 * Enumeration for describing the state of the thread pool
 */
enum ThreadPoolState {
  /**
   * Thread pool is initializing
   */
  THREAD_POOL_STATE_INITIALIZING,
  /**
   * Thread pool is initialized and running
   */
  THREAD_POOL_STATE_RUNNING,
  /**
   * Thread pool is shutting down (terminating)
   */
  THREAD_POOL_STATE_SHUTTING_DOWN,
  /**
   * Thread pool has terminated (stopped)
   */
  THREAD_POOL_STATE_TERMINATED
};

/**
 * Tasking class to execute jobs in the thread pool
 */
class Task {
private:
  /**
   * Function pointer to execute for the task
   */
  void (*function_)(void*);
  /**
   * Argument to pass in for the task
   */
  void* arg_;

public:
  /**
   * Constructor
   *
   * @param function Function to execute for the task
   * @param arg Function argument to pass for the task (default: NULL)
   */
  Task(void (*function)(void*), void* arg = NULL) : function_(function), arg_(arg) {}
  /**
   * Execute the task
   */
  void execute() {
    (*function_)(arg_);
  }
};

/**
 * This class will spawn threads up to a user specified amount in order to
 * properly maintain the consumer/producer design pattern and relinquish the
 * CPU properly to other system processes.
 */
class ThreadPool {
private:
  /**
   * Condition variable signaling thread queue
   */
  uv_cond_t threadCondition_;
  /**
   * Condition variable signaling task queue
   */
  uv_cond_t queueCondition_;
  /**
   * Mutex for operations in the thread pool
   */
  uv_mutex_t lock_;
  /**
   * Maximum number of threads
   */
  unsigned short size_;
  /**
   * Threads in the thread pool
   */
  std::vector<uv_thread_t> threads_;
  /**
   * Tasks to be executed in the thread pool
   */
  std::deque<Task*> tasks_;
  /**
   * State of the thread pool
   */
  ThreadPoolState state_;

  /**
   * Initialize the thread pool
   *
   * @return Error code for thread creation; 0 indicates threads created
   *         successfully
   */
  int initialize() {
    //Create the threads for the thread pool
    state_ = THREAD_POOL_STATE_INITIALIZING;
    for (int n = 0; n < size_; ++n) {
      //Create the thread and ensure it was created
      uv_thread_t thread;
      int errorCode = uv_thread_create(&thread, start, static_cast<void*>(this));
      if (errorCode != 0) {
        return errorCode;
      }
      threads_.push_back(thread);
    }
    state_ = THREAD_POOL_STATE_RUNNING;

    //Indicate thread pool was create successfully
    return 0;
  }

  /**
   * Start the thread pool thread so that it can accept tasks
   *
   * @param arg Thread pool pointer argument
   */
  void static start(void* arg) {
    static_cast<ThreadPool*>(arg)->loop();
  }

  /**
   * Thread loop to execute pending tasks in the tasks queue
   */
  void loop() {
    //Loop indefinitely for processing tasks
    while (true) {
      //Wait for a task
      uv_mutex_lock(&lock_);
      while ((state_ == THREAD_POOL_STATE_INITIALIZING || state_ == THREAD_POOL_STATE_RUNNING) && tasks_.empty()) {
        uv_cond_wait(&threadCondition_, &lock_);
      }

      //Determine if the condition was for shutting down the thread
      if (state_ == THREAD_POOL_STATE_SHUTTING_DOWN && tasks_.empty()) {
        uv_mutex_unlock(&lock_);
        return;
      }

      //Get the task and remove it from the list of tasks
      Task* task = tasks_.front();
      tasks_.pop_front();
      uv_mutex_unlock(&lock_);
      uv_cond_signal(&queueCondition_);

      //Execute the task and free resources when complete
      task->execute();
      delete task;
    }
  }

  /**
   * Terminate the thread pool by joining all threads in the pool and wait
   * for them to finish.
   */
  void terminate() {
    //Indicate the thread pool is shutting down
    uv_mutex_lock(&lock_);
    state_ = THREAD_POOL_STATE_SHUTTING_DOWN;
    uv_mutex_unlock(&lock_);
    uv_cond_signal(&threadCondition_);

    //Wait for all current tasks to complete
    uv_mutex_lock(&lock_);
    while (!tasks_.empty()) {
      uv_cond_wait(&queueCondition_, &lock_);
    }
    uv_mutex_unlock(&lock_);

    //Join all threads in the thread pool
    for (int n = 0; n < size_; ++n) {
      uv_thread_join(&threads_[n]);
      uv_cond_signal(&threadCondition_);
    }

    //Indicate the thread pool is terminated
    state_ = THREAD_POOL_STATE_TERMINATED;
  }

public:
  /**
   * Constructor
   *
   * @param size Maximum number of threads
   */
  ThreadPool(unsigned short size) : size_(size), state_(THREAD_POOL_STATE_TERMINATED) {
    uv_mutex_init(&lock_);
    uv_cond_init(&threadCondition_);
    uv_cond_init(&queueCondition_);
    initialize();
  }

  /**
   * Destructor
   */
  ~ThreadPool() {
    stop();
  }

  /**
   * Add a task to the thread pool.  If all threads in the thread pool are
   * processing tasks this method call will block.
   *
   * @param task Task to execute in thread pool; becomes invalidated after
   *             execution; freed
   */
  void addTask(Task* task) {
    //Make sure the task is valid
    if (task) {
      uv_mutex_lock(&lock_);

      //Ensure the task queue is not full
      while (!tasks_.empty()) {
        uv_cond_wait(&queueCondition_, &lock_);
      }

      //Add the task to the thread pool
      tasks_.push_back(task);
      uv_cond_signal(&threadCondition_);
      uv_mutex_unlock(&lock_);
    }
  }

  /**
   * Stop the thread pool and wait for all tasks to complete
   */
  void stop() {
    if (state_ != THREAD_POOL_STATE_TERMINATED) {
      terminate();
    }
  }

};

#endif //_THREAD_POOL_HPP

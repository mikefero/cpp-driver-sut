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
#ifndef _TIMER_HPP
#define _TIMER_HPP

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/time.h>
#endif

#define MICROSECONDS_IN_SECONDS 1000000.0
#define MILLISECONDS_IN_SECONDS 1000.0

/**
 * Time type for use with getting elapsed time
 */
enum TimeType {
  /**
   * Microseconds
   */
  TIME_TYPE_MICROSECONDS,
  /**
   * Milliseconds
   */
  TIME_TYPE_MILLISECONDS,
  /**
   * Seconds
   */
  TIME_TYPE_SECONDS
};

/**
 * High resolution cross platform timer for easily calculating elapsed time
 */
class Timer {
private:
  /**
   * Flag to determine if the timer is stopped
   */
  bool isStopped_;
#ifdef _WIN32
  /**
   * Frequency of ticks (ticks per second)
   */
  LARGE_INTEGER tickFrequency_;
  /**
   * Starting count
   */
  LARGE_INTEGER startTime_;
#else
  struct timeval startTime_;
#endif
  /**
   * Ending count
   */
#ifdef _WIN32
  LARGE_INTEGER endTime_;
#else
  struct timeval endTime_;
#endif

public:
  /**
   * Constructor
   */
  Timer() : isStopped_(false) {
    //Initialize the corresponding timer
#ifdef WIN32
    QueryPerformanceFrequency(&tickFrequency_);
    startTime_.QuadPart = 0;
    endTime_.QuadPart = 0;
#else
    startTime_.tv_sec = 0;
    startTime_.tv_usec = 0;
    endTime_.tv_sec = 0;
    endTime_.tv_usec = 0;
#endif
  }

  /**
   * Start the timer
   */
  void start() {
    //Reset the timer flag
    isStopped_ = false;

    //Get the start time from the corresponding timer
#ifdef WIN32
    QueryPerformanceCounter(&startTime_);
#else
    gettimeofday(&startTime_, NULL);
#endif
  }

  void stop() {
    //Indicate the timer is stopped
    isStopped_ = true;

    //Stop the corresponding timer
#ifdef WIN32
    QueryPerformanceCounter(&endTime_);
#else
    gettimeofday(&endTime_, NULL);
#endif
  }

  /**
   * Get the elapsed time
   *
   * @param type Type of elapsed time (default: TIME_TYPE_MICROSECONDS)
   * @return Elapsed time in microseconds, milliseconds, or seconds
   */
  double getElapsedTime(const TimeType type = TIME_TYPE_MICROSECONDS) {
    //Determine the type of return for the elapsed time
    double elapsedTimeCalculation = 1;
    if (type == TIME_TYPE_MILLISECONDS) {
      elapsedTimeCalculation = 1 / MILLISECONDS_IN_SECONDS;
    } else if (type == TIME_TYPE_SECONDS) {
      elapsedTimeCalculation = 1 / MICROSECONDS_IN_SECONDS;
    }

    //Calculate the elapsed time
#ifdef WIN32
    if(!isStopped_) {
      QueryPerformanceCounter(&endTime_);
    }
    double startTimeInReturnType = startTime_.QuadPart * (MICROSECONDS_IN_SECONDS / tickFrequency_.QuadPart);
    double endTimeInReturnType = endTime_.QuadPart * (MICROSECONDS_IN_SECONDS / tickFrequency_.QuadPart);
#else
    if(!isStopped_) {
      gettimeofday(&endTime_, NULL);
    }
    double startTimeInReturnType = (startTime_.tv_sec * MICROSECONDS_IN_SECONDS) + startTime_.tv_usec;
    double endTimeInReturnType = (endTime_.tv_sec * MICROSECONDS_IN_SECONDS) + endTime_.tv_usec;
#endif

    //Return the elapsed time in requested type
    return (endTimeInReturnType - startTimeInReturnType) * elapsedTimeCalculation;
  }

};

#endif //_TIMER_HPP

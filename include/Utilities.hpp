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
#ifndef _UTILITIES_HPP
#define _UTILITIES_HPP
//Use the appropriate stack trace unwinder
#ifndef _WIN32
#	define UNW_LOCAL_ONLY
#	include <libunwind.h>
#	include <unistd.h>
#else
#	include <StackWalker.h>
#	include <windows.h>
#endif
#include <stdio.h>
#include <string>
#include <sstream>

#include "Timer.hpp"

#define ONE_MILLISECOND_SLEEP 1
#define ONE_SECOND_SLEEP 1000
#define EPOCH_DELTA 116444736000000000llu //Delta since 1970/01/01 (Win32 epoch to Unix epoch)

#define TO_STRING(x) dynamic_cast<std::ostringstream&>((std::ostringstream() << std::dec << x )).str()
#define TO_CSTR(x) TO_STRING(x).c_str()

#ifdef _WIN32
/**
 * Overriding class to convert the output of StackWalker into a stack trace
 * variable
 */
class StackWalkerToString : public StackWalker {
private:
  /**
   * Pointer to the stack trace for StackWalker output
   */
  std::stringstream* stackTrace_;

protected:
  /**
   * Implement the output function to append the global stack trace
   * variable for the utilities function
   *
   * @param szText Current text content in the stack trace
   */
  virtual void OnOutput(LPCSTR szText) {
    (*stackTrace_) << std::string(szText);
    StackWalker::OnOutput(szText);
  }

public:
  StackWalkerToString(std::stringstream* stackTrace) : stackTrace_(stackTrace), StackWalker(RetrieveNone) {}
};
#endif

/**
 * Utilities class for performing common operations
 */
class Utilities {
private:
  /**
   * Disable the default constructor
   */
  Utilities() {}

  /**
   * Disable the destructor
   */
  ~Utilities() {}

public:
  /**
   * Get the stack trace from the current application context
   *
   * @return Stack trace at the current application context
   */
  static std::string getStackTrace() {
    //Create a string for the stack trace result
    std::stringstream stackTraceResult;

#ifndef _WIN32
    //Get the context and cursor for the stack trace
    unw_context_t context;
    unw_cursor_t cursor;
    unw_getcontext(&context);
    unw_init_local(&cursor, &context);

    //Unwind each frame in the cursor to get the stack trace
    while (unw_step(&cursor) > 0) {
      //Get the function/procedure name
      unw_word_t byteOffset;
      char procedureName[1024] = { '\0' };
      unw_get_proc_name(&cursor, procedureName, 1024, &byteOffset);

      //Get the instruction pointer (program counter)
      unw_word_t instructionPointer;
      unw_get_reg(&cursor, UNW_REG_IP, &instructionPointer);

      //Get the stack pointer
      unw_word_t stackPointer;
      unw_get_reg(&cursor, UNW_REG_SP, &stackPointer);

      char buffer[2048] = { '\0' };
      sprintf(buffer, "\t%p : (%s+0x%x) [%p]\n", (void *) instructionPointer, procedureName, (unsigned int) byteOffset, (void *) instructionPointer);
      stackTraceResult << buffer;

      //TODO: Add a mechanism to use addr2line to determine filename and line number in stack trace
      //printf("\tat %s (%s:%d)\n", filename, procedureName, lineNumber);
    }
#else
    //Get the stack trace
    StackWalkerToString stackWalker(&stackTraceResult);
    stackWalker.ShowCallstack();
#endif

    //Return the result
    return stackTraceResult.str();
  }

  /**
   * Cross platform millisecond timestamp
   *
   * @return Local timestamp (epoch) in milliseconds
   */
  static unsigned long long getTimeStamp() {
    //Get the current local time
#ifndef _WIN32
    struct timeval localTime = { 0 };
    gettimeofday(&localTime, NULL);
#else
    FILETIME localTime;
    GetSystemTimeAsFileTime(&localTime);
#endif

    //Calculate the millisecond timestamp (Unix epoch)
    unsigned long long localTimeStampInMilliseconds = 0l;
#ifndef _WIN32
    localTimeStampInMilliseconds = static_cast<unsigned long long>((localTime.tv_sec * 1000) + (localTime.tv_usec / 1000));
#else
    localTimeStampInMilliseconds |= static_cast<unsigned long long>(localTime.dwHighDateTime);
    localTimeStampInMilliseconds <<= 32llu;
    localTimeStampInMilliseconds |= static_cast<unsigned long long>(localTime.dwLowDateTime);
    localTimeStampInMilliseconds -= EPOCH_DELTA;
    localTimeStampInMilliseconds /= 10000;
#endif

    //Return the local timestamp
    return localTimeStampInMilliseconds;
  }

  /**
   * Cross platform millisecond granularity sleep
   *
   * @param milliseconds Time in milliseconds to sleep
   */
  static void sleep(unsigned int milliseconds) {
#ifndef _WIN32
    //Convert the milliseconds into a proper timespec structure
    struct timespec requested = { 0 };
    time_t seconds = static_cast<int>(milliseconds / 1000);
    long int nanoseconds = static_cast<long int>((milliseconds - (seconds * 1000)) * 1000000);

    //Assign the requested time and perform sleep
    requested.tv_sec = seconds;
    requested.tv_nsec = nanoseconds;
    while (nanosleep(&requested, &requested) == -1) {
      continue;
    }
#else
    Sleep(milliseconds);
#endif
  }
};

#endif //_UTILITIES_HPP

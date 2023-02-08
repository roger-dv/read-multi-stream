/* child-process-tracking.cpp

Copyright 2018 Roger D. Voss

Created  by roger-dv on 04/21/2018.
Modified by roger-dv on 02/07/2023.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <wait.h>
#include <cstring>
#include <unistd.h>
#include <future>
#include "signal-handling.h"
#include "child-process-tracking.h"

using signal_handling::quit_flag;

static std::timed_mutex qm;
static std::atomic_int child_process_count = {0};
static std::unordered_map<pid_t, std::tuple<int, int>> child_processes;

static void track_child_process_completion();

void start_tracking_child_process(int const child_pid, int const stdout_wr_fd, int const stderr_wr_fd) {
  int curr_child_process_count = 0;
  {
    std::lock_guard<std::timed_mutex> lk(qm);
    curr_child_process_count = child_process_count.fetch_add(1);
    child_processes.emplace(std::make_pair(child_pid, std::make_tuple(stdout_wr_fd, stderr_wr_fd)));
  }
  if (curr_child_process_count <= 0) {
    track_child_process_completion();
  }
}

static void track_child_process_completion() {
  using child_process_completion_proc_t = std::function<bool(int)>;

  static auto const waitid_on_forked_children = [](child_process_completion_proc_t child_process_completion_proc) {
    volatile bool done = false;
    siginfo_t info {0};
    do {
      if (waitid(P_ALL, 0, &info, WEXITED|WSTOPPED) == 0) {
        child_process_count--;
        done = child_process_completion_proc(info.si_pid);
      } else {
        const auto rc = errno;
        switch(rc) {
          case 0:
            break;
          case ECHILD:
            if (quit_flag != 0 || done) return; // flag when non-zero indicates was signaled to terminate
            fprintf(stderr, "TRACE: waitid(): %s\n", strerror(rc));
            done = true;
            break;
          case EINTR: // waidid() was interrupted by a signal so
            fprintf(stderr, "INFO: waitid(): %s\n", strerror(rc));
            return;   // exit the lambda and the thread context it's executing in
          default:
            fprintf(stderr, "ERROR: waitid() returned on error: %s\n", strerror(rc));
        }
      }
    } while (!done);
  };

  // lambda is a completion routine invoked when a forked child process terminates
  static auto const child_process_completion = [](int const child_pid) -> bool {
    int stdout_wr_fd = -1, stderr_wr_fd = -1;

    std::unique_lock<std::timed_mutex> lk(qm, std::defer_lock);
    std::chrono::milliseconds wait_time{250};
    bool done;
    do {
      if ((done = lk.try_lock_for(wait_time))) {
        auto const fd_pair = child_processes[child_pid];
        stdout_wr_fd = std::get<0>(fd_pair);
        stderr_wr_fd = std::get<1>(fd_pair);
        child_processes.erase(child_pid);
        lk.unlock();
      }
    } while(!done);

    close(stdout_wr_fd);
    close(stderr_wr_fd);

    fprintf(stderr, "DEBUG: terminating child process pid(%d) -> stdout wr fd close(%d); stderr wr fd close(%d)\n",
            child_pid, stdout_wr_fd, stderr_wr_fd);

    return quit_flag != 0;
  };

  std::promise<void> prom;
  auto sf = prom.get_future().share();

  std::thread waitid_on_forked_children_thrd{
      std::function<void()>([sf] {
        sf.wait(); // Waits for calling thread to send notification to proceed
        waitid_on_forked_children(child_process_completion);
      })};

  waitid_on_forked_children_thrd.detach();

  // Send notification to async waitid_on_forked_children() lambda to proceed
  prom.set_value();
}

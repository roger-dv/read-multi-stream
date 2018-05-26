/* uncompress-stream.cpp

Copyright 2018 Roger D. Voss

Created by roger-dv on 4/21/18.

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
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <memory>
#include <fcntl.h>
#include "util.h"
#include "child-process-tracking.h"
#include "uncompress-stream.h"

std::tuple<int, int> get_uncompressed_stream(const char * const filepath) {
  enum PIPES : short { READ = 0, WRITE = 1 };

  int stdout_pipes[2] { -1, -1 };
  auto rc = pipe(stdout_pipes); int line_nbr = __LINE__;
  if (rc == -1) {
    fprintf(stderr, "ERROR: %d: %s() -> pipe(): %s\n", line_nbr, __FUNCTION__, strerror(errno));
    return std::tuple<int, int>{-1, -1};
  }

  auto const cleanup_pipes = [](int p[]) {
    if (p != nullptr) {
      for(int i = PIPES::READ; i <= PIPES::WRITE; i++) {
        auto const fd = p[i];
        if (fd == -1) continue;
        close(fd);
        p[i] = -1;
      }
    }
  };
  // cleanup stdout pipes if return from function due to error
  std::unique_ptr<int[],decltype(cleanup_pipes)> sp_stdout_pipes(stdout_pipes, cleanup_pipes);

  int stderr_pipes[2] { -1, -1 };
  rc = pipe(stderr_pipes); line_nbr = __LINE__;
  if (rc == -1) {
    fprintf(stderr, "ERROR: %d: %s() -> pipe(): %s\n", line_nbr, __FUNCTION__, strerror(errno));
    return std::tuple<int, int>{-1, -1};
  }

  // cleanup stderr pipes if return from function due to error
  std::unique_ptr<int[],decltype(cleanup_pipes)> sp_stderr_pipes(stderr_pipes, cleanup_pipes);

  auto const fd_stdout = stdout_pipes[PIPES::READ];
  auto const fd_stderr = stderr_pipes[PIPES::READ];

  auto const stdout_flags = fcntl(fd_stdout, F_GETFL, 0);
  rc = fcntl(fd_stdout, F_SETFD, stdout_flags | FD_CLOEXEC); line_nbr = __LINE__;
  if (rc == -1) {
    fprintf(stderr, "ERROR: %d: %s() -> fcntl(fd: %d): %s\n", line_nbr, __FUNCTION__, fd_stdout, strerror(errno));
    return std::tuple<int, int>{-1, -1};
  }

  auto const stderr_flags = fcntl(fd_stderr, F_GETFL, 0);
  rc = fcntl(fd_stderr, F_SETFD, stderr_flags | FD_CLOEXEC); line_nbr = __LINE__;
  if (rc == -1) {
    fprintf(stderr, "ERROR: %d: %s() -> fcntl(fd: %d): %s\n", line_nbr, __FUNCTION__, fd_stderr, strerror(errno));
    return std::tuple<int, int>{-1, -1};
  }

  auto const pid = fork(); line_nbr = __LINE__;
  if (pid == -1) {
    fprintf(stderr, "ERROR: %d: %s() -> fork(): %s\n", line_nbr, __FUNCTION__, strerror(errno));
    return std::tuple<int, int>{-1, -1};
  }
  if (pid == 0) {
    // child process
    // redirect the stdout stream to write pipe
    fflush(stdout);
    fflush(stderr);
    auto const stdout_fd = stdout_pipes[PIPES::WRITE];
    auto const stderr_fd = stderr_pipes[PIPES::WRITE];
    while ((dup2(stdout_fd, STDOUT_FILENO) == -1) && (errno == EINTR)) {}
    while ((dup2(stderr_fd, STDERR_FILENO) == -1) && (errno == EINTR)) {}
    dbg_dump_file_desc_flags(stdout_fd);
    dbg_dump_file_desc_flags(stderr_fd);
    close(stdout_fd);
    close(stderr_fd);

    // invoke the gzip program
    static const char gzip[] = "gzip";
    fprintf(stderr, "DEBUG: child process pid(%d) -> writing fd: %d; exec of: '%s -dc %s'\n",
            getpid(), stdout_fd, gzip, filepath);
    execlp(gzip, gzip, "-dc", filepath, static_cast<char*>(nullptr)); line_nbr = __LINE__;

    // only executes following statements if the execlp() call fatally failed
    fprintf(stderr, "ERROR: %d: %s() -> execlp(): %s\n", line_nbr, __FUNCTION__, strerror(errno));
    _exit(1);
  }

  // only the parent process, after the fork() call, arrives at these statements

  // restore original flag settings
  fcntl(fd_stdout, F_SETFD, stdout_flags);
  fcntl(fd_stderr, F_SETFD, stderr_flags);

  start_tracking_child_process(pid /*child pid */, stdout_pipes[PIPES::WRITE], stderr_pipes[PIPES::WRITE]);

  fprintf(stderr, "DEBUG: parent process pid(%d) -> reading stdout fd from: %d and stderr fd from: %d : \"%s\"\n",
          getpid(), fd_stdout, fd_stderr, filepath);

  stdout_pipes[PIPES::READ] = stdout_pipes[PIPES::WRITE] -1;
  sp_stdout_pipes.release();
  stderr_pipes[PIPES::READ] = stderr_pipes[PIPES::WRITE] -1;
  sp_stderr_pipes.release();

  return std::tuple<int, int>{fd_stdout, fd_stderr};
}

/* read-multi-strm.cpp

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
#include <cstring>
#include "read-multi-strm.h"
#include "signal-handling.h"

read_multi_stream::read_multi_stream(u_int const read_buf_size) : read_buf_size(read_buf_size) {
  fds.reserve(initial_fds_vec_capacity);

  fprintf(stderr, "DEBUG: read_buf_size: %ul\n", read_buf_size);
}

read_multi_stream::read_multi_stream(int const stdout_fd, int const stderr_fd, u_int const read_buf_size)
    : read_buf_size(read_buf_size)
{
  fds.reserve(initial_fds_vec_capacity);
  read_buf_ctx rbc_stdout(stdout_fd, read_buf_size);
  read_buf_ctx rbc_stderr(stderr_fd, read_buf_size);
  auto item = std::make_tuple(std::move(rbc_stdout), std::move(rbc_stderr));
  fds.emplace_back(std::move(item));

  fprintf(stderr, "DEBUG: stdout_fd: %d, stderr_fd: %d\nDEBUG: read_buf_size: %ul\\n",
          stdout_fd, stderr_fd, read_buf_size);
}

read_multi_stream::read_multi_stream(std::tuple<int, int> fd_pair, u_int const read_buf_size)
    : read_buf_size(read_buf_size)
{
  fds.reserve(initial_fds_vec_capacity);
  int const stdout_fd = std::get<0>(fd_pair);
  int const stderr_fd = std::get<1>(fd_pair);
  read_buf_ctx rbc_stdout(stdout_fd, read_buf_size);
  read_buf_ctx rbc_stderr(stderr_fd, read_buf_size);
  auto item = std::make_tuple(std::move(rbc_stdout), std::move(rbc_stderr));
  fds.emplace_back(std::move(item));

  fprintf(stderr, "DEBUG: stdout_fd: %d, stderr_fd: %d\nDEBUG: read_buf_size: %ul\\n",
          stdout_fd, stderr_fd, read_buf_size);
}

read_multi_stream::read_multi_stream(std::initializer_list<std::tuple<int, int>> init, u_int const read_buf_size)
    : read_buf_size(read_buf_size)
{
  fds.reserve(initial_fds_vec_capacity);
  for(auto const &fd_pair : init) {
    int const stdout_fd = std::get<0>(fd_pair);
    int const stderr_fd = std::get<1>(fd_pair);

    fprintf(stderr, "DEBUG: stdout_fd: %d, stderr_fd: %d\n", stdout_fd, stderr_fd);

    read_buf_ctx rbc_stdout(stdout_fd, read_buf_size);
    read_buf_ctx rbc_stderr(stderr_fd, read_buf_size);
    auto item = std::make_tuple(std::move(rbc_stdout), std::move(rbc_stderr));
    fds.emplace_back(std::move(item));
  }
  fprintf(stderr, "DEBUG: read_buf_size: %u\n", read_buf_size);
}

read_multi_stream& read_multi_stream::operator +=(std::tuple<int, int> fd_pair) {
  int const stdout_fd = std::get<0>(fd_pair);
  int const stderr_fd = std::get<1>(fd_pair);

  fprintf(stderr, "DEBUG: stdout_fd: %d, stderr_fd: %d\n", stdout_fd, stderr_fd);

  read_buf_ctx rbc_stdout(stdout_fd, read_buf_size);
  read_buf_ctx rbc_stderr(stderr_fd, read_buf_size);
  auto item = std::make_tuple(std::move(rbc_stdout), std::move(rbc_stderr));
  fds.emplace_back(std::move(item));

  return *this;
}

read_multi_stream::~read_multi_stream() {
  fprintf(stderr, "DEBUG: << %p->%s()\n", this, __FUNCTION__);
}

int read_multi_stream::wait_for_io(std::vector<int> &active_fds) {
  int rc = EXIT_SUCCESS;
  fd_set rfd_set{0};
  struct timeval tv{5, 0}; // Wait up to five seconds

  FD_ZERO(&rfd_set);
  for(auto const & item : this->fds) {
    auto const &rbc_stdout = std::get<0>(item);
    auto const &rbc_stderr = std::get<1>(item);
    FD_SET(rbc_stdout.orig_fd, &rfd_set); // select on the original file descriptor
    FD_SET(rbc_stderr.orig_fd, &rfd_set); // select on the original file descriptor
  }

  int highest_fd = -1;
  for(int i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, &rfd_set) && i > highest_fd) {
      highest_fd = i;
    }
  }

  while(!signal_handling::interrupted()) {
    /* Watch input stream to see when it has input. */
    auto ret_val = select(highest_fd + 1, &rfd_set, nullptr, nullptr, &tv);
    int line_nbr = __LINE__;
    /* Don't rely on the value of tv now! */
    if (ret_val == -1) {
      const auto ec = errno;
      if (ec == EINTR) {
        return ec; // signal for exiting condition detected so bail out immediately
      }
      fprintf(stderr, "ERROR: %d: %s() -> select(): %s\n", line_nbr, __FUNCTION__, strerror(ec));
      return EXIT_FAILURE;
    }

    if (ret_val > 0) {
      active_fds.clear();
      bool any_ready = false;
      for(auto const & item : this->fds) {
        auto const &rbc_stdout = std::get<0>(item);
        auto const &rbc_stderr = std::get<1>(item);
        if (FD_ISSET(rbc_stdout.orig_fd, &rfd_set)) {
          active_fds.push_back(rbc_stdout.orig_fd);
          any_ready = true;
        }
        if (FD_ISSET(rbc_stderr.orig_fd, &rfd_set)) {
          active_fds.push_back(rbc_stderr.orig_fd);
          any_ready = true;
        }
      }
      if (any_ready) {
        fputs("DEBUG: Data is available now:\n", stderr);
      }
    }
  }

  return rc;
}

void test() {
  fprintf(stderr, "DEBUG: >> %s()\n", __FUNCTION__);
  auto fd_1 = dup(STDIN_FILENO);
  auto fd_2 = dup(STDIN_FILENO);
  auto fd_3 = dup(STDIN_FILENO);
  auto fd_4 = dup(STDIN_FILENO);
  auto fd_5 = dup(STDIN_FILENO);
  auto fd_6 = dup(STDIN_FILENO);
  auto fd_7 = dup(STDIN_FILENO);
  auto fd_8 = dup(STDIN_FILENO);

  read_multi_stream rms({std::make_tuple(fd_1, fd_2), std::make_tuple(fd_3, fd_4), std::make_tuple(fd_5, fd_6)}, 512);
  rms += std::make_tuple(fd_7, fd_8);

  int count = 0;
  for(auto const & item : rms.fds) {
    count++;
    auto const &rbc_stdout = std::get<0>(item);
    auto const &rbc_stderr = std::get<1>(item);
    fprintf(stderr,
            "DEBUG: this: %p, stdout_fd: %03d dup: %03d read_buffer: %p\n"
            "       this: %p, stderr_fd: %03d dup: %03d read_buffer: %p\n",
            &rbc_stdout, rbc_stdout.orig_fd, rbc_stdout.dup_fd, (void *) rbc_stdout.read_buffer,
            &rbc_stderr, rbc_stderr.orig_fd, rbc_stderr.dup_fd, (void *) rbc_stderr.read_buffer);
  }
  fprintf(stderr, "DEBUG: << %s(), count: %d\n", __FUNCTION__, count);
}

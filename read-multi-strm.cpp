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
#include <cassert>
#include "read-multi-strm.h"
#include "signal-handling.h"

#define DBG_VERIFY 1

read_multi_stream::read_multi_stream(u_int const read_buf_size) : read_buf_size(read_buf_size) {
  fprintf(stderr, "DEBUG: read_buf_size: %u\n", read_buf_size);
}

read_buf_ctx* read_multi_stream::lookup_mutable_read_buf_ctx(int fd) const {
  auto search = fd_map.find(fd);
  if (search != fd_map.end()) {
    auto const &sp_entry = search->second;
    if (sp_entry->get_stdout_fd() == fd) {
      return &sp_entry->stdout_ctx;
    }
    if (sp_entry->get_stderr_fd() == fd) {
      return &sp_entry->stderr_ctx;
    }
  }
  return nullptr;
}

void read_multi_stream::verify_added_elem(const read_buf_ctx_pair &elem,
                                          int stdout_fd, int stderr_fd, u_int read_buf_size)
{
#if DBG_VERIFY
  assert(&fd_map.at(stdout_fd)->stdout_ctx == &elem.stdout_ctx);
  assert(&fd_map.at(stderr_fd)->stderr_ctx == &elem.stderr_ctx);
  assert(elem.stdout_ctx.orig_fd == stdout_fd);
  assert(elem.stdout_ctx.read_buf_limit == (read_buf_size - 1));
  assert(elem.stderr_ctx.orig_fd == stderr_fd);
  assert(elem.stderr_ctx.read_buf_limit == (read_buf_size - 1));
  fprintf(stderr,
          "DEBUG: added vector element read_buf_ctx_pair: %p\n"
          "DEBUG: stdout_fd: %d, stderr_fd: %d, read_buf_size: %u\n",
          &elem, elem.stdout_ctx.orig_fd, elem.stderr_ctx.orig_fd, read_buf_size);
#endif
}

void read_multi_stream::add_entry_to_map(int stdout_fd, int stderr_fd, u_int read_buf_size) {
  auto sp_shared_item = std::make_shared<read_buf_ctx_pair>(stdout_fd, stderr_fd, read_buf_size);
  fd_map.insert(std::make_pair(stdout_fd, sp_shared_item));
  fd_map.insert(std::make_pair(stderr_fd, sp_shared_item));
  auto &elem = *sp_shared_item.get();
  elem.stderr_ctx.is_stderr_flag = true;
#if DBG_VERIFY
  verify_added_elem(elem, stdout_fd, stderr_fd, read_buf_size);
#endif

}

read_multi_stream::read_multi_stream(int const stdout_fd, int const stderr_fd, u_int const read_buf_size)
    : read_buf_size(read_buf_size)
{
  add_entry_to_map(stdout_fd, stderr_fd, read_buf_size);
}

read_multi_stream::read_multi_stream(std::tuple<int, int> fd_pair, u_int const read_buf_size)
    : read_buf_size(read_buf_size)
{
  int const stdout_fd = std::get<0>(fd_pair);
  int const stderr_fd = std::get<1>(fd_pair);
  add_entry_to_map(stdout_fd, stderr_fd, read_buf_size);
}

read_multi_stream::read_multi_stream(std::initializer_list<std::tuple<int, int>> init, u_int const read_buf_size)
    : read_buf_size(read_buf_size)
{
  for(auto const &fd_pair : init) {
    int const stdout_fd = std::get<0>(fd_pair);
    int const stderr_fd = std::get<1>(fd_pair);

    fprintf(stderr, "DEBUG: stdout_fd: %d, stderr_fd: %d\n", stdout_fd, stderr_fd);

    add_entry_to_map(stdout_fd, stderr_fd, read_buf_size);
  }
  fprintf(stderr, "DEBUG: read_buf_size: %u\n", read_buf_size);
}

read_multi_stream& read_multi_stream::operator +=(std::tuple<int, int> fd_pair) {
  int const stdout_fd = std::get<0>(fd_pair);
  int const stderr_fd = std::get<1>(fd_pair);

  fprintf(stderr, "DEBUG: stdout_fd: %d, stderr_fd: %d\n", stdout_fd, stderr_fd);

  add_entry_to_map(stdout_fd, stderr_fd, read_buf_size);

  return *this;
}

read_multi_stream::~read_multi_stream() {
  fprintf(stderr, "DEBUG: << (%p)->%s()\n", this, __FUNCTION__);
}

int read_multi_stream::wait_for_io(std::vector<int> &active_fds) {
  int rc = 0;
  fd_set rfd_set{0};
  struct timeval tv{5, 0}; // Wait up to five seconds

  FD_ZERO(&rfd_set);
  for(auto const &kv : fd_map) {
    FD_SET(kv.first, &rfd_set); // select on the original file descriptor
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
      return -1;
    }

    if (ret_val > 0) {
      active_fds.clear();
      bool any_ready = false;
      for(auto const &kv : fd_map) {
        if (FD_ISSET(kv.first, &rfd_set)) {
          active_fds.push_back(kv.first);
          any_ready = true;
        }
      }
      if (any_ready) {
        fputs("DEBUG: Data is available now:\n", stderr);
        break;
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

  std::unordered_map<int, const read_buf_ctx*> seen{};
  int count = 0;
  for(auto const &kv : rms.fd_map) {
    count++;
    auto const &rbc_stdout = kv.second->stdout_ctx;
    auto const &rbc_stderr = kv.second->stderr_ctx;
    auto search1 = seen.find(rbc_stdout.orig_fd);
    if (search1 != seen.end()) continue;
    seen.insert(std::make_pair(rbc_stdout.orig_fd, &rbc_stdout));
    auto search2 = seen.find(rbc_stderr.orig_fd);
    if (search2 == seen.end()) {
      seen.insert(std::make_pair(rbc_stderr.orig_fd, &rbc_stderr));
    }
    fprintf(stderr,
            "DEBUG: this: %p, stdout_fd: %03d, dup: %03d, read_buffer: %p\n"
            "       this: %p, stderr_fd: %03d, dup: %03d, read_buffer: %p\n",
            &rbc_stdout, rbc_stdout.orig_fd, rbc_stdout.dup_fd, (void *) rbc_stdout.read_buffer,
            &rbc_stderr, rbc_stderr.orig_fd, rbc_stderr.dup_fd, (void *) rbc_stderr.read_buffer);
  }
  fprintf(stderr, "DEBUG: << %s(), count: %d\n", __FUNCTION__, count);
}
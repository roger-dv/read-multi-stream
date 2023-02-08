/* read-multi-strm.cpp

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
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <cassert>
#include "read-multi-strm.h"
#include "signal-handling.h"

#define DBG_VERIFY 1

// This declaration appears in assert.h and is part of stdc library definition. However,
// it is dependent upon conditional compilation controlled via NDEBUG macro definition.
// Here we are using it regardless of whether is debug or non-debug build, so declaring
// it extern explicitly.
extern "C" void __assert (const char *__assertion, const char *__file, int __line)
__THROW __attribute__ ((__noreturn__));

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
                                          int stdout_fd, int stderr_fd, u_int read_buffer_size)
{
#if DBG_VERIFY
  assert(&fd_map.at(stdout_fd)->stdout_ctx == &elem.stdout_ctx);
  assert(&fd_map.at(stderr_fd)->stderr_ctx == &elem.stderr_ctx);
  assert(elem.stdout_ctx.orig_fd == stdout_fd);
  assert(elem.stdout_ctx.read_buf_limit == (read_buffer_size - 1));
  assert(elem.stderr_ctx.orig_fd == stderr_fd);
  assert(elem.stderr_ctx.read_buf_limit == (read_buffer_size - 1));
  fprintf(stderr,
          "DEBUG: added vector element read_buf_ctx_pair: %p\n"
          "DEBUG: stdout_fd: %d, stderr_fd: %d, read_buffer_size: %u\n",
          &elem, elem.stdout_ctx.orig_fd, elem.stderr_ctx.orig_fd, read_buffer_size);
#endif
}

void read_multi_stream::add_entry_to_map(int stdout_fd, int stderr_fd, u_int read_buffer_size) {
  auto sp_shared_item = std::make_shared<read_buf_ctx_pair>(stdout_fd, stderr_fd, read_buffer_size);
  fd_map.insert(std::make_pair(stdout_fd, sp_shared_item));
  fd_map.insert(std::make_pair(stderr_fd, sp_shared_item));
  auto &elem = *sp_shared_item.get();
  elem.stderr_ctx.is_stderr_flag = true;
#if DBG_VERIFY
  verify_added_elem(elem, stdout_fd, stderr_fd, read_buffer_size);
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

int read_multi_stream::poll_for_io(std::vector<pollfd_result> &active_fds) {
  active_fds.clear();
  const struct timespec timeout_ts{ 3, 0 };
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);

  // stack-allocate array of struct pollfd and zero initialize its memory space
  const auto fds_count = fd_map.size();
  if (fds_count == 0) return -1; // no file descriptors remaining to poll on
  const auto pollfd_array_size = sizeof(struct pollfd) * fds_count;
  auto const pollfd_array = (struct pollfd*) alloca(pollfd_array_size);
  memset(pollfd_array, 0, pollfd_array_size);

  // set fds to be polled as entries in pollfd_array
  // (requesting event notice of when ready to read)
  auto it = fd_map.begin();
  unsigned int i = 0, j = 0;
  for(; i < fds_count; i++) {
    if (it == fd_map.end()) break; // when reach iteration end of fd_map
    auto &rfd = pollfd_array[i];
    rfd.fd = it->first;
    rfd.events = POLLIN;
    j++;
    it++; // advance fd_map iterator to next element of fd_map
  }
  if (i != j && i != fds_count) {
    __assert("number of struct pollfd entries assigned to not equal to fd_map entries count", __FILE__, __LINE__);
  }

  while(!signal_handling::interrupted()) {
    /* Watch input streams to see when have input. */
    int line_nbr = __LINE__ + 1;
    auto ret_val = ppoll(pollfd_array, fds_count, &timeout_ts, &sigset);
    if (ret_val == -1) {
      const auto ec = errno;
      if (ec == EINTR) {
        return ec; // signal interruption detected so bail out immediately
      }
      fprintf(stderr, "ERROR: %d: %s() -> ppoll(): %s\n", line_nbr, __FUNCTION__, strerror(ec));
      return -1;
    }

    if (ret_val > 0) {
      bool any_ready = false;
      for(i = 0; i < fds_count; i++) {
        const auto &rfd = pollfd_array[i];
        if (rfd.revents != 0) {
          active_fds.push_back({.fd = rfd.fd, .revents = rfd.revents});
          any_ready = true;
        }
      }
      if (any_ready) {
        fputs("DEBUG: Data is available now:\n", stderr);
        break;
      }
    }
  }

  return 0;
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
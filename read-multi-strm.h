/* read-multi-strm.h

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
#ifndef READ_MULTI_STRM_H
#define READ_MULTI_STRM_H

#include <sys/types.h>
#include <tuple>
#include <vector>
#include <unordered_map>
#include "read-buf-ctx.h"


u_int const default_read_buf_size = 128;
u_int const initial_fds_vec_capacity = 16;

struct read_buf_ctx_pair {
  read_buf_ctx stdout_ctx;
  read_buf_ctx stderr_ctx;
  // deletes default constructor and copy constructor
  read_buf_ctx_pair() = delete;
  read_buf_ctx_pair(const read_buf_ctx_pair&) = delete;
  // supports one constructor taking arguments and a default move constructor
  read_buf_ctx_pair(read_buf_ctx_pair && rbcp_rval) noexcept = default;
  // this constructor is used for emplace construction into vector
  read_buf_ctx_pair(int stdout_fd, int stderr_fd, u_int read_buf_size)
      : stdout_ctx(stdout_fd, read_buf_size), stderr_ctx(stderr_fd, read_buf_size) {}
  // supports move-only assignment semantics
  read_buf_ctx_pair & operator=(const read_buf_ctx_pair &) = delete;
  read_buf_ctx_pair & operator=(read_buf_ctx_pair && rbcp) noexcept {
    this->stdout_ctx = std::move(rbcp.stdout_ctx);
    this->stderr_ctx = std::move(rbcp.stderr_ctx);
    return *this;
  }
  ~read_buf_ctx_pair() = default;
};

class read_multi_stream final {
  std::vector<read_buf_ctx_pair> fds;
  std::unordered_map<int, std::tuple<size_t, read_buf_ctx*>> fd_map;
  u_int const read_buf_size;
  friend class read_buf_ctx;
  friend void test();
public:
  read_multi_stream(const read_multi_stream &) = delete;
  read_multi_stream& operator=(const read_multi_stream &) = delete;
  explicit read_multi_stream(u_int read_buf_size = default_read_buf_size);
  read_multi_stream(int stdout_fd, int stderr_fd, u_int read_buf_size = default_read_buf_size);
  explicit read_multi_stream(std::tuple<int, int> fd_pair, u_int read_buf_size = default_read_buf_size);
  read_multi_stream(std::initializer_list<std::tuple<int, int>> init, u_int read_buf_size = default_read_buf_size);
  read_multi_stream& operator +=(std::tuple<int, int> fd_pair);
  read_multi_stream(read_multi_stream &&rms) noexcept : read_buf_size(0) { fds = std::move(rms.fds); }
  read_multi_stream& operator=(read_multi_stream &&rms) noexcept {
    fds = std::move(rms.fds);
    *const_cast<u_int*>(&read_buf_size) = rms.read_buf_size;
    return *this;
  }
  ~read_multi_stream();
  int wait_for_io(std::vector<int> &active_fds);
  size_t size() const { return fds.size(); }
  read_buf_ctx& get_read_buf_ctx(int fd) const {
    auto entry = fd_map.at(fd);
    auto p_entry = std::get<1>(entry);
    assert(p_entry != nullptr);
    return *p_entry;
  }
private:
  void add_to_fd_map(size_t index, read_buf_ctx_pair &elem);
  void verify_added_elem(size_t index, const read_buf_ctx_pair &elem, int stdout_fd, int stderr_fd, u_int read_buf_size);
};

#endif //READ_MULTI_STRM_H

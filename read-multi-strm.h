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
#include "read-buf-ctx.h"


u_int const default_read_buf_size = 128;
u_int const initial_fds_vec_capacity = 16;

class read_multi_stream final {
  std::vector<std::tuple<read_buf_ctx, read_buf_ctx>> fds;
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
};

#endif //READ_MULTI_STRM_H

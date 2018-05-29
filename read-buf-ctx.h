/* read-buf-ctx.h

Copyright 2018 Roger D. Voss

Created by roger-dv on 4/12/18.

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
#ifndef READ_BUF_CTX_H
#define READ_BUF_CTX_H

#include <cstdio>
#include <memory>
#include <string>
#include <functional>

using fd_t = class read_buf_ctx;

class read_buf_ctx final {
private:
  const int orig_fd;
  int dup_fd = -1;
  char * const read_buffer;
  const u_int read_buf_limit;
  u_int pos = 0;
  bool eof_flag = false;
  bool is_stderr_flag = false;
  friend void test();
  friend class read_multi_stream;
public:
  read_buf_ctx() = delete;
  read_buf_ctx(const read_buf_ctx &) = delete;
  read_buf_ctx& operator=(const read_buf_ctx &) = delete;
  explicit read_buf_ctx(int input_fd, u_int read_buf_size = 128);
  read_buf_ctx(read_buf_ctx &&rbc) noexcept : orig_fd(-1), read_buffer(nullptr), read_buf_limit(0) {
    *this = std::move(rbc);
  }
  read_buf_ctx& operator=(read_buf_ctx &&rbc) noexcept;
  ~read_buf_ctx();
  bool is_valid_init() const { return orig_fd >= 0 && dup_fd != -1; }
  bool is_stderr_stream() const { return is_stderr_flag; }
  int read_line_on_ready(std::string &output_strbuf);
private:
  bool find_next_eol(char *pLF, const char *end, std::string &output_strbuf);
  friend void close_dup_fd(fd_t *p);
  using fd_close_dup_t = std::function<void(fd_t *)>;
  std::unique_ptr<fd_t, fd_close_dup_t> sp_input_fd;
  using rbc_free_buf_t = std::function<void(void*)>;
  std::unique_ptr<void, rbc_free_buf_t> sp_read_buf_rb;
};

#endif //READ_BUF_CTX_H

/* util.cpp

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
#include <cxxabi.h>
#include <cassert>
#include <cstring>
#include <memory>
#include "util.h"

std::string get_unmangled_name(std::string_view mangled_name) {
  auto const free_nm = [](char *p) { std::free(p); };
  int status;
  auto const pnm = abi::__cxa_demangle(mangled_name.data(), nullptr, nullptr, &status);
  std::unique_ptr<char, decltype(free_nm)> nm_sp(pnm, free_nm);
  return std::string(nm_sp.get());
}

int get_file_desc(FILE *stream, const int line_nbr) {
  assert(stream != nullptr);
  const int fd = fileno(stream);
  if (fd == -1) {
    fprintf(stderr, "ERROR: %d: %s() -> fileno(): %s\n", line_nbr, __FUNCTION__, strerror(errno));
    return -1;
  }
  return fd;
}

bool valid_file(std::string_view /*filepath*/) {
  return true;
}

bool has_ending(std::string_view full_str, std::string_view ending, int &offset, int line_nbr) {
  offset = -1;
  bool rslt = false;
  if (full_str.length() >= ending.length()) {
    offset = static_cast<int>(full_str.length() - ending.length());
    rslt = (0 == full_str.compare(static_cast<unsigned long>(offset), ending.length(), ending));
  }
  if (!rslt) {
    fprintf(stderr, "ERROR: %d: %s() -> \"%s\" is not a %s compressed file\n",
            line_nbr, __FUNCTION__, full_str.data(), ending.data());
  }
  return rslt;
}

bool dbg_echo_input_source(int const fd, int const line_nbr) {
#if 0
  auto const fstream = fdopen(fd, "r");
  if (fstream == nullptr) {
    fprintf(stderr, "ERROR: %d: %s() -> fdopen(fd: %d): %s\n", line_nbr, __FUNCTION__, fd, strerror(errno));
    dbg_dump_file_desc_flags(fd);
  } else {
    dbg_dump_file_desc_flags(get_file_desc(fstream, line_nbr));
    char *line = nullptr;
    size_t len = 0;
    ssize_t read;
    while ((read = getline(&line, &len, fstream)) != -1) {
      fputs(line, stdout);
    }
    free(line);
    fclose(fstream);
  }
  return true;
#elif 0
  char buf[512];
  auto const buf_size = sizeof(buf)/sizeof(buf[0]);
  for(;;) {
    auto const count = read(fd, buf, buf_size);
    if (count > 0) {
      write(STDOUT_FILENO, buf, count);
      continue;
    } else if (count == -1) {
      fprintf(stderr, "ERROR: %d: %s() -> read(fd: %d): %s\n", line_nbr, __FUNCTION__, fd, strerror(errno));
      dbg_dump_file_desc_flags(fd);
    }
    break;
  }
  close(fd);
  return true;
#else
  dbg_dump_file_desc_flags(fd);
  return false;
#endif
}

void dbg_dump_file_desc_flags(int const fd) {
#if 0
  int const flags = fcntl(fd, F_GETFL, 0);
  // test for these flag bits: O_APPEND, O_ASYNC, O_DIRECT, O_NOATIME, and O_NONBLOCK
  fprintf(stderr,
          "DEBUG: file descriptor %d flags: 0x%08X\n"
          "DEBUG: O_APPEND   0x%08X status: %d\n"
          "DEBUG: O_ASYNC    0x%08X status: %d\n"
          "DEBUG: O_DIRECT   0x%08X status: %d\n"
          "DEBUG: O_NOATIME  0x%08X status: %d\n"
          "DEBUG: O_NONBLOCK 0x%08X status: %d\n",
          fd, flags,
          O_APPEND, static_cast<int>((flags & O_APPEND) != 0),
          O_ASYNC, static_cast<int>((flags & O_ASYNC) != 0),
          O_DIRECT, static_cast<int>((flags & O_DIRECT) != 0),
          O_NOATIME, static_cast<int>((flags & O_NOATIME) != 0),
          O_NONBLOCK, static_cast<int>((flags & O_NONBLOCK) != 0)
  );
#endif
}

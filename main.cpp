/* main.cpp

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
#include <cstring>
#include <unistd.h>
#include <cxxabi.h>
#include <cassert>
#include <set>
#include "signal-handling.h"
#include "util.h"
#include "uncompress-stream.h"
#include "read-multi-strm.h"

static std::tuple<int, const char*> write_to_output_stream(read_buf_ctx &rbc, FILE* output_stream);
//static void do_on_exit();

int main(int argc, char **argv) {
  try {
    signal_handling::set_signals_handler();

//    atexit(do_on_exit);

    u_int read_buf_size = 64; // default

    auto const stdin_fd = get_file_desc(stdin, __LINE__); // default
    if (stdin_fd == -1) {
      fprintf(stderr, "ERROR: unexpected error - unable to obtain stdin file descriptor\n");
      return EXIT_FAILURE;
    }
    dbg_dump_file_desc_flags(stdin_fd);

    read_multi_stream rms;

    if (argc > 1) {
      for(int i = 1; i < argc; i++) {
        const char * const arg = argv[i];
        fprintf(stderr, "DEBUG: arg: \"%s\"\n", arg);
        switch(arg[0]) {
          case '-': {
            if (strcasecmp(arg, "-bufsize") == 0) {
              if (++i < argc) {
                const char * const nbr_str = argv[i];
                try {
                  const auto nbr = std::stoul(nbr_str);
                  if (nbr <= UINT16_MAX) {
                    read_buf_size = (u_int) nbr;
                  } else {
                    fprintf(stderr, "WARN: %lu was out of range for maximum allowed (%u bytes) read buffer size\n", nbr, UINT16_MAX);
                  }
                } catch (const std::invalid_argument &ex) {
                  fprintf(stderr, "WARN: '%s' was not a valid positive integer expressing read buffer size\n", nbr_str);
                } catch (const std::out_of_range &ex) {
                  fprintf(stderr, "WARN: '%s' was out of range as a positive integer expressing read buffer size\n", nbr_str);
                }
              } else {
                fprintf(stderr, "ERROR: expected numeric value following command option '%s'\n", arg);
                return EXIT_FAILURE;
              }
            } else {
              fprintf(stderr, "ERROR: unknown command option '%s'\n", arg);
              return EXIT_FAILURE;
            } 
            break;
          }
          default: { // assume argument is a file path
            if (valid_file(arg)) {
              if (has_ending(arg, ".gz", __LINE__)) {
                auto const fd_pair = get_uncompressed_stream(arg);
                auto const fd_stdout = std::get<0>(fd_pair);
                auto const fd_stderr = std::get<1>(fd_pair);
                if (fd_stdout != -1) {
                  rms += std::make_tuple(fd_stdout, fd_stderr);
                  continue;
                }
              }
            }
            return EXIT_FAILURE;
          }
        }
      }
    }

    fprintf(stderr, "DEBUG: using %u bytes as read buffer size\n", read_buf_size);

    std::set<FILE*> fps{stdout, stderr};
    bool is_ctrl_z_registered = false;
    std::vector<int> fds{};
    const char* msg = "failure";
    int rc;
    while((rc = rms.wait_for_io(fds)) == 0 && fps.size() > 0) {
      for(auto const fd : fds) {
        auto &rbc = rms.get_read_buf_ctx(fd);
        if (rbc.is_valid_init()) {
          if (!is_ctrl_z_registered) {
            const auto curr_thrd = pthread_self();
            signal_handling::register_ctrl_z_handler([curr_thrd](int sig) {
//            fprintf(stderr, "DEBUG: << %s(sig: %d)\n", "signal_interrupt_thread", sig);
              pthread_kill(curr_thrd, sig);
            });
            is_ctrl_z_registered = true;
          }
          auto const output_stream = rbc.is_stderr_stream() ? stderr : stdout;
          fps.erase(output_stream);
          auto rtn = write_to_output_stream(rbc, output_stream);
          rc = std::get<0>(rtn);
          msg = std::get<1>(rtn);
        } else {
          fputs("ERROR: initialization failure of read_buf_ctx object", stderr);
          rc = EXIT_FAILURE;
          break;
        }
      }
    }
    fprintf(stderr, "INFO: program exiting with status: [%d] %s\n", rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE, msg);
  } catch(...) {
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    fprintf(stderr, "process %d terminating due to unhandled exception of type %s", getpid(), ex_nm.c_str());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static std::tuple<int, const char*> write_to_output_stream(read_buf_ctx &rbc, FILE * const output_stream) {
  const char* msg = "";
  std::string str_buf;
  str_buf.reserve(16);

  auto const check_output_io = [&msg](int rc) -> bool {
    if (rc == -1) {
      fprintf(stderr,"ERROR: failed writing to output stream: [%d] %s\n", rc, strerror(errno));
      msg = "failure";
      return false;
    }
    return true;
  };

  int rc = 0, rc2 = 0;
  for(long input_line = 1; !signal_handling::interrupted(); input_line++) {
    fprintf(stderr, "DEBUG: string buffer capacity: %lu, string length: %lu\nDEBUG: read line (%05lu) of input:\n",
            str_buf.capacity(), str_buf.length(), input_line);
    str_buf.clear();
    rc = rbc.read_line_on_ready(str_buf);
    if (rc != EXIT_SUCCESS || strcasecmp(str_buf.c_str(), "quit") == 0) {
      switch(rc) {
        case EXIT_SUCCESS:
          msg = "successful";
          break;
        case EXIT_FAILURE:
          msg = "failure";
          break;
        case EINTR:
          msg = strerror(rc);
          fprintf(stderr, "INFO: read-input thread interrupted; status: [%d] %s\n", rc, msg);
          continue;
        case EOF:
          msg = "end of input stream";
          break;
        default:
          msg = "";
      }
      if (!str_buf.empty()) {
        rc2 = fputs(str_buf.c_str(), output_stream);
        if (check_output_io(rc2)) {
          rc2 = fputc('\n', output_stream);
          if (check_output_io(rc2)) {
            rc2 = fflush(output_stream);
          }
        }
      }
      rc = (rc == EXIT_SUCCESS || rc == EOF) && rc2 != -1 ? EXIT_SUCCESS : EXIT_FAILURE;
      return std::make_tuple(rc, msg);
    }
    rc = fputs(str_buf.c_str(), output_stream);
    if (check_output_io(rc)) {
      rc = fputc('\n', output_stream);
      check_output_io(rc);
    }
    if (rc == -1) return std::make_tuple(EXIT_FAILURE, msg);
  }
  rc = fflush(output_stream);
  check_output_io(rc);
  fputs("DEBUG: breaking out of read-line input loop due to interrupt signal\n", stderr);
  return std::make_tuple(rc != -1 ? EXIT_SUCCESS : EXIT_FAILURE, msg);
}

/*
static void do_on_exit() {
  extern void test();
  test();
  fputs("DEBUG: call to test completed\n", stderr);
}
*/

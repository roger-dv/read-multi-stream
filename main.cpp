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
#include <set>
#include <cassert>
#include <map>
#include <future>
#include "signal-handling.h"
#include "util.h"
#include "uncompress-stream.h"
#include "read-multi-strm.h"

using write_result = std::tuple<int, int, std::string>;
static write_result write_to_output_stream(int fd, read_buf_ctx &rbc, FILE* output_stream, std::string &str_buf);
//static void do_on_exit();

using file_stream_unique_ptr = std::unique_ptr<FILE, std::function<decltype(fclose)>>;

/**
 * Data structure that holds context for output files associated
 * to a given input file. A given input file will decompress into
 * a corresponding output file (of the same name but minus ".gz"
 * suffix), and there will be a file of the same name with a ".err"
 * suffix for recording any errors encountered in processing that
 * input file.
 *
 * FILE stream pointers are established per each of these output
 * files; they are wrapped with unique smart pointers to where they
 * will be properly closed when this context object instance is
 * destructed.
 */
struct output_streams {
  const std::string output_file;
  const std::string output_err_file;
  file_stream_unique_ptr output_stream;
  file_stream_unique_ptr output_err_stream;
  std::string output_str_buf{};
  std::string output_err_str_buf{};

  // the only valid way to construct this object
  output_streams(std::basic_string<char> &&output_file_param,
                 std::basic_string<char> &&output_err_file_param,
                 file_stream_unique_ptr  &&output_stream_param,
                 file_stream_unique_ptr  &&output_err_stream_param) :
      output_file(std::move(output_file_param)), output_err_file(std::move(output_err_file_param)),
      output_stream(std::move(output_stream_param)), output_err_stream(std::move(output_err_stream_param))
  {
    output_str_buf.reserve(16);
    output_err_str_buf.reserve(16);
  }
  output_streams() = delete;
  output_streams(const output_streams &&) = delete;
  output_streams& operator =(const output_streams &&) = delete;
  output_streams(const output_streams &) = delete;
  output_streams& operator =(const output_streams &) = delete;
  ~output_streams() = default;
};


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

    // holds the output context of all input files (hence "multi stream" moniker)
    read_multi_stream rms;

    // file descriptors to the output (stdout and stderr) of processing
    // a given input file are used as keys to this map. The map can be
    // dereferenced via a file descriptor (when it is ready to be read)
    // and the output files context retrieved
    std::map<int, std::shared_ptr<output_streams>> output_streams_map;

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
              int offset;
              const std::string input_file{arg};
              if (has_ending(input_file, ".gz", offset, __LINE__)) {
                auto const fd_pair = get_uncompressed_stream(arg);
                auto const fd_stdout = std::get<0>(fd_pair);
                auto const fd_stderr = std::get<1>(fd_pair);
                if (fd_stdout != -1) {
                  rms += std::make_tuple(fd_stdout, fd_stderr);

                  std::string output_file{input_file.substr(0, static_cast<unsigned long>(offset))};
                  std::string output_err_file{output_file + ".err"};
                  fprintf(stderr, "output file: \"%s\" output error file: \"%s\"\n",
                          output_file.c_str(), output_err_file.c_str());

                  auto output_stream = fopen(output_file.c_str(), "wb");
                  static const char * const errfmt = "ERROR: failed opening output file \"%s\":\n\t%s\n";
                  if (output_stream == nullptr) {
                    fprintf(stderr, errfmt, output_file.c_str(), strerror(errno));
                    return EXIT_FAILURE;
                  }
                  file_stream_unique_ptr sp_output_stream{output_stream, &fclose};

                  auto output_err_stream = fopen(output_err_file.c_str(), "wb");
                  if (output_err_stream == nullptr) {
                    fprintf(stderr, errfmt, output_err_file.c_str(), strerror(errno));
                    return EXIT_FAILURE;
                  }
                  file_stream_unique_ptr sp_output_err_stream{output_err_stream, &fclose};

                  auto sp_shared_item = std::make_shared<output_streams>(std::move(output_file),
                                                                         std::move(output_err_file),
                                                                         std::move(sp_output_stream),
                                                                         std::move(sp_output_err_stream));
                  output_streams_map.insert(std::make_pair(fd_stdout, sp_shared_item));
                  output_streams_map.insert(std::make_pair(fd_stderr, sp_shared_item));

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

    bool is_ctrl_z_registered = false;
    std::vector<int> fds{};
    std::vector<std::future<write_result>> futures{};
    std::string msg{"failure"};
    int rc;
    while((rc = rms.wait_for_io(fds)) == 0 && !output_streams_map.empty()) {
      futures.clear();
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
          auto search = output_streams_map.find(fd);
          if (search == output_streams_map.end()) {
            fputs("WARN: a ready-to-read file descriptor failed to dereference an output context - skipping\n", stderr);
            continue;
          }
          auto const &sp_output_ctx = search->second;
          futures.emplace_back(
              std::async(
                  [fd, &rbc, sp_output_ctx] {
                    auto const output_stream = rbc.is_stderr_stream() ? sp_output_ctx->output_err_stream.get()
                                                                      : sp_output_ctx->output_stream.get();
                    std::string &str_buf = rbc.is_stderr_stream() ? sp_output_ctx->output_str_buf
                                                                  : sp_output_ctx->output_err_str_buf;
                    return write_to_output_stream(fd, rbc, output_stream, str_buf);
                  } ));
        } else {
          fputs("ERROR: initialization failure of read_buf_ctx object", stderr);
          rc = EXIT_FAILURE;
          break;
        }
      }
      for(auto &fut : futures) {
        auto rtn = fut.get();
        auto fd = std::get<0>(rtn);
        rc = std::get<1>(rtn);
        if (rc != EXIT_SUCCESS) {
          msg = std::move(std::get<2>(rtn));
          // removed dereference key for output context per this file descriptor
          output_streams_map.erase(fd); // TODO: need to come up with way to detect eof
        }
      }
    }
    fprintf(stderr, "INFO: program exiting with status: [%d] %s\n", rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE, msg.c_str());
  } catch(...) {
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    fprintf(stderr, "process %d terminating due to unhandled exception of type %s", getpid(), ex_nm.c_str());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static write_result write_to_output_stream(int fd, read_buf_ctx &rbc, FILE *const output_stream, std::string &str_buf) {
  const char* msg = "";

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
      if (rc != EXIT_SUCCESS && rc != EOF && rc2 != -1) {
        rc = EXIT_FAILURE;
      }
      return std::make_tuple(fd, rc, msg);
    }
    rc = fputs(str_buf.c_str(), output_stream);
    if (check_output_io(rc)) {
      rc = fputc('\n', output_stream);
      check_output_io(rc);
    }
    if (rc == -1) return std::make_tuple(fd, EXIT_FAILURE, msg);
  }
  rc = fflush(output_stream);
  check_output_io(rc);
  fputs("DEBUG: breaking out of read-line input loop due to interrupt signal\n", stderr);
  return std::make_tuple(fd, rc != -1 ? EXIT_SUCCESS : EXIT_FAILURE, msg);
}

/*
static void do_on_exit() {
  extern void test();
  test();
  fputs("DEBUG: call to test completed\n", stderr);
}
*/

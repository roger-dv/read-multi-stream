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
#include <map>
#include <future>
#include "signal-handling.h"
#include "util.h"
#include "uncompress-stream.h"
#include "read-multi-strm.h"


//static void do_on_exit();

using file_stream_unique_ptr = std::unique_ptr<FILE, std::function<decltype(fclose)>>;

/**
 * Data structure that holds context for an output stream associated
 * to a given input stream. A given input file will gzip decompress
 * into a corresponding output file stream (of the same name but minus
 * ".gz" suffix), and there will be a file of the same name with a
 * ".err" suffix for recording any errors encountered in processing
 * that input file.
 *
 * A C FILE stream is established per each of these output files. The
 * context data structure here is instantiated to track the read-access
 * consumption of one of these output streams (one is instantiated per
 * each of the two output file streams).
 *
 * A FILE stream pointer is wrapped with a unique smart pointer so that
 * it will be properly closed when an instance of this context object
 * is destructed. (The fclose() function will close the FILE stream.)
 */
struct output_stream_context {
  const std::string output_file;
  file_stream_unique_ptr output_stream;
  long output_stream_line{1};
  std::string output_str_buf{};

  // the only valid way to construct this object
  output_stream_context(std::string &&output_file_rval, file_stream_unique_ptr &&output_stream_rval) noexcept :
      output_file(std::move(output_file_rval)),
      output_stream(std::move(output_stream_rval))
  {
    output_str_buf.reserve(16);
  }
  output_stream_context() = delete;
  output_stream_context(output_stream_context &&) = delete;
  output_stream_context &operator=(const output_stream_context &&) = delete;
  output_stream_context(const output_stream_context &) = delete;
  output_stream_context &operator=(const output_stream_context &) = delete;
  ~output_stream_context() = default;
};

/**
 * NOTE: Casting of enums on assignments and switch statements necessary for
 * working around bug in CLion code inspections where incorrectly complains
 * about taking enum type from integer. (Bug should be fixed in CLion 2018.1)
 */
using WR = enum class WRITE_RESULT : char {
  NO_OP = 0, SUCCESS, FAILURE, INTERRUPTED, END_OF_FILE
};

using read_multi_result = std::tuple<int, WRITE_RESULT>;

using output_streams_context_map_t = std::map<int, std::shared_ptr<output_stream_context>>;

static read_multi_result read_on_ready(bool &is_ctrl_z_registered, read_multi_stream &rms,
                                       output_streams_context_map_t &output_streams_map);

using write_result = std::tuple<int, int, WRITE_RESULT>;

using write_to_output_callback = std::function<int(FILE *, const char *, const char *)>;

static write_result
write_to_output_stream(int fd, read_buf_ctx &rbc, FILE *output_stream, long &input_line, std::string &str_buf,
                       const write_to_output_callback &writer);

static const char *write_result_str(WRITE_RESULT result) {
  switch (result) {
    case (WR) WR::SUCCESS:
      return "success";
    case (WR) WR::FAILURE:
      return "failure";
    case (WR) WR::INTERRUPTED:
      return "thread interrupted";
    case (WR) WR::END_OF_FILE:
      return "end of input stream";
    default:
      return "";
  }
}

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
    // a given input file are used as keys to this map. Can dereference
    // the map via a file descriptor (when it is ready to be read) and
    // the output files context retrieved
    output_streams_context_map_t output_streams_map;

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

                  output_streams_map.insert(
                      std::make_pair(fd_stdout,
                                     std::make_shared<output_stream_context>(std::move(output_file),
                                                                             std::move(sp_output_stream))));
                  output_streams_map.insert(
                      std::make_pair(fd_stderr,
                                     std::make_shared<output_stream_context>(std::move(output_err_file),
                                                                             std::move(sp_output_err_stream))));

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

    auto const result = read_on_ready(is_ctrl_z_registered, rms, output_streams_map);
    auto const ec = std::get<0>(result);
    auto const wr = std::get<1>(result);
    const std::string msg{write_result_str(wr)};

    auto const rtn = ec == 0 || wr == (WR) WR::END_OF_FILE ? EXIT_SUCCESS : EXIT_FAILURE;

    fprintf(stderr, "INFO: program exiting with status: [%d] %s\n", rtn, msg.c_str());
    return rtn;
  } catch(...) {
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    fprintf(stderr, "process %d terminating due to unhandled exception of type %s", getpid(), ex_nm.c_str());
    return EXIT_FAILURE;
  }
}

static read_multi_result read_on_ready(bool &is_ctrl_z_registered, read_multi_stream &rms,
                                       output_streams_context_map_t &output_streams_map)
{
  std::vector<pollfd_result> fds{};
  std::vector<std::future<write_result>> futures{};
  WRITE_RESULT wr{(WR) WR::FAILURE};
  int rc{0};

  while (rms.size() > 0 && !signal_handling::interrupted() && ((rc = rms.poll_for_io(fds)) == 0 || rc == EINTR)) {
    futures.clear();
    for(const auto& pollfd : fds) {
      const auto fd = pollfd.fd;
      auto const prbc = rms.get_mutable_read_buf_ctx(fd);
      assert(prbc != nullptr); // lookup should never dereference to a null pointer
      if (prbc == nullptr) continue;
      if (prbc->is_valid_init()) {
        if (!is_ctrl_z_registered) { // a one-time-only initialization
          const auto curr_thread = pthread_self();
          signal_handling::register_ctrl_z_handler([curr_thread](int sig) -> void {
//            fprintf(stderr, "DEBUG: << %s(sig: %d)\n", "signal_interrupt_thread", sig);
            pthread_kill(curr_thread, sig);
          });
          is_ctrl_z_registered = true;
        }
        auto search = output_streams_map.find(fd); // look up the file descriptor to find its output stream context
        if (search == output_streams_map.end()) {
          fputs("WARN: a ready-to-read file descriptor failed to dereference an output context - skipping\n", stderr);
          continue;
        }
        auto output_stream_ctx = search->second;
        // invoke the write to the output stream context in an asynchronous manner, using a future to get the outcome
        futures.emplace_back(
            std::async(std::launch::async,
                       [fd, prbc, output_stream_ctx] {
                         auto const output_stream = output_stream_ctx->output_stream.get();
                         auto &input_line = output_stream_ctx->output_stream_line;
                         auto &str_buf = output_stream_ctx->output_str_buf;
                         // the writer callback accepts line of text and writes it to output stream;
                         // however, could do application logic processing on text line here as well
                         return write_to_output_stream(fd, *prbc, output_stream, input_line, str_buf,
                                                       [](FILE *os, const char *str, const char *nl) -> int {
                                                         auto rc2 = fputs(str, os);
                                                         if (rc2 != -1 && nl != nullptr) {
                                                           rc2 = fputs(nl, os);
                                                         }
                                                         return rc2;
                                                       });
                       }));
      } else {
        // A failed initialization detected for the read_buf_ctx (the input source), so remove
        // dereference key for the input and output stream context items per this file descriptor
        rms.remove(fd); // input context
        output_streams_map.erase(fd); // output context
        fputs("ERROR: initialization failure of read_buf_ctx object", stderr);
        rc = EXIT_FAILURE;
        break;
      }
    }
    // obtain results from all the async futures
    for(auto &fut : futures) {
      auto rtn = fut.get();
      auto const fd  = std::get<0>(rtn);
      auto const rc2 = std::get<1>(rtn);
      if (rc2 != EXIT_SUCCESS) {
        rc = rc2;
        wr = std::get<2>(rtn);
        // removed dereference key for output context per this file descriptor
        rms.remove(fd);
        output_streams_map.erase(fd);
      }
    }
  }

  if (rc == EXIT_SUCCESS) {
    wr = (WR) WR::SUCCESS;
  }
  return std::make_tuple(rc, wr);
}

static write_result
write_to_output_stream(int fd, read_buf_ctx &rbc, FILE *const output_stream, long &input_line, std::string &str_buf,
                       const write_to_output_callback &writer)
{
  WRITE_RESULT wr{(WR) WR::NO_OP};

  auto const check_output_io = [&wr](int rc) -> bool {
    if (rc == -1) {
      fprintf(stderr,"ERROR: failed writing to output stream: [%d] %s\n", rc, strerror(errno));
      wr = (WR) WR::FAILURE;
      return false;
    }
    return true;
  };

  bool is_eintr;
  int rc;

  while (!(is_eintr = signal_handling::interrupted())) {
    fprintf(stderr, "DEBUG: string buffer capacity: %lu, string length: %lu\nDEBUG: read line (%05lu) of input:\n",
            str_buf.capacity(), str_buf.length(), input_line);
    str_buf.clear();
    rc = rbc.read_line(str_buf);
    if (rc != EXIT_SUCCESS) {
      const char* nl = nullptr;
      switch(rc) {
        case EXIT_FAILURE:
          wr = (WR) WR::FAILURE;
          break;
        case EINTR:
          wr = (WR) WR::INTERRUPTED;
          fprintf(stderr, "INFO: read-input thread interrupted; status: [%d] %s\n", rc, strerror(rc));
          continue;
        case EOF:
          wr = (WR) WR::END_OF_FILE;
          nl = "\n";
          break;
        default:
          wr = (WR) WR::NO_OP;
      }
      if (!str_buf.empty()) {
        auto rc2 = writer(output_stream, str_buf.c_str(), nl); // write to output whatever is in string buffer
        if (check_output_io(rc2)) {
          rc2 = fflush(output_stream); // flushing output because reached end-of-file, was interrupted, or input failure
          check_output_io(rc2);
        }
        if (rc == EOF && rc2 == -1) {
          rc = EXIT_FAILURE;
        }
      }
      return std::make_tuple(fd, rc, wr);
    }
    rc = writer(output_stream, str_buf.c_str(), "\n"); // write string buffer as a line of text to output stream
    if (check_output_io(rc)) {
      input_line++;
    } else {
      fflush(output_stream); // encountered error condition writing to output, but still making attempt to flush output
      return std::make_tuple(fd, EXIT_FAILURE, wr);
    }
    break;
  }
  rc = fflush(output_stream); // flushing output because just wrote a full text line
  rc = check_output_io(rc) ? EXIT_SUCCESS : EXIT_FAILURE;
  if (is_eintr) {
    fputs("DEBUG: breaking out of read-line input loop due to interrupt signal\n", stderr);
  }
  return std::make_tuple(fd, rc, wr);
}

/*
static void do_on_exit() {
  extern void test();
  test();
  fputs("DEBUG: call to test completed\n", stderr);
}
*/
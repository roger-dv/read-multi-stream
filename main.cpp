//
// Created by rogerv on 4/12/18.
//

#include <cstring>
#include <unistd.h>
#include <cxxabi.h>
#include "signal-handling.h"
#include "util.h"
#include "uncompress-stream.h"
#include "read-multi-strm.h"

static void do_on_exit();

int main(int argc, char **argv) {
  try {
    signal_handling::set_signals_handler();

    atexit(do_on_exit);

    u_int read_buf_size = 64; // default

    auto const stdin_fd = get_file_desc(stdin, __LINE__); // default
    if (stdin_fd == -1) {
      fprintf(stderr, "ERROR: unexpected error - unable to obtain stdin file descriptor\n");
      return EXIT_FAILURE;
    }
    dbg_dump_file_desc_flags(stdin_fd);

    struct {
      int fd;
    } input_src{stdin_fd}; // default input stream
    using fd_t = decltype(input_src);
    auto const cleanup_src_input = [stdin_fd](fd_t *p) {
      if (p != nullptr && p->fd != stdin_fd && p->fd > 2) {
        close(p->fd);
        p->fd = -1;
      }
    };
    using fd_close_t = decltype(cleanup_src_input);
    std::unique_ptr<fd_t, fd_close_t> sp_input_src(&input_src, cleanup_src_input);

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
                  sp_input_src.reset(nullptr);
                  input_src.fd = fd_stdout;
                  sp_input_src.reset(&input_src);

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

    // TODO: for debug diagnostics - can comment out when done with its use
    if (dbg_echo_input_source(sp_input_src->fd, __LINE__)) {
      return EXIT_SUCCESS;
    }

    read_buf_ctx rbc(sp_input_src->fd, read_buf_size);
    if (rbc.is_valid_init()) {
      const auto curr_thrd = pthread_self();
      signal_handling::register_ctrl_z_handler([curr_thrd](int sig){
//        fprintf(stderr, "DEBUG: << %s(sig: %d)\n", "signal_interrupt_thread", sig);
        pthread_kill(curr_thrd, sig);
      });
    } else {
      fputs("ERROR: failed to construct/initialize read_buf_ctx object", stderr);
      return EXIT_FAILURE;
    }
    std::string str_buf;
    str_buf.reserve(16);

    for(int i = 1; !signal_handling::interrupted(); i++) {
      fprintf(stderr, "DEBUG: string buffer capacity: %lu, string length: %lu\nDEBUG: read line (%03d) of input:\n",
             str_buf.capacity(), str_buf.length(), i);
      str_buf.clear();
      auto rc = rbc.read_line_on_ready(str_buf);
      if (rc != EXIT_SUCCESS || strcasecmp(str_buf.c_str(), "quit") == 0) {
        const char *msg;
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
          puts(str_buf.c_str());
        }
        fprintf(stderr, "INFO: program exiting with status: [%d] %s\n", rc, msg);
        return rc == EXIT_SUCCESS || rc == EOF ? EXIT_SUCCESS : EXIT_FAILURE;
      }
      puts(str_buf.c_str());
    }
    fputs("DEBUG: program out of read-line input loop due to interrupt signal\n", stderr);
  } catch(...) {
    const auto ex_nm = get_unmangled_name(abi::__cxa_current_exception_type()->name());
    fprintf(stderr, "process %d terminating due to unhandled exception of type %s", getpid(), ex_nm.c_str());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

static void do_on_exit() {
  extern void test();
  test();
  fputs("DEBUG: call to test completed\n", stderr);
}
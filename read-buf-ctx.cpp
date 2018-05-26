//
// Created by rogerv on 4/12/18.
//

#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <cassert>
#include <fcntl.h>
#include "signal-handling.h"
#include "read-buf-ctx.h"

static int get_dup_file_desc(int fd, const int line_nbr) {
  fd = dup(fd);
  if (fd == -1) {
    fprintf(stderr, "ERROR: %d: %s() -> dup(): %s\n", line_nbr, __FUNCTION__, strerror(errno));
    return -1;
  }
  return fd;
}

void close_dup_fd(fd_t *p) {
  if (p != nullptr && p->dup_fd >= 0) {
    close(p->dup_fd);
    p->dup_fd = -1;
  }
}

read_buf_ctx::read_buf_ctx(const int input_fd, const u_int read_buf_size)
    : orig_fd{input_fd}, // file descriptor of input source
      read_buffer{static_cast<char *const>(malloc(read_buf_size))},
      read_buf_limit{read_buf_size - 1},
      sp_input_fd{this, &close_dup_fd},
      sp_read_buf_rb{read_buffer, &free}
{
  if (orig_fd != -1) {
    dup_fd = get_dup_file_desc(orig_fd, __LINE__); // get a dup file descriptor from original
    if (dup_fd != -1) {
      // bounds check the file descriptor against max fd set size
      assert(this->dup_fd < FD_SETSIZE);
      // set the dup file descriptor to non-blocking mode
      int flags = fcntl(dup_fd, F_GETFL, 0);
      auto rtn = fcntl(dup_fd, F_SETFL, flags | O_NONBLOCK); int line_nbr = __LINE__;
      if (rtn == -1) {
        fprintf(stderr, "ERROR: %d: %s() -> fcntl(): %s\n", line_nbr, __FUNCTION__, strerror(errno));
        sp_input_fd.reset(nullptr);
      }
    }
  }
}

read_buf_ctx &read_buf_ctx::operator=(read_buf_ctx &&rbc) noexcept {
  *const_cast<int*>(&orig_fd) = rbc.orig_fd;
  *const_cast<char**>(&read_buffer) = rbc.read_buffer;
  *const_cast<u_int*>(&read_buf_limit) = rbc.read_buf_limit;
  dup_fd = rbc.dup_fd;
  pos = rbc.pos;
  eof_flag = rbc.eof_flag;
  sp_input_fd = std::move(rbc.sp_input_fd);
  sp_read_buf_rb = std::move(rbc.sp_read_buf_rb);
  return *this;
}


read_buf_ctx::~read_buf_ctx() {
  auto const ptr = sp_input_fd ? sp_input_fd.get() : nullptr;
  auto const ofd = ptr != nullptr ? ptr->orig_fd : -1;
  auto const dfd = ptr != nullptr ? ptr->dup_fd  : -1;
  auto const rb = sp_read_buf_rb ? sp_read_buf_rb.get() : nullptr;
  fprintf(stderr, "DEBUG: << %p->%s(), orig_fd: %d, dup_fd: %d, read_buffer: %p\n", this, __FUNCTION__, ofd, dfd, rb);
}

/**
 * Function that uses POSIX select() API to detect input availability on a
 * file descriptor. The file descriptor has been duped from the original
 * (as extracted from a FILE stream) and has been set to non-blocking mode.
 * So when data is available to be read, it will be read using read() API
 * in non-blocking manner until read() indicates there is no more data to
 * be read; then returns back to the select() call.
 *
 * A line of text as ended by either LF or CRLF convention is appended
 * to the supplied std::string in/out reference parameter.
 *
 * Once a line ending condition has been read, or if the input file descriptor
 * indicates end of file due to Control-Z or being closed, then the function
 * returns.
 *
 * The function return result indicates whether error condition was encountered
 * (which will be logged to stderr at point of detection).
 *
 * @param output_strbuf
 * @return EXIT_SUCCESS when no error condition encountered, EXIT_FAILURE if
 * was an error, EINTR if a signal terminated a call to select() or the call
 * to read(), or EOF if end of input condition encountered
 */
int read_buf_ctx::read_line_on_ready(std::string &output_strbuf) {
  if (this->eof_flag) {
    bool eol = false;
    if (this->pos > 0) {
      // insure any content in the read buffer gets appended to the string buffer
      const char * const end = this->read_buffer + this->pos;
      char * const pLF = this->read_buffer;
      assert(pLF < end);
      eol = find_next_eol(pLF, end, output_strbuf);
    }
    return eol && this->pos > 0 ? EXIT_SUCCESS : EOF;
  }

  int rc = EXIT_SUCCESS;
  fd_set rfd_set{0};
  struct timeval tv{5, 0}; // Wait up to five seconds

  FD_ZERO(&rfd_set);
  FD_SET(this->orig_fd, &rfd_set); // select on the original file descriptor

  int highest_fd = -1;
  for(int i = 0; i < FD_SETSIZE; i++) {
    if (FD_ISSET(i, &rfd_set) && i > highest_fd) {
      highest_fd = i;
    }
  }

  while(!signal_handling::interrupted()) {
    /* Watch input stream to see when it has input. */
    auto ret_val = select(highest_fd + 1, &rfd_set, nullptr, nullptr, &tv); int line_nbr = __LINE__;
    /* Don't rely on the value of tv now! */
    if (ret_val == -1) {
      const auto ec = errno;
      if (ec == EINTR) {
        return ec; // signal for exiting condition detected so bail out immediately
      }
      fprintf(stderr, "ERROR: %d: %s() -> select(): %s\n", line_nbr, __FUNCTION__, strerror(ec));
      return EXIT_FAILURE;
    }

    if (ret_val > 0) {
      if (FD_ISSET(this->orig_fd, &rfd_set)) {
        fputs("DEBUG: Data is available now:\n", stderr);
        bool had_data; // flag which indicates whether to keep reading input
        bool eol = false;
        do {
          char * const rd_buf_base =  this->read_buffer + this->pos;
          const auto rd_buf_size = this->read_buf_limit - this->pos;
          const auto n = read(this->dup_fd, rd_buf_base, rd_buf_size);
          had_data = n > 0;
          if (had_data) {
            char *const end = &rd_buf_base[n];
            *end = '\0';  /* null terminate the read buffer contents to be a valid C string   */
                          /* (the buffer is sized to allow for a terminating null character)  */
//            fprintf(stderr, "TRACE1: %p %03lu '%s'\n", (void *) rd_buf_base, n, rd_buf_base);
            char * const pLF = this->read_buffer;
            assert(pLF < end);
            eol = find_next_eol(pLF, end, output_strbuf);
          } else if (n == 0) { // indicates end-of-file condition was encountered by read() call
            fprintf(stderr, "DEBUG: %d %s() -> eof reached\n", __LINE__, __FILE__);
            rd_buf_base[n] = '\0'; // insure is null terminated to a valid C string
            this->eof_flag = true;
            if (this->pos > 0) {
              // insure any content in the read buffer gets appended to the string buffer
              const char * const end = rd_buf_base;
              char * const pLF = this->read_buffer;
              assert(pLF < end);
              eol = find_next_eol(pLF, end, output_strbuf);
            }
            rc = eol && this->pos > 0 ? EXIT_SUCCESS : EOF;
          }
        } while(had_data && !eol);
      }
      break;
    }

    /* Wait up to five seconds. */
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    FD_ZERO(&rfd_set);
    FD_SET(this->orig_fd, &rfd_set); // select on the original file descriptor
  }

  return signal_handling::interrupted() ? EINTR : rc; // the dup file descriptor is closed by smart pointer
}

bool read_buf_ctx::find_next_eol(char *pLF, const char * const end, std::string &output_strbuf) {
  bool eol = false;

  pLF = strchr(pLF, '\n');
  if (pLF != nullptr) {
    *pLF = '\0'; // the LF is changed to a null to be a valid C string termination
    eol = true;

    // deal with possibility of CRLF combo - have detected the LF, now check for prior CR
    const auto rd_buf_frag_size = static_cast<u_int>(pLF - this->read_buffer);
    if (rd_buf_frag_size > 0) {
      auto const pPrevCh = pLF - 1;
      if (*pPrevCh == '\r') {
        *pPrevCh = '\0'; // now null terminate to valid C string at where CR is found
      }
      output_strbuf += this->read_buffer; // append a text line fragment that has a detected eol condition
    } else if (!output_strbuf.empty() && output_strbuf.back() == '\r') {
      output_strbuf.pop_back(); // remove the CR at end of string buffer
    }

    this->pos = 0;

    // copy any text fragment remaining beyond detected eol to the front of the read buffer
    const char *const pNextCh = pLF + 1;
    const auto count = static_cast<u_int>(end - pNextCh);
    if (count > 0) {
      memmove(this->read_buffer, pNextCh, count);
      this->read_buffer[count] = '\0'; // null terminate to valid C string
      this->pos = count;
//      fprintf(stderr, "TRACE2: %p %03u '%s'\n", (void *) this->read_buffer, count, this->read_buffer);
    }
  } else if (end > this->read_buffer) {
    output_strbuf += this->read_buffer; // append a text line fragment (no eol detected)
    this->pos = 0;
  }

  return eol;
}
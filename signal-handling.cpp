//
// Created by rogerv on 4/12/18.
//

#include <cstdlib>
#include <csignal>
#include <cassert>
#include <mutex>
#include "signal-handling.h"

namespace signal_handling {

  volatile sig_atomic_t quit_flag{0};

  static void signal_callback_handler(int /*sig*/) { // can be called asynchronously
    quit_flag = 1;
//    fprintf(stderr, "DEBUG: << %s(sig: %d)\n", __FUNCTION__, sig);
  }

  void set_signals_handler() {
    static std::mutex guard;
    std::unique_lock<std::mutex> lk(guard);
    quit_flag = 0;
    signal(SIGINT, signal_callback_handler);
    signal(SIGTERM, signal_callback_handler);
    signal(SIGTSTP, signal_callback_handler);
  }

  static int ctrl_z_handler_sig = SIGINT;
  static ctrl_z_handler_t ctrl_z_handler{[](int /*sig*/) { signal_callback_handler(SIGINT); }};

  static void signal_callback_ctrl_z_handler(int sig) { // can be called asynchronously
    assert(sig == SIGTSTP);
    auto const sav_cb = signal(ctrl_z_handler_sig, SIG_IGN);
    ctrl_z_handler.operator()(ctrl_z_handler_sig);
    signal(ctrl_z_handler_sig, sav_cb);
//    fprintf(stderr, "DEBUG: << %s(sig: %d)\n", __FUNCTION__, sig);
  }

  void register_ctrl_z_handler(ctrl_z_handler_t cb) {
    register_ctrl_z_handler(SIGINT, std::move(cb));
  }

  void register_ctrl_z_handler(int sig, ctrl_z_handler_t cb) {
    static std::mutex guard;
    std::unique_lock<std::mutex> lk(guard);
    ctrl_z_handler_sig = sig;
    ctrl_z_handler = std::move(cb);
    signal(SIGTSTP, signal_callback_ctrl_z_handler);
  }

}
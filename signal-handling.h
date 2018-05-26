//
// Created by rogerv on 4/12/18.
//

#ifndef SIGNAL_HANDLING_H
#define SIGNAL_HANDLING_H

#include <cstdlib>
#include <csignal>
#include <functional>

namespace signal_handling {
  void set_signals_handler();
  using ctrl_z_handler_t = std::function<void(int)>;
  void register_ctrl_z_handler(ctrl_z_handler_t /*handler*/);
  void register_ctrl_z_handler(int /*sig*/, ctrl_z_handler_t /*handler*/);
  extern volatile sig_atomic_t quit_flag;
  inline bool interrupted() { return quit_flag != 0; }
}

#endif //SIGNAL_HANDLING_H
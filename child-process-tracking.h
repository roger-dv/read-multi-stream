//
// Created by rogerv on 4/21/18.
//

#ifndef CHILD_PROCESS_TRACKING_H
#define CHILD_PROCESS_TRACKING_H

void start_tracking_child_process(int child_pid, int stdout_wr_fd, int stderr_wr_fd);

#endif //CHILD_PROCESS_TRACKING_H
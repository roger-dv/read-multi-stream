//
// Created by rogerv on 4/21/18.
//

#ifndef UTIL_H
#define UTIL_H

#include <cstdio>
#include <string>

std::string get_unmangled_name(const char * mangled_name);
int  get_file_desc(FILE *stream, int line_nbr);
bool valid_file(const char * filepath);
bool has_ending(std::string full_str, std::string ending, int line_nbr);
bool dbg_echo_input_source(int fd, int line_nbr);
void dbg_dump_file_desc_flags(int fd);

#endif //UTIL_H